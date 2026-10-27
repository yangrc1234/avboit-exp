// SPDX-License-Identifier: MIT
// Base-mask bounds, frame reset, and disconnected requests without padding.
#include <donut/app/ApplicationBase.h>
#include <donut/core/vfs/VFS.h>
#include <donut/engine/ShaderFactory.h>
#include <nvrhi/utils.h>
#include <array>
#include <vector>
#include <random>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>
#include "render/TileSchedule.h"

bool RunBlurDispatchTest(nvrhi::IDevice *device)
{
    using namespace donut;
    using Rect = std::array<unsigned, 4>;
    constexpr unsigned Width = 640, Height = 384;
    auto fs = std::make_shared<vfs::NativeFileSystem>();
    engine::ShaderFactory factory(device, fs,
                                  app::GetDirectoryWithExecutable() / "shaders/avboit" /
                                      app::GetShaderTypeName(device->getGraphicsAPI()));
    auto shader = factory.CreateShader("shaders/FrostScheduleTest.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    if (!shader)
        return false;
    auto buffer = [&](unsigned size, nvrhi::Format format, bool uav)
    {
        return device->createBuffer(
            nvrhi::BufferDesc()
                .setByteSize(size)
                .setCanHaveTypedViews(true)
                .setCanHaveUAVs(uav)
                .setFormat(format)
                .setInitialState(uav ? nvrhi::ResourceStates::UnorderedAccess : nvrhi::ResourceStates::CopyDest)
                .setKeepInitialState(true));
    };
    auto camera = buffer(10 * 16, nvrhi::Format::RGBA32_FLOAT, false),
         bounds = buffer(60 * 2 * 16, nvrhi::Format::RGBA32_FLOAT, false);
    auto rectangles = buffer(11 * 16, nvrhi::Format::RGBA32_UINT, true);
    auto commands = device->createCommandList();
    std::mt19937 random(824);
    unsigned cases = 0;
    for (unsigned count : {3u, 4u, 5u})
    {
        const auto chain = avboit::FrostChain::Make(Width, Height, count);
        const auto layout = avboit::TileSchedule::Make(Width, Height, count);
        auto workMask = buffer(layout.entries * 4, nvrhi::Format::R32_UINT, true);
        auto readback = device->createBuffer(nvrhi::BufferDesc()
                                                 .setByteSize(layout.entries * 4)
                                                 .setCpuAccess(nvrhi::CpuAccessMode::Read)
                                                 .setInitialState(nvrhi::ResourceStates::CopyDest)
                                                 .setKeepInitialState(true));
        using B = nvrhi::BindingSetItem;
        auto desc = nvrhi::BindingSetDesc()
                        .addItem(B::TypedBuffer_SRV(15, camera))
                        .addItem(B::TypedBuffer_SRV(0, bounds))
                        .addItem(B::TypedBuffer_UAV(5, rectangles, nvrhi::Format::R32_UINT))
                        .addItem(B::TypedBuffer_UAV(2, workMask));
        nvrhi::BindingLayoutHandle bindingLayout;
        nvrhi::BindingSetHandle bindings;
        if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::Compute, 0, desc, bindingLayout,
                                                     bindings))
            return false;
        auto pipeline = device->createComputePipeline(
            nvrhi::ComputePipelineDesc().setComputeShader(shader).addBindingLayout(bindingLayout));
        for (bool sparse : {false, true})
            for (unsigned fixture = 0; fixture < 38; ++fixture)
            {
                std::array<unsigned, 60> input{};
                bool full = fixture == 31, empty = fixture % 4 == 0, sharpOnly = fixture % 3 == 0;
                unsigned maxMip = fixture < 32 ? count - 1 : std::min(fixture - 32, count - 1);
                if (!empty)
                    for (unsigned i = 0; i < (fixture < 10 ? 1 : 12); ++i)
                        input[random() % 60] = sharpOnly ? 2 : 1;
                if (fixture == 37)
                {
                    input = {};
                    input[0] = input[59] = 1;
                    maxMip = 0;
                    empty = sharpOnly = false;
                }
                bool frostEmpty = empty || sharpOnly;
                std::array<std::array<float, 4>, 120> targets{};
                for (unsigned tile = 0; tile < 60; ++tile)
                {
                    unsigned x = tile % 10 * 64, y = tile / 10 * 64;
                    targets[tile * 2] = {float(x), float(y), float(x + 64), float(y + 64)};
                    targets[tile * 2 + 1][0] =
                        float((input[tile] & 1 ? 2u : 0u) | (input[tile] & 2 ? 4u : 0u) | (maxMip << 3));
                }
                if (full)
                {
                    targets[0] = {0, 0, Width, Height};
                    targets[1][0] = float(2u | (maxMip << 3));
                }
                std::array<float, 40> cam{};
                cam[18] = float(sparse);
                cam[24] = Width;
                cam[25] = Height;
                cam[32] = float(chain.width);
                cam[33] = float(chain.height);
                cam[34] = float(count);
                cam[37] = fixture % 3 == 0 ? 0.f : fixture % 3 == 1 ? 24.f : 64.f;
                commands->open();
                commands->writeBuffer(camera, cam.data(), sizeof(cam));
                commands->writeBuffer(bounds, targets.data(), sizeof(targets));
                commands->clearBufferUInt(rectangles, 0);
                commands->clearBufferUInt(workMask, 0);
                commands->setComputeState(nvrhi::ComputeState().setPipeline(pipeline).addBindingSet(bindings));
                commands->dispatch(60, 1, 1);
                commands->copyBuffer(readback, 0, workMask, 0, layout.entries * 4);
                commands->close();
                device->executeCommandList(commands);
                device->waitForIdle();
                const auto data =
                    static_cast<const unsigned *>(device->mapBuffer(readback, nvrhi::CpuAccessMode::Read));
                if (!data)
                    return false;
                std::vector<unsigned> words(data, data + layout.entries);
                device->unmapBuffer(readback);
                const unsigned *mask = words.data();
                // Independent overlap oracle: only base groups intersecting
                // requested rectangles are marked. Later mips have no masks.
                bool ok = true;
                for (unsigned stage = 0; stage < 11; ++stage)
                    if (stage != 1)
                        ok &= layout.capacity[stage] == 0;
                const unsigned pitch = (chain.width + 3) / 4;
                unsigned marked = 0;
                for (unsigned slot = 0; slot < layout.capacity[1]; ++slot)
                {
                    bool expected = full;
                    const unsigned gx = slot % pitch, gy = slot / pitch;
                    for (unsigned tile = 0; tile < 60 && !expected; ++tile)
                    {
                        if ((input[tile] & 1u) == 0)
                            continue;
                        const double x0 = double(tile % 10 * 64) * chain.width / Width;
                        const double y0 = double(tile / 10 * 64) * chain.height / Height;
                        const double x1 = double((tile % 10 + 1) * 64) * chain.width / Width;
                        const double y1 = double((tile / 10 + 1) * 64) * chain.height / Height;
                        expected = gx * 4 < std::ceil(x1) && (gx + 1) * 4 > std::floor(x0) && gy * 4 < std::ceil(y1) &&
                                   (gy + 1) * 4 > std::floor(y0);
                    }
                    const bool actual = mask[layout.offset[1] + 1 + slot] != 0;
                    ok &= actual == expected;
                    marked += actual;
                }
                if (frostEmpty && !full)
                    ok &= marked == 0;
                if (fixture == 37)
                    ok &= marked < layout.capacity[1] / 2;
                if (!ok)
                {
                    printf("Tile dependency FAIL: mips %u sparse %u fixture %u\n", count, sparse, fixture);
                    return false;
                }
                ++cases;
                device->runGarbageCollection();
            }
    }
    printf("Frost masks: %u GPU base bounds / reset / empty-gap contracts PASS\n", cases);
    return true;
}
