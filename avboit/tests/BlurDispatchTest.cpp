// SPDX-License-Identifier: MIT
// GPU producer/consumer contracts: no missing source taps,
// empty frames reset lists, and disconnected requests preserve their empty gap.
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
    auto camera = buffer(9 * 16, nvrhi::Format::RGBA32_FLOAT, false),
         bounds = buffer(60 * 2 * 16, nvrhi::Format::RGBA32_FLOAT, false);
    auto footprint = buffer(25 * 16, nvrhi::Format::RGBA32_FLOAT, false);
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
                        .addItem(B::TypedBuffer_SRV(16, footprint))
                        .addItem(B::TypedBuffer_UAV(5, rectangles, nvrhi::Format::R32_UINT))
                        .addItem(B::TypedBuffer_UAV(2, workMask));
        nvrhi::BindingLayoutHandle bindingLayout;
        nvrhi::BindingSetHandle bindings;
        if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::Compute, 0, desc, bindingLayout,
                                                     bindings))
            return false;
        auto pipeline = device->createComputePipeline(
            nvrhi::ComputePipelineDesc().setComputeShader(shader).addBindingLayout(bindingLayout));
        for (bool cubic : {false, true})
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
                    std::array<float, 36> cam{};
                    cam[18] = float(sparse);
                    cam[24] = Width;
                    cam[25] = Height;
                    cam[32] = float(chain.width);
                    cam[33] = float(chain.height);
                    cam[34] = float(count);
                    auto pads = chain.Footprints(Width, Height, cubic);
                    commands->open();
                    commands->writeBuffer(camera, cam.data(), sizeof(cam));
                    commands->writeBuffer(bounds, targets.data(), sizeof(targets));
                    commands->writeBuffer(footprint, pads.data(), sizeof(pads));
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
                    std::vector<unsigned> origins(layout.entries * 2);
                    // CPU enumeration for the independent tap oracle, not a GPU work list.
                    for (unsigned stage = 1; stage < count * 2; stage += 2)
                    {
                        unsigned base = layout.offset[stage], pitch = (layout.width[stage] + 3) / 4, n = 0;
                        for (unsigned i = 0; i < layout.capacity[stage]; ++i)
                            if (mask[base + 1 + i])
                            {
                                origins[(base + 1 + n) * 2] = i % pitch * 4;
                                origins[(base + 1 + n) * 2 + 1] = i / pitch * 4;
                                ++n;
                            }
                        origins[base * 2] = n;
                    }
                    const unsigned *list = origins.data();
                    auto contains = [&](unsigned stage, int x, int y)
                    {
                        x = std::clamp(x, 0, int(layout.width[stage]) - 1);
                        y = std::clamp(y, 0, int(layout.height[stage]) - 1);
                        unsigned block = 4u;
                        return mask[layout.offset[stage] + 1 +
                                    (y / block) * ((layout.width[stage] + block - 1) / block) + x / block] != 0;
                    };
                    bool ok = true;
                    for (unsigned stage = 0; stage < 11; ++stage)
                    {
                        unsigned w = layout.width[stage], h = layout.height[stage], base = layout.offset[stage],
                                 n = list[base * 2], block = stage == 10 ? 8u : 4u;
                        ok &= n <= layout.capacity[stage];
                        std::vector<bool> seen(layout.capacity[stage]);
                        for (unsigned i = 0; i < n && i < layout.capacity[stage]; ++i)
                        {
                            unsigned x = list[(base + 1 + i) * 2], y = list[(base + 1 + i) * 2 + 1];
                            if (x >= w || y >= h || x % block || y % block)
                            {
                                ok = false;
                                continue;
                            }
                            unsigned slot = (y / block) * ((w + block - 1) / block) + x / block;
                            ok &= !seen[slot] && mask[base + 1 + slot] != 0;
                            seen[slot] = true;
                        }
                        for (unsigned i = 0; i < layout.capacity[stage]; ++i)
                            ok &= seen[i] == (mask[base + 1 + i] != 0);
                        if ((stage < 10 && (stage & 1) == 0) || (stage < 10 && stage / 2 > maxMip))
                        {
                            ok &= n == 0;
                            continue;
                        }
                        if (stage < 10 && frostEmpty && !full)
                            ok &= n == 0;
                        if (stage < 10 && full)
                            ok &= n == layout.capacity[stage];
                        if (stage == 10)
                            continue;
                        // Exercise every produced work block's corners, including tails.
                        for (unsigned i = 0; i < n; ++i)
                            for (unsigned oy : {0u, block - 1})
                                for (unsigned ox : {0u, block - 1})
                                {
                                    unsigned x = std::min(w - 1, list[(base + 1 + i) * 2] + ox),
                                             y = std::min(h - 1, list[(base + 1 + i) * 2 + 1] + oy);
                                    // Base-level taps use the globally defined B-prime/opaque
                                    // sampling helper; its cache border has a separate GPU test.
                                    if (stage > 1)
                                    {
                                        // Fused H/V reads the preceding mip directly. Include
                                        // every horizontal-cache halo row, not just the rows
                                        // interpolated by this output pixel's vertical taps.
                                        const unsigned previous = stage - 2;
                                        const double ry = double(layout.height[previous]) / h;
                                        unsigned originY = list[(base + 1 + i) * 2 + 1];
                                        int first = int(std::floor((originY + .5) * ry - .5 - 2.708608527326045));
                                        int last = int(std::floor((std::min(originY + 3, h - 1) + .5) * ry - .5 +
                                                                  2.708608527326045)) +
                                                   1;
                                        for (double tap : {-.8906824581564393, .8906824581564393, -2.708608527326045,
                                                           2.708608527326045})
                                        {
                                            int px = int(std::floor((x + .5) * layout.width[previous] / w - .5 + tap));
                                            for (int py = first; py <= last; ++py)
                                                for (int sx = 0; sx < 2; ++sx)
                                                    ok &= contains(previous, px + sx, py);
                                        }
                                    }
                                }
                    }
                    // Every requested Gaussian output pixel exists. Sharp-only sources
                    // need no Gaussian producers, regardless of their displacement.
                    for (unsigned tile = 0; tile < 60; ++tile)
                        if (input[tile])
                        {
                            for (unsigned y = (tile / 10) * 64; y < (tile / 10 + 1) * 64; ++y)
                                for (unsigned x = (tile % 10) * 64; x < (tile % 10 + 1) * 64; ++x)
                                {
                                    if (input[tile] & 1u)
                                        for (unsigned stage = 1; stage <= maxMip * 2 + 1; stage += 2)
                                            ok &= contains(stage, int(double(x) * layout.width[stage] / Width),
                                                           int(double(y) * layout.height[stage] / Height));
                                }
                        }
                    if (fixture == 37)
                    {
                        unsigned base = layout.offset[1], n = list[base * 2];
                        ok &= n < layout.capacity[1] / 2;
                    }
                    if (!ok)
                    {
                        printf("Tile dependency FAIL: mips %u sparse %u fixture %u\n", count, sparse, fixture);
                        return false;
                    }
                    ++cases;
                    device->runGarbageCollection();
                }
    }
    printf("Frost masks: %u GPU coverage / halo / empty-gap contracts PASS\n", cases);
    return true;
}
