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
#include "tests/AdaptiveDepthReference.h"
#include <vector>

namespace
{
struct ObjectBounds
{
    std::array<float, 4> lower, upper;
};
constexpr unsigned Width = 640, Height = 480, TilePixels = 32, BlurPixels = 64;
constexpr unsigned TilesX = 20, TilesY = 15, BlurX = 10, BlurY = 8;
constexpr unsigned ScalarOffset = 2048, RgbOffset = ScalarOffset + TilesX * TilesY * 64;
constexpr unsigned BlurOffset = RgbOffset + TilesX * TilesY * 64, OutputWords = BlurOffset + BlurX * BlurY;
unsigned DepthSlice(float z)
{
    return std::min(2047u, unsigned(std::max(0.f, std::log2(1 + std::max(0.f, z)) / std::log2(81.f) * 2048)));
}

std::vector<unsigned> Reference(const std::vector<ObjectBounds> &objects)
{
    std::vector<unsigned> out(OutputWords);
    for (const auto &object : objects)
    {
        const auto &lo = object.lower;
        const auto &hi = object.upper;
        if (hi[2] < .05f || lo[2] > 80.f)
            continue;
        // An AABB is outside if none of its corners lie inside a side plane.
        // Keep this corner oracle independent of the shader's p-vertex form.
        bool outside = false;
        for (unsigned axis = 0; axis < 2; ++axis)
            for (float sign : {-1.f, 1.f})
            {
                float maximum = -1e30f;
                for (unsigned corner = 0; corner < 8; ++corner)
                {
                    float coord = (corner & (1u << axis)) ? hi[axis] : lo[axis];
                    float z = (corner & 4) ? hi[2] : lo[2];
                    maximum = std::max(maximum, z * (1.f + 2.f / (axis ? Height : Width)) + sign * coord);
                }
                outside |= maximum < -1e-5f;
            }
        if (outside)
            continue;
        float xmin = 0, ymin = 0, xmax = Width, ymax = Height;
        if (lo[2] > .05f)
        {
            xmin = ymin = 1e30f;
            xmax = ymax = -1e30f;
            for (unsigned c = 0; c < 8; ++c)
            {
                float x = (c & 1) ? hi[0] : lo[0], y = (c & 2) ? hi[1] : lo[1], z = (c & 4) ? hi[2] : lo[2];
                x = (x / z * .5f + .5f) * Width;
                y = (-y / z * .5f + .5f) * Height;
                xmin = std::min(xmin, x);
                xmax = std::max(xmax, x);
                ymin = std::min(ymin, y);
                ymax = std::max(ymax, y);
            }
        }
        float pad = std::max(hi[3], 0.f);
        xmin = std::floor(xmin);
        ymin = std::floor(ymin);
        xmax = std::ceil(xmax);
        ymax = std::ceil(ymax);
        const unsigned first = DepthSlice(lo[2]), last = DepthSlice(hi[2]);
        for (unsigned y = 0; y < TilesY; ++y)
            for (unsigned x = 0; x < TilesX; ++x)
            {
                if ((x + 1) * TilePixels <= xmin - 1 || x * TilePixels >= xmax + 1 ||
                    (y + 1) * TilePixels <= ymin - 1 || y * TilePixels >= ymax + 1)
                    continue;
                for (unsigned z = first; z <= last; ++z)
                {
                    out[z] = 1;
                    out[(unsigned(lo[3]) & 1 ? RgbOffset : ScalarOffset) + (y * TilesX + x) * 64 + z / 32] |=
                        1u << (z % 32);
                }
            }
        for (unsigned y = 0; y < BlurY; ++y)
            for (unsigned x = 0; x < BlurX; ++x)
            {
                if (!((x + 1) * BlurPixels <= xmin - 1 || x * BlurPixels >= xmax + 1 ||
                      (y + 1) * BlurPixels <= ymin - 1 || y * BlurPixels >= ymax + 1))
                    out[BlurOffset + y * BlurX + x] |= 4u;
                if ((x + 1) * BlurPixels <= xmin - pad || x * BlurPixels >= xmax + pad ||
                    (y + 1) * BlurPixels <= ymin - pad || y * BlurPixels >= ymax + pad)
                    continue;
                out[BlurOffset + y * BlurX + x] |=
                    ((unsigned(lo[3]) & 2) ? 1u : 0u) | ((unsigned(lo[3]) & 4) ? 2u : 0u);
            }
    }
    return out;
}
} // namespace

bool RunBoundsTest(nvrhi::IDevice *device)
{
    using namespace donut;
    auto fs = std::make_shared<vfs::NativeFileSystem>();
    engine::ShaderFactory factory(device, fs,
                                  app::GetDirectoryWithExecutable() / "shaders/avboit" /
                                      app::GetShaderTypeName(device->getGraphicsAPI()));
    auto shader = factory.CreateShader("shaders/BoundsOccupancy.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    if (!shader)
        return false;
    auto input = device->createBuffer(nvrhi::BufferDesc()
                                          .setByteSize(16384)
                                          .setCanHaveTypedViews(true)
                                          .setFormat(nvrhi::Format::RGBA32_FLOAT)
                                          .setInitialState(nvrhi::ResourceStates::CopyDest)
                                          .setKeepInitialState(true));
    auto output = device->createBuffer(nvrhi::BufferDesc()
                                           .setByteSize(OutputWords * 4)
                                           .setCanHaveTypedViews(true)
                                           .setCanHaveUAVs(true)
                                           .setFormat(nvrhi::Format::R32_UINT)
                                           .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
                                           .setKeepInitialState(true));
    auto readback = device->createBuffer(nvrhi::BufferDesc()
                                             .setByteSize(OutputWords * 4)
                                             .setCpuAccess(nvrhi::CpuAccessMode::Read)
                                             .setInitialState(nvrhi::ResourceStates::CopyDest)
                                             .setKeepInitialState(true));
    auto screen = device->createTexture(nvrhi::TextureDesc()
                                            .setWidth(BlurX)
                                            .setHeight(BlurY)
                                            .setFormat(nvrhi::Format::R32_UINT)
                                            .setIsUAV(true)
                                            .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
                                            .setKeepInitialState(true));
    auto screenReadback = device->createStagingTexture(screen->getDesc(), nvrhi::CpuAccessMode::Read);
    auto bindings = nvrhi::BindingSetDesc()
                        .addItem(nvrhi::BindingSetItem::TypedBuffer_SRV(0, input))
                        .addItem(nvrhi::BindingSetItem::TypedBuffer_UAV(0, output))
                        .addItem(nvrhi::BindingSetItem::Texture_UAV(1, screen));
    nvrhi::BindingLayoutHandle layout;
    nvrhi::BindingSetHandle set;
    if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::Compute, 0, bindings, layout, set))
        return false;
    auto pipeline =
        device->createComputePipeline(nvrhi::ComputePipelineDesc().setComputeShader(shader).addBindingLayout(layout));
    auto warpShader = factory.CreateShader("shaders/AdaptiveDepth.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    const unsigned warpWords = 2 + 2 * (avboit::VirtualSlices + 1);
    auto warp = device->createBuffer(nvrhi::BufferDesc()
                                         .setByteSize(warpWords * 4)
                                         .setCanHaveTypedViews(true)
                                         .setCanHaveUAVs(true)
                                         .setFormat(nvrhi::Format::R32_UINT)
                                         .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
                                         .setKeepInitialState(true));
    auto warpReadback = device->createBuffer(nvrhi::BufferDesc()
                                                 .setByteSize(warpWords * 4)
                                                 .setCpuAccess(nvrhi::CpuAccessMode::Read)
                                                 .setInitialState(nvrhi::ResourceStates::CopyDest)
                                                 .setKeepInitialState(true));
    auto warpBindings = nvrhi::BindingSetDesc()
                            .addItem(nvrhi::BindingSetItem::TypedBuffer_SRV(0, output))
                            .addItem(nvrhi::BindingSetItem::TypedBuffer_UAV(0, warp));
    nvrhi::BindingLayoutHandle warpLayout;
    nvrhi::BindingSetHandle warpSet;
    if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::Compute, 0, warpBindings, warpLayout,
                                                 warpSet))
        return false;
    auto warpPipeline = device->createComputePipeline(
        nvrhi::ComputePipelineDesc().setComputeShader(warpShader).addBindingLayout(warpLayout));
    constexpr unsigned PhysicalWords = TilesX * TilesY * 8;
    auto physical = device->createBuffer(nvrhi::BufferDesc()
                                             .setByteSize(PhysicalWords * 4)
                                             .setCanHaveTypedViews(true)
                                             .setCanHaveUAVs(true)
                                             .setFormat(nvrhi::Format::R32_UINT)
                                             .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
                                             .setKeepInitialState(true));
    auto physicalReadback = device->createBuffer(nvrhi::BufferDesc()
                                                     .setByteSize(PhysicalWords * 4)
                                                     .setCpuAccess(nvrhi::CpuAccessMode::Read)
                                                     .setInitialState(nvrhi::ResourceStates::CopyDest)
                                                     .setKeepInitialState(true));
    auto physicalShader =
        factory.CreateShader("shaders/PhysicalOccupancy.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    auto physicalBindings = nvrhi::BindingSetDesc()
                                .addItem(nvrhi::BindingSetItem::TypedBuffer_SRV(0, output))
                                .addItem(nvrhi::BindingSetItem::TypedBuffer_SRV(1, warp))
                                .addItem(nvrhi::BindingSetItem::TypedBuffer_SRV(2, input))
                                .addItem(nvrhi::BindingSetItem::TypedBuffer_UAV(0, physical));
    nvrhi::BindingLayoutHandle physicalLayout;
    nvrhi::BindingSetHandle physicalSet;
    if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::Compute, 0, physicalBindings,
                                                 physicalLayout, physicalSet))
        return false;
    auto physicalPipeline = device->createComputePipeline(
        nvrhi::ComputePipelineDesc().setComputeShader(physicalShader).addBindingLayout(physicalLayout));
    auto commands = device->createCommandList();
    std::mt19937 random(531);
    for (unsigned test = 0; test < 37; ++test)
    {
        std::vector<ObjectBounds> objects;
        if (test == 0)
            objects.push_back({{-.8f, -.6f, 2, 3}, {.8f, .6f, 3, 96}}); // RGB frost
        if (test == 1)
            objects.push_back({{-.8f, -.6f, 2, 1}, {.8f, .6f, 3, 96}}); // Same bounds, no frost
        if (test == 2)
            objects.push_back({{-1, -1, -1, 2}, {1, 1, 2, 48}}); // Near-plane crossing
        if (test == 3)
            objects.push_back({{-1, -1, -3, 3}, {1, 1, -1, 48}}); // Behind camera
        if (test == 4)
            objects.push_back({{-1, -1, 81, 3}, {1, 1, 82, 48}}); // Beyond far
        if (test == 5)
            objects.push_back({{10, 10, 1, 3}, {11, 11, 2, 48}}); // Off-screen
        if (test >= 6 && test < 32 && test % 3 != 0)
            for (unsigned i = 0; i < 80; ++i)
            {
                const float x = float(int(random() % 200) - 100) * .03125f,
                            y = float(int(random() % 160) - 80) * .03125f, z = .5f + float(random() % 100) * .125f;
                objects.push_back(
                    {{x, y, z, float(random() % 8)}, {x + .375f, y + .625f, z + .5f, float(random() % 4) * 32}});
            }
        if (test == 32)
            objects.push_back({{-.8f, -5.f, -1.f, 7}, {.8f, -4.f, 1.f, 8192}}); // Below sky view, crossing near
        if (test == 33)
            objects.push_back({{4.f, -.5f, -1.f, 7}, {5.f, .5f, 1.f, 8192}}); // Right, crossing near
        if (test == 34)
            objects.push_back({{-5.f, -.5f, -1.f, 7}, {-4.f, .5f, 1.f, 8192}}); // Left, crossing near
        if (test == 35)
            objects.push_back({{-.8f, 4.f, -1.f, 7}, {.8f, 5.f, 1.f, 8192}}); // Above, crossing near
        if (test == 36)
            objects.push_back({{-.1f, -.1f, -1.f, 7}, {.1f, .1f, 1.f, 8192}}); // Genuine near-plane crossing
        // Every third frame is empty, testing old mask data cannot survive.
        const auto expected = Reference(objects);
        std::vector<std::array<float, 4>> data = {
            {{Width, Height, TilePixels, BlurPixels}}, {{1, 1, .05f, 80}}, {{1, float(objects.size()), 0, 0}}};
        for (const auto &o : objects)
        {
            data.push_back(o.lower);
            data.push_back(o.upper);
        }
        commands->open();
        commands->clearTextureUInt(screen, nvrhi::AllSubresources, 0);
        commands->clearBufferUInt(output, 0);
        commands->writeBuffer(input, data.data(), data.size() * 16);
        if (!objects.empty())
        {
            commands->setComputeState(nvrhi::ComputeState().setPipeline(pipeline).addBindingSet(set));
            commands->dispatch(unsigned(objects.size()), 1, 1);
        }
        commands->setComputeState(nvrhi::ComputeState().setPipeline(warpPipeline).addBindingSet(warpSet));
        commands->dispatch(1, 1, 1);
        commands->setComputeState(nvrhi::ComputeState().setPipeline(physicalPipeline).addBindingSet(physicalSet));
        commands->dispatch(TilesX * TilesY, 1, 1);
        commands->copyBuffer(physicalReadback, 0, physical, 0, PhysicalWords * 4);
        commands->copyBuffer(warpReadback, 0, warp, 0, warpWords * 4);
        commands->copyTexture(screenReadback, {}, screen, {});
        commands->copyBuffer(readback, 0, output, 0, OutputWords * 4);
        commands->close();
        device->executeCommandList(commands);
        device->waitForIdle();
        const auto *raw = static_cast<const unsigned *>(device->mapBuffer(readback, nvrhi::CpuAccessMode::Read));
        if (!raw)
            return false;
        std::vector<unsigned> actual(raw, raw + OutputWords);
        device->unmapBuffer(readback);
        size_t rowPitch = 0;
        auto screenData = static_cast<const char *>(
            device->mapStagingTexture(screenReadback, {}, nvrhi::CpuAccessMode::Read, &rowPitch));
        if (!screenData)
            return false;
        for (unsigned y = 0; y < BlurY; ++y)
            for (unsigned x = 0; x < BlurX; ++x)
                actual[BlurOffset + y * BlurX + x] = reinterpret_cast<const unsigned *>(screenData + y * rowPitch)[x];
        device->unmapStagingTexture(screenReadback);
        unsigned mismatch = OutputWords;
        for (unsigned i = 0; i < OutputWords; ++i)
            if (actual[i] != expected[i])
            {
                mismatch = i;
                break;
            }
        if (mismatch != OutputWords)
            printf("Bounds case %u mismatch word %u: GPU=%u CPU=%u\n", test, mismatch, actual[mismatch],
                   expected[mismatch]);
        if (mismatch != OutputWords)
            return false;
        auto warpExpected = avboit::BuildDepthWarp(std::vector<unsigned>(expected.begin(), expected.begin() + 2048));
        auto warpActual = static_cast<const unsigned *>(device->mapBuffer(warpReadback, nvrhi::CpuAccessMode::Read));
        bool warpMatches = warpActual && std::equal(warpExpected.begin(), warpExpected.end(), warpActual);
        if (warpActual)
            device->unmapBuffer(warpReadback);
        if (!warpMatches)
        {
            printf("Bounds -> Adaptive Z case %u FAILED\n", test);
            return false;
        }
        std::vector<unsigned> physicalExpected(PhysicalWords, 0);
        for (unsigned tile = 0; tile < TilesX * TilesY; ++tile)
            for (unsigned kind = 0; kind < 2; ++kind)
                for (unsigned z = 0; z < 2048; ++z)
                {
                    unsigned source = (kind ? RgbOffset : ScalarOffset) + tile * 64 + z / 32;
                    if ((expected[source] & (1u << (z % 32))) == 0)
                        continue;
                    unsigned accepted = z / (2048 / warpExpected[0]);
                    unsigned first = std::min(126u, warpExpected[2 + accepted * 2]);
                    for (unsigned tap = first > 0 ? first - 1 : 0; tap <= std::min(127u, first + 2); ++tap)
                        physicalExpected[tile * 8 + kind * 4 + tap / 32] |= 1u << (tap % 32);
                }
        auto physicalActual =
            static_cast<const unsigned *>(device->mapBuffer(physicalReadback, nvrhi::CpuAccessMode::Read));
        bool physicalMatches =
            physicalActual && std::equal(physicalExpected.begin(), physicalExpected.end(), physicalActual);
        if (physicalActual)
            device->unmapBuffer(physicalReadback);
        if (!physicalMatches)
        {
            printf("Physical occupancy case %u FAILED\n", test);
            return false;
        }
        printf("Bounds + frost -> Adaptive Z -> physical masks case %u PASS\n", test);
        device->runGarbageCollection();
    }
    return true;
}
