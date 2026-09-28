// SPDX-License-Identifier: MIT
#include <donut/app/ApplicationBase.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/core/vfs/VFS.h>
#include <nvrhi/utils.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace
{
constexpr unsigned Rays = 16, Slices = 128, ScalarWords = Rays * Slices / 4, StorageWords = ScalarWords + Rays * Slices;
using Float4 = std::array<float, 4>;
struct Event
{
    Float4 location, tau;
};
unsigned Quantize(float tau)
{
    return unsigned(std::clamp(tau / std::log(255.f), 0.f, 1.f) * 255.f + .5f);
}
std::vector<Float4> Reference(const std::vector<Event> &events)
{
    std::vector<std::array<unsigned, 4>> extinction(Rays * Slices);
    for (const auto &e : events)
    {
        unsigned ray = unsigned(e.location[0]);
        float z = std::clamp(e.location[1], 0.f, 126.9999f);
        unsigned slice = unsigned(z);
        float f = z - slice;
        for (unsigned tap = 0; tap < 2; ++tap)
        {
            float weight = tap ? f : 1 - f;
            auto &value = extinction[ray * Slices + slice + tap];
            if (e.location[2])
                value[0] += Quantize(e.tau[0] * weight);
            else
                for (unsigned c = 0; c < 3; ++c)
                    value[c + 1] += Quantize(e.tau[c] * weight);
        }
    }
    std::vector<Float4> result(Rays * Slices);
    for (unsigned ray = 0; ray < Rays; ++ray)
    {
        float integral[3] = {};
        bool saturated[4] = {};
        for (unsigned slice = 0; slice < Slices; ++slice)
        {
            auto e = extinction[ray * Slices + slice];
            for (unsigned c = 0; c < 4; ++c)
                saturated[c] |= e[c] > 255;
            for (unsigned c = 0; c < 3; ++c)
            {
                integral[c] += (e[0] + e[c + 1]) * std::log(255.f) / 255.f;
                result[ray * Slices + slice][c] = saturated[0] || saturated[c + 1] ? 0.f : std::exp(-integral[c]);
            }
            result[ray * Slices + slice][3] = 1;
        }
    }
    return result;
}

// Independent integer-sum oracle: modulo-2^32 addition is order invariant,
// including scalar byte carry and RGB guard-field carry.
std::vector<unsigned> PackedWords(const std::vector<Event> &events)
{
    std::vector<unsigned> words(StorageWords);
    for (const auto &e : events)
    {
        unsigned ray = unsigned(e.location[0]);
        float z = std::clamp(e.location[1], 0.f, 126.9999f);
        unsigned slice = unsigned(z);
        float fraction = z - slice;
        for (unsigned tap = 0; tap < 2; ++tap)
        {
            float weight = tap ? fraction : 1 - fraction;
            unsigned at = slice + tap;
            if (e.location[2])
                words[ray * 32 + at / 4] += Quantize(e.tau[0] * weight) << ((at % 4) * 8);
            else
                words[ScalarWords + ray * Slices + at] += Quantize(e.tau[0] * weight) +
                                                          (Quantize(e.tau[1] * weight) << 10) +
                                                          (Quantize(e.tau[2] * weight) << 20);
        }
    }
    return words;
}

// For isolated single-channel tests, a carrying channel is already saturated;
// its adjacent field receives floor(sum/1024). Up to 1024 events, that carry
// stays <=255, so the adjacent channel never legitimately sets overflow.
// This predicts the packed approximation separately from ideal RGB extinction.
std::vector<Float4> SingleChannelPackedReference(const std::vector<Event> &events, unsigned primary)
{
    auto words = PackedWords(events);
    auto ideal = Reference(events);
    auto result = ideal;
    for (unsigned ray = 0; ray < Rays; ++ray)
    {
        float integral[3] = {};
        for (unsigned slice = 0; slice < Slices; ++slice)
        {
            unsigned rgb = words[ScalarWords + ray * Slices + slice];
            for (unsigned c = 0; c < 3; ++c)
            {
                integral[c] += float((rgb >> (c * 10)) & 1023u) * std::log(255.f) / 255.f;
                if (c != primary)
                    result[ray * Slices + slice][c] = std::exp(-integral[c]);
            }
        }
    }
    return result;
}
} // namespace

bool RunExtinctionTest(nvrhi::IDevice *device)
{
    using namespace donut;
    auto fs = std::make_shared<vfs::NativeFileSystem>();
    engine::ShaderFactory factory(device, fs,
                                  app::GetDirectoryWithExecutable() / "shaders/avboit" /
                                      app::GetShaderTypeName(device->getGraphicsAPI()));
    auto splat = factory.CreateShader("shaders/Extinction.hlsl", "splat", nullptr, nvrhi::ShaderType::Compute);
    auto integrate = factory.CreateShader("shaders/Extinction.hlsl", "integrate", nullptr, nvrhi::ShaderType::Compute);
    if (!splat || !integrate)
        return false;
    auto buffer = [&](unsigned bytes, nvrhi::Format format, bool uav)
    {
        return device->createBuffer(
            nvrhi::BufferDesc()
                .setByteSize(bytes)
                .setCanHaveTypedViews(true)
                .setCanHaveUAVs(uav)
                .setFormat(format)
                .setInitialState(uav ? nvrhi::ResourceStates::UnorderedAccess : nvrhi::ResourceStates::CopyDest)
                .setKeepInitialState(true));
    };
    auto eventsBuffer = buffer(65536, nvrhi::Format::RGBA32_FLOAT, false);
    auto extinction = buffer(StorageWords * 4, nvrhi::Format::R32_UINT, true);
    auto overflow = buffer(Rays * 4 * 4, nvrhi::Format::R32_UINT, true);
    auto transmission = buffer(Rays * Slices * 16, nvrhi::Format::RGBA32_FLOAT, true);
    auto readback = device->createBuffer(nvrhi::BufferDesc()
                                             .setByteSize(Rays * Slices * 16 + StorageWords * 4)
                                             .setCpuAccess(nvrhi::CpuAccessMode::Read)
                                             .setInitialState(nvrhi::ResourceStates::CopyDest)
                                             .setKeepInitialState(true));
    auto bindings = nvrhi::BindingSetDesc()
                        .addItem(nvrhi::BindingSetItem::TypedBuffer_SRV(0, eventsBuffer))
                        .addItem(nvrhi::BindingSetItem::TypedBuffer_UAV(0, extinction))
                        .addItem(nvrhi::BindingSetItem::TypedBuffer_UAV(1, overflow))
                        .addItem(nvrhi::BindingSetItem::TypedBuffer_UAV(2, transmission));
    nvrhi::BindingLayoutHandle layout;
    nvrhi::BindingSetHandle set;
    if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::Compute, 0, bindings, layout, set))
        return false;
    auto splatPipeline =
        device->createComputePipeline(nvrhi::ComputePipelineDesc().setComputeShader(splat).addBindingLayout(layout));
    auto integratePipeline = device->createComputePipeline(
        nvrhi::ComputePipelineDesc().setComputeShader(integrate).addBindingLayout(layout));
    auto commands = device->createCommandList();
    std::mt19937 random(711);
    float worstCarryError = 0;
    for (unsigned test = 0; test < 56; ++test)
    {
        std::vector<Event> events;
        if (test == 1)
            events.push_back({{0, 3.25f, 1, 0}, {.7f, .7f, .7f, 0}}); // DWORD boundary
        if (test == 2)
            events.push_back({{1, 2.75f, 1, 0}, {.7f, .7f, .7f, 0}}); // merged scalar splat
        if (test == 3)
            events.push_back({{2, 12.5f, 0, 0}, {.2f, .7f, 1.1f, 0}});
        if (test == 4)
            for (unsigned j = 0; j < 3; ++j)
                events.push_back({{0, 3, 1, 0}, {4, 4, 4, 0}});
        if (test == 5)
            for (unsigned j = 0; j < 3; ++j)
                events.push_back({{1, 8, 0, 0}, {4, 0, 0, 0}});
        if (test >= 6 && test < 12 && test % 3)
            for (unsigned ray = 0; ray < Rays; ++ray)
                for (unsigned j = 0; j < 24; ++j)
                {
                    float z = float(random() % 500) * .25f;
                    bool scalar = random() % 2;
                    float r = float(1 + random() % 20) * .025f, g = scalar ? r : float(1 + random() % 20) * .025f,
                          b = scalar ? r : float(1 + random() % 20) * .025f;
                    events.push_back({{float(ray), z, float(scalar), 0}, {r, g, b, 0}});
                }
        unsigned primary = 0;
        if (test >= 12 && test < 48)
        {
            unsigned index = test - 12;
            primary = (index / 6) % 3;
            const unsigned counts[] = {4, 5, 16, 64, 256, 1024};
            float z = index < 18 ? 13.f : 13.25f;
            Float4 tau = {};
            tau[primary] = 4;
            for (unsigned i = 0; i < counts[index % 6]; ++i)
                events.push_back({{2, z, 0, 0}, tau});
        }
        if (test >= 48)
        {
            // Saturated scalar rows: merged and cross-DWORD taps, uint wrap,
            // first/last legal slices and changing depth between frames.
            const float z[] = {0, 2.25f, 3.25f, 63.5f, 64, 125.75f, 126.999f, 3};
            for (unsigned i = 0; i < 1024; ++i)
                events.push_back({{3, z[test - 48], 1, 0}, {4, 4, 4, 0}});
        }
        auto ideal = Reference(events);
        auto expected = test >= 12 && test < 48 ? SingleChannelPackedReference(events, primary) : ideal;
        auto expectedWords = PackedWords(events);
        std::vector<Float4> data = {{{float(events.size()), 0, 0, 0}}};
        for (const auto &e : events)
        {
            data.push_back(e.location);
            data.push_back(e.tau);
        }
        commands->open();
        commands->clearBufferUInt(extinction, 0);
        commands->clearBufferUInt(overflow, Slices);
        commands->setBufferState(extinction, nvrhi::ResourceStates::UnorderedAccess);
        commands->setBufferState(overflow, nvrhi::ResourceStates::UnorderedAccess);
        commands->commitBarriers();
        commands->writeBuffer(eventsBuffer, data.data(), data.size() * 16);
        if (!events.empty())
        {
            commands->setComputeState(nvrhi::ComputeState().setPipeline(splatPipeline).addBindingSet(set));
            commands->dispatch(unsigned(events.size() + 63) / 64, 1, 1);
        }
        commands->setBufferState(extinction, nvrhi::ResourceStates::UnorderedAccess);
        commands->setBufferState(overflow, nvrhi::ResourceStates::UnorderedAccess);
        commands->commitBarriers();
        commands->setComputeState(nvrhi::ComputeState().setPipeline(integratePipeline).addBindingSet(set));
        commands->dispatch(1, 1, 1);
        commands->copyBuffer(readback, 0, transmission, 0, Rays * Slices * 16);
        commands->copyBuffer(readback, Rays * Slices * 16, extinction, 0, StorageWords * 4);
        commands->close();
        device->executeCommandList(commands);
        device->waitForIdle();
        auto actual = static_cast<const Float4 *>(device->mapBuffer(readback, nvrhi::CpuAccessMode::Read));
        if (!actual)
            return false;
        float error = 0;
        for (unsigned i = 0; i < Rays * Slices; ++i)
            for (unsigned c = 0; c < 4; ++c)
            {
                if (!std::isfinite(actual[i][c]))
                    error = 1e30f;
                else
                    error = std::max(error, std::abs(actual[i][c] - expected[i][c]));
            }
        auto actualWords = reinterpret_cast<const unsigned *>(actual + Rays * Slices);
        if (!std::equal(expectedWords.begin(), expectedWords.end(), actualWords))
            error = 1e30f;
        float carryError = 0;
        for (unsigned i = 0; i < Rays * Slices; ++i)
            for (unsigned c = 0; c < 3; ++c)
                carryError = std::max(carryError, std::abs(actual[i][c] - ideal[i][c]));
        worstCarryError = std::max(worstCarryError, carryError);
        if (error >= .00001f)
            for (unsigned j = 0; j < 8; ++j)
                printf("slice %u actual %.5f expected %.5f\n", j, actual[j][0], expected[j][0]);
        device->unmapBuffer(readback);
        printf("Packed extinction case %u implementation error %.8f / ideal RGB difference %.6f %s\n", test, error,
               carryError, error < .00001f ? "PASS" : "FAIL");
        if (error >= .00001f)
            return false;
        device->runGarbageCollection();
    }
    printf("Known packed RGB carry approximation: worst ideal-reference difference %.6f. This is NOT a quality pass.\n",
           worstCarryError);
    return true;
}
