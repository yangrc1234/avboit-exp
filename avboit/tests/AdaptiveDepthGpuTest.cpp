// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <donut/app/DeviceManager.h>
#include <donut/app/ApplicationBase.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/core/log.h>
#include <donut/core/vfs/VFS.h>
#include <nvrhi/utils.h>

using namespace donut;

#include "tests/AdaptiveDepthReference.h"
#include <random>

bool RunBlurDispatchTest(nvrhi::IDevice *device);
bool RunBackgroundSamplingTest(nvrhi::IDevice *device);
bool RunBoundsTest(nvrhi::IDevice *device);
bool RunExtinctionTest(nvrhi::IDevice *device);

bool RunBudgetTest(nvrhi::IDevice *device, unsigned budget, bool fixed = false)
{
    auto nativeFS = std::make_shared<vfs::NativeFileSystem>();
    engine::ShaderFactory shaders(device, nativeFS,
                                  app::GetDirectoryWithExecutable() / "shaders/avboit" /
                                      app::GetShaderTypeName(device->getGraphicsAPI()));
    const std::string entry = budget == 128 ? "main" : "budget" + std::to_string(budget);
    auto shader = shaders.CreateShader(fixed ? "shaders/FixedDepth.hlsl" : "shaders/AdaptiveDepth.hlsl", entry.c_str(),
                                       nullptr, nvrhi::ShaderType::Compute);
    auto occupancyShader =
        shaders.CreateShader("shaders/DepthOccupancy.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    if (!shader || !occupancyShader)
        return false;
    const uint32_t outputWords = 2 + 2 * (avboit::VirtualSlices + 1);
    auto input = device->createBuffer(nvrhi::BufferDesc()
                                          .setByteSize(avboit::VirtualSlices * 4)
                                          .setCanHaveTypedViews(true)
                                          .setCanHaveUAVs(true)
                                          .setFormat(nvrhi::Format::R32_UINT)
                                          .setDebugName("VirtualDepthOccupancy")
                                          .setInitialState(nvrhi::ResourceStates::CopyDest)
                                          .setKeepInitialState(true));
    auto outputDesc = nvrhi::BufferDesc()
                          .setByteSize(outputWords * 4)
                          .setCanHaveTypedViews(true)
                          .setCanHaveUAVs(true)
                          .setFormat(nvrhi::Format::R32_UINT)
                          .setDebugName("AdaptiveDepthWarp")
                          .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
                          .setKeepInitialState(true);
    auto output = device->createBuffer(outputDesc);
    auto readback = device->createBuffer(nvrhi::BufferDesc()
                                             .setByteSize(outputWords * 4)
                                             .setCpuAccess(nvrhi::CpuAccessMode::Read)
                                             .setInitialState(nvrhi::ResourceStates::CopyDest)
                                             .setKeepInitialState(true));
    auto bindings = nvrhi::BindingSetDesc()
                        .addItem(nvrhi::BindingSetItem::TypedBuffer_SRV(0, input))
                        .addItem(nvrhi::BindingSetItem::TypedBuffer_UAV(0, output));
    nvrhi::BindingSetHandle set;
    nvrhi::BindingLayoutHandle layout;
    if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::Compute, 0, bindings, layout, set))
        return false;
    auto pipeline =
        device->createComputePipeline(nvrhi::ComputePipelineDesc().setComputeShader(shader).addBindingLayout(layout));
    auto rangesBuffer = device->createBuffer(nvrhi::BufferDesc()
                                                 .setByteSize(4096 * 4)
                                                 .setCanHaveTypedViews(true)
                                                 .setFormat(nvrhi::Format::R32_UINT)
                                                 .setDebugName("ConservativeDepthRanges")
                                                 .setInitialState(nvrhi::ResourceStates::CopyDest)
                                                 .setKeepInitialState(true));
    auto occupancyBindings = nvrhi::BindingSetDesc()
                                 .addItem(nvrhi::BindingSetItem::TypedBuffer_SRV(0, rangesBuffer))
                                 .addItem(nvrhi::BindingSetItem::TypedBuffer_UAV(0, input));
    nvrhi::BindingSetHandle occupancySet;
    nvrhi::BindingLayoutHandle occupancyLayout;
    if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::Compute, 0, occupancyBindings,
                                                 occupancyLayout, occupancySet))
        return false;
    auto occupancyPipeline = device->createComputePipeline(
        nvrhi::ComputePipelineDesc().setComputeShader(occupancyShader).addBindingLayout(occupancyLayout));
    auto commands = device->createCommandList();
    std::mt19937 random(20260926);
    for (unsigned test = 0; test < 40; ++test)
    {
        std::vector<uint32_t> occupancy(avboit::VirtualSlices, 0);
        if (test == 1)
            occupancy[0] = 1;
        else if (test == 2)
            occupancy.back() = 1;
        else if (test == 3)
            std::fill(occupancy.begin(), occupancy.end(), 1);
        else if (test > 3)
            for (unsigned i = 0; i < (test - 3) * 7; ++i)
                occupancy[random() % occupancy.size()] = 1;
        std::vector<uint32_t> ranges;
        for (uint32_t slice = 0; slice < occupancy.size(); ++slice)
            if (occupancy[slice])
            {
                ranges.push_back(slice);
                ranges.push_back(slice);
            }
        auto expected = avboit::BuildDepthWarp(occupancy, budget);
        if (fixed)
        {
            std::fill(expected.begin(), expected.end(), 0);
            expected[0] = expected[1] = budget - 1;
            for (unsigned i = 0; i <= avboit::VirtualSlices; ++i)
            {
                expected[2 + 2 * i] = std::min(i, budget - 1);
                if (i < budget - 1)
                    expected[3 + 2 * i] = 1 | (i == 0 ? 2 : 0) | (i == budget - 2 ? 4 : 0);
            }
        }
        commands->open();
        commands->clearBufferUInt(input, 0);
        if (!ranges.empty())
        {
            commands->writeBuffer(rangesBuffer, ranges.data(), ranges.size() * 4);
            commands->setComputeState(nvrhi::ComputeState().setPipeline(occupancyPipeline).addBindingSet(occupancySet));
            commands->dispatch(uint32_t(ranges.size() / 2), 1, 1);
        }
        commands->setComputeState(nvrhi::ComputeState().setPipeline(pipeline).addBindingSet(set));
        commands->dispatch(1, 1, 1);
        commands->copyBuffer(readback, 0, output, 0, outputWords * 4);
        commands->close();
        device->executeCommandList(commands);
        device->waitForIdle();
        auto actual = static_cast<const uint32_t *>(device->mapBuffer(readback, nvrhi::CpuAccessMode::Read));
        bool match = actual && std::equal(expected.begin(), expected.end(), actual);
        if (actual)
            device->unmapBuffer(readback);
        if (!match)
        {
            printf("Adaptive Z case %u FAILED\n", test);
            return false;
        }
        printf("Adaptive Z case %u: virtual=%u occupied=%u PASS\n", test, expected[0], expected[1]);
        device->runGarbageCollection();
    }
    return true;
}

bool RunTest(nvrhi::IDevice *device)
{
    for (bool fixed : {false, true})
        for (unsigned budget : {16u, 32u, 64u, 128u})
            if (!RunBudgetTest(device, budget, fixed))
                return false;
    return true;
}

int main(int argc, const char **argv)
{
    log::ConsoleApplicationMode();
#ifndef _DEBUG
    log::SetMinSeverity(log::Severity::Warning);
#endif

    nvrhi::GraphicsAPI api = app::GetGraphicsAPIFromCommandLine(argc, argv);
    std::unique_ptr<app::DeviceManager> deviceManager =
        std::unique_ptr<app::DeviceManager>(app::DeviceManager::Create(api));

    app::DeviceCreationParameters deviceParams;
#ifdef _DEBUG
    deviceParams.enableDebugRuntime = true;
    deviceParams.enableNvrhiValidationLayer = true;
#endif

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--help") == 0)
        {
            printf("Usage: %s [options]\n"
                   " -dx11            Use DX11 API\n"
                   " -dx12            Use DX12 API (default)\n"
                   " -vk              Use Vulkan API\n"
                   " --list-adapters  Enumerate the graphics adapters present in the system\n"
                   " --adapter <n>    Use graphics adapter with index <n> as reported by --list-adapters\n",
                   argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--list-adapters") == 0)
        {
            if (!deviceManager->CreateInstance(deviceParams))
            {
                log::error("Cannot initialize a %s subsystem.", nvrhi::utils::GraphicsAPIToString(api));
                return 1;
            }

            std::vector<app::AdapterInfo> adapters;
            if (!deviceManager->EnumerateAdapters(adapters))
            {
                log::error("Cannot enumerate graphics adapters.");
                return 1;
            }

            for (int adapterIndex = 0; adapterIndex < int(adapters.size()); ++adapterIndex)
            {
                auto const &info = adapters[adapterIndex];
                int deviceMemoryMB = int(info.dedicatedVideoMemory / (1024 * 1024));
                printf("Adapter %d: %s (%d MB VRAM)\n", adapterIndex, info.name.c_str(), deviceMemoryMB);
            }
            return 0;
        }
        else if (strcmp(argv[i], "--adapter") == 0)
        {
            if (i + 1 >= argc)
            {
                log::error("--device requires a parameter");
                return 1;
            }
            deviceParams.adapterIndex = atoi(argv[i + 1]);
            ++i;
        }
    }

    if (!deviceManager->CreateHeadlessDevice(deviceParams))
    {
        log::error("Cannot initialize a graphics device with the requested parameters");
        return 1;
    }

    printf("Using %s API with %s.\n", nvrhi::utils::GraphicsAPIToString(api), deviceManager->GetRendererString());

    if (!RunTest(deviceManager->GetDevice()) || !RunBoundsTest(deviceManager->GetDevice()) ||
        !RunExtinctionTest(deviceManager->GetDevice()) || !RunBlurDispatchTest(deviceManager->GetDevice()) ||
        !RunBackgroundSamplingTest(deviceManager->GetDevice()))
        return 1;

    deviceManager->Shutdown();

    return 0;
}
