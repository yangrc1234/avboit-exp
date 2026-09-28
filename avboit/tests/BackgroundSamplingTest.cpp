// SPDX-License-Identifier: MIT
#include <donut/app/ApplicationBase.h>
#include <donut/core/vfs/VFS.h>
#include <donut/engine/ShaderFactory.h>
#include <nvrhi/utils.h>
#include <array>
#include <cstdio>

bool RunBackgroundSamplingTest(nvrhi::IDevice *device)
{
    using namespace donut;
    using B = nvrhi::BindingSetItem;
    constexpr unsigned Width = 644, Height = 360;
    auto fs = std::make_shared<vfs::NativeFileSystem>();
    engine::ShaderFactory factory(device, fs,
                                  app::GetDirectoryWithExecutable() / "shaders/avboit" /
                                      app::GetShaderTypeName(device->getGraphicsAPI()));
    auto init =
        factory.CreateShader("shaders/BackgroundSamplingTest.hlsl", "initialize", nullptr, nvrhi::ShaderType::Compute);
    auto compare =
        factory.CreateShader("shaders/BackgroundSamplingTest.hlsl", "compare", nullptr, nvrhi::ShaderType::Compute);
    if (!init || !compare)
        return false;
    auto texture = [&](unsigned w, unsigned h, nvrhi::Format format)
    {
        return device->createTexture(nvrhi::TextureDesc()
                                         .setWidth(w)
                                         .setHeight(h)
                                         .setFormat(format)
                                         .setIsUAV(true)
                                         .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                         .setKeepInitialState(true));
    };
    auto opaque = texture(Width, Height, nvrhi::Format::RGBA16_FLOAT),
         reference = texture(Width, Height, nvrhi::Format::RGBA16_FLOAT);
    auto cache = texture(Width, Height, nvrhi::Format::RGBA16_FLOAT),
         tiles = texture((Width + 63) / 64, (Height + 63) / 64, nvrhi::Format::R32_UINT);
    auto camera = device->createBuffer(nvrhi::BufferDesc()
                                           .setByteSize(7 * 16)
                                           .setFormat(nvrhi::Format::RGBA32_FLOAT)
                                           .setCanHaveTypedViews(true)
                                           .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                           .setKeepInitialState(true));
    auto failures = device->createBuffer(nvrhi::BufferDesc()
                                             .setByteSize(4)
                                             .setFormat(nvrhi::Format::R32_UINT)
                                             .setCanHaveTypedViews(true)
                                             .setCanHaveUAVs(true)
                                             .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
                                             .setKeepInitialState(true));
    auto readback = device->createBuffer(nvrhi::BufferDesc()
                                             .setByteSize(4)
                                             .setCpuAccess(nvrhi::CpuAccessMode::Read)
                                             .setInitialState(nvrhi::ResourceStates::CopyDest)
                                             .setKeepInitialState(true));
    auto linear = device->createSampler(
        nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
    auto initDesc = nvrhi::BindingSetDesc()
                        .addItem(B::TypedBuffer_SRV(15, camera))
                        .addItem(B::Texture_UAV(0, opaque))
                        .addItem(B::Texture_UAV(1, reference))
                        .addItem(B::Texture_UAV(2, cache))
                        .addItem(B::Texture_UAV(3, tiles));
    auto compareDesc = nvrhi::BindingSetDesc()
                           .addItem(B::TypedBuffer_SRV(15, camera))
                           .addItem(B::Texture_SRV(0, reference))
                           .addItem(B::Texture_SRV(3, opaque))
                           .addItem(B::Texture_SRV(27, cache))
                           .addItem(B::Texture_SRV(30, tiles))
                           .addItem(B::TypedBuffer_UAV(4, failures))
                           .addItem(B::Sampler(0, linear));
    nvrhi::BindingLayoutHandle initLayout, compareLayout;
    nvrhi::BindingSetHandle initSet, compareSet;
    if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::Compute, 0, initDesc, initLayout,
                                                 initSet) ||
        !nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::Compute, 0, compareDesc, compareLayout,
                                                 compareSet))
        return false;
    auto initPipeline =
        device->createComputePipeline(nvrhi::ComputePipelineDesc().setComputeShader(init).addBindingLayout(initLayout));
    auto comparePipeline = device->createComputePipeline(
        nvrhi::ComputePipelineDesc().setComputeShader(compare).addBindingLayout(compareLayout));
    std::array<float, 28> cam{};
    cam[24] = Width;
    cam[25] = Height;
    auto commands = device->createCommandList();
    commands->open();
    commands->writeBuffer(camera, cam.data(), sizeof(cam));
    commands->clearBufferUInt(failures, 0);
    commands->setComputeState(nvrhi::ComputeState().setPipeline(initPipeline).addBindingSet(initSet));
    commands->dispatch((Width + 7) / 8, (Height + 7) / 8);
    commands->setComputeState(nvrhi::ComputeState().setPipeline(comparePipeline).addBindingSet(compareSet));
    commands->dispatch((Width + 7) / 8, (Height + 7) / 8);
    commands->copyBuffer(readback, 0, failures, 0, 4);
    commands->close();
    device->executeCommandList(commands);
    device->waitForIdle();
    const auto result = static_cast<const unsigned *>(device->mapBuffer(readback, nvrhi::CpuAccessMode::Read));
    if (!result)
        return false;
    const unsigned count = *result;
    device->unmapBuffer(readback);
    printf("B prime: %u HDR/validity/border/off-screen samples, %u mismatches\n", Width * Height * 16, count);
    return count == 0;
}
