// SPDX-License-Identifier: MIT
#include "ResourceReuse.h"
#include "ExtinctionVolume.h"
#include "GpuScope.h"
#include <cstdio>
#include <nvrhi/utils.h>
#include <stdexcept>

namespace avboit
{
void ExtinctionVolume::Create(nvrhi::IDevice *device, const ExtinctionInputs &inputs)
{
    if (!inputs.width || !inputs.height)
        throw std::invalid_argument("Extinction dimensions must be positive");
    m_depthBudget = inputs.depthBudget;
    m_width = inputs.width;
    m_height = inputs.height;
    m_tiles = ((m_width + 7) / 8) * ((m_height + 7) / 8);
    m_objects = inputs.objectCount;
    m_indices = inputs.indices;
    const unsigned rays = m_width * m_height;
    auto buffer = [&](const nvrhi::BufferHandle &current, unsigned words, const char *name)
    {
        return ReuseBuffer(device, current,
                           nvrhi::BufferDesc()
                               .setByteSize(words * 4)
                               .setCanHaveTypedViews(true)
                               .setCanHaveUAVs(true)
                               .setFormat(nvrhi::Format::R32_UINT)
                               .setDebugName(name)
                               .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
                               .setKeepInitialState(true));
    };
    m_masks = buffer(m_masks, 2048 + m_tiles * 128, "Virtual extinction occupancy");
    m_screenTiles = ReuseTexture(device, m_screenTiles,
                                 nvrhi::TextureDesc()
                                     .setWidth((inputs.screenWidth + 63) / 64)
                                     .setHeight((inputs.screenHeight + 63) / 64)
                                     .setFormat(nvrhi::Format::R32_UINT)
                                     .setIsUAV(true)
                                     .setDebugName("2D transparency / frost tile requests")
                                     .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
                                     .setKeepInitialState(true));
    m_warp = buffer(m_warp, 2 + 2 * 2049, "Adaptive depth warp");
    m_physicalMasks = buffer(m_physicalMasks, m_tiles * 8, "Physical scalar/RGB occupancy");
    m_extinction = buffer(m_extinction, rays * (m_depthBudget / 4 + m_depthBudget), "Packed extinction");
    m_overflow = buffer(m_overflow, rays * 4, "First overflow depth");
    m_lut = ReuseTexture(device, m_lut,
                         nvrhi::TextureDesc()
                             .setWidth(m_width)
                             .setHeight(m_height)
                             .setDepth(m_depthBudget)
                             .setDimension(nvrhi::TextureDimension::Texture3D)
                             .setFormat(nvrhi::Format::RGBA8_UNORM)
                             .setIsUAV(true)
                             .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
                             .setKeepInitialState(true)
                             .setDebugName("Transmittance LUT"));
    m_zeroSlice = ReuseTexture(device, m_zeroSlice,
                               nvrhi::TextureDesc()
                                   .setWidth(m_width)
                                   .setHeight(m_height)
                                   .setFormat(nvrhi::Format::R32_UINT)
                                   .setIsUAV(true)
                                   .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
                                   .setKeepInitialState(true)
                                   .setDebugName("First zero-transmittance slice"));
    m_rasterTarget = ReuseTexture(device, m_rasterTarget,
                                  nvrhi::TextureDesc()
                                      .setWidth(m_width)
                                      .setHeight(m_height)
                                      .setFormat(nvrhi::Format::R8_UNORM)
                                      .setIsRenderTarget(true)
                                      .setDebugName("Splat raster dimensions")
                                      .setInitialState(nvrhi::ResourceStates::RenderTarget)
                                      .setKeepInitialState(true));
    m_framebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_rasterTarget));

    using B = nvrhi::BindingSetItem;
    using D = nvrhi::BindingSetDesc;
    auto compute = [&](const D &bindings, nvrhi::IShader *shader)
    {
        ComputePass pass;
        if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::Compute, 0, bindings, pass.layout,
                                                     pass.bindings))
            throw std::runtime_error("Extinction compute bindings");
        pass.pipeline = device->createComputePipeline(
            nvrhi::ComputePipelineDesc().setComputeShader(shader).addBindingLayout(pass.layout));
        return pass;
    };
    m_boundsPass = compute(D().addItem(B::TypedBuffer_SRV(0, inputs.bounds))
                               .addItem(B::TypedBuffer_SRV(15, inputs.camera))
                               .addItem(B::TypedBuffer_SRV(16, inputs.frostFootprints))
                               .addItem(B::TypedBuffer_UAV(2, inputs.frostWorkMask))
                               .addItem(B::TypedBuffer_UAV(5, inputs.frostRectangles, nvrhi::Format::R32_UINT))
                               .addItem(B::TypedBuffer_UAV(0, m_masks))
                               .addItem(B::Texture_UAV(1, m_screenTiles)),
                           inputs.shaders.bounds);
    m_warpPass = compute(D().addItem(B::TypedBuffer_SRV(0, m_masks)).addItem(B::TypedBuffer_UAV(0, m_warp)),
                         inputs.shaders.adaptiveDepth);
    m_physicalPass = compute(D().addItem(B::TypedBuffer_SRV(0, m_masks))
                                 .addItem(B::TypedBuffer_SRV(1, m_warp))
                                 .addItem(B::TypedBuffer_SRV(2, inputs.bounds))
                                 .addItem(B::TypedBuffer_UAV(0, m_physicalMasks)),
                             inputs.shaders.physicalMask);
    auto integration = D().addItem(B::TypedBuffer_SRV(15, inputs.camera))
                           .addItem(B::TypedBuffer_SRV(1, m_warp))
                           .addItem(B::TypedBuffer_SRV(22, m_physicalMasks))
                           .addItem(B::TypedBuffer_UAV(0, m_extinction))
                           .addItem(B::TypedBuffer_UAV(1, m_overflow))
                           .addItem(B::Texture_UAV(2, m_lut))
                           .addItem(B::Texture_UAV(3, m_zeroSlice));
    m_integratePass = compute(integration, inputs.shaders.integrate);
    m_densePass = compute(integration, inputs.shaders.integrateDense);
    m_clearPass = compute(integration, inputs.shaders.clear);

    auto splat = D().addItem(B::TypedBuffer_SRV(0, inputs.vertices, nvrhi::Format::UNKNOWN, inputs.vertexRange))
                     .addItem(B::TypedBuffer_SRV(1, m_warp))
                     .addItem(B::Texture_SRV(3, inputs.opaqueColorDepth))
                     .addItem(B::TypedBuffer_UAV(0, m_extinction))
                     .addItem(B::TypedBuffer_UAV(1, m_overflow))
                     .addItem(B::Texture_SRV(24, inputs.opaqueHardwareDepth))
                     .addItem(B::TypedBuffer_SRV(15, inputs.camera))
                     .addItem(B::TypedBuffer_SRV(23, inputs.materials));
    m_splatLayout = nullptr;
    m_splatBindings = nullptr;
    if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::AllGraphics, 0, splat, m_splatLayout,
                                                 m_splatBindings))
        throw std::runtime_error("Extinction splat bindings");
    nvrhi::GraphicsPipelineDesc pipeline;
    pipeline.bindingLayouts = {m_splatLayout};
    pipeline.renderState.depthStencilState.depthTestEnable = false;
    pipeline.renderState.depthStencilState.depthWriteEnable = false;
    m_splatPipelines.clear();
    for (const auto &material : inputs.materialShaders)
    {
        pipeline.VS = material.vertex;
        pipeline.PS = material.extinctionPixel;
        pipeline.renderState.rasterState.cullMode = material.cullMode;
        m_splatPipelines.push_back(device->createGraphicsPipeline(pipeline, m_framebuffer->getFramebufferInfo()));
    }
}

void ExtinctionVolume::ComputePass::Record(nvrhi::ICommandList *commands, unsigned x, unsigned y) const
{
    commands->setComputeState(nvrhi::ComputeState().setPipeline(pipeline).addBindingSet(bindings));
    commands->dispatch(x, y, 1);
}
void ExtinctionVolume::Barrier(nvrhi::ICommandList *commands) const
{
    // Same-state UAV barriers are required between clear, atomic splat and integration.
    commands->setBufferState(m_extinction, nvrhi::ResourceStates::UnorderedAccess);
    commands->setBufferState(m_overflow, nvrhi::ResourceStates::UnorderedAccess);
    commands->commitBarriers();
}
void ExtinctionVolume::RecordBounds(nvrhi::ICommandList *commands) const
{
    commands->clearBufferUInt(m_masks, 0);
    commands->clearTextureUInt(m_screenTiles, nvrhi::AllSubresources, 0);
    if (m_objects)
        m_boundsPass.Record(commands, m_objects);
}
void ExtinctionVolume::RecordAdaptiveDepth(nvrhi::ICommandList *commands) const
{
    m_warpPass.Record(commands, 1);
}
void ExtinctionVolume::RecordPhysicalMask(nvrhi::ICommandList *commands) const
{
    m_physicalPass.Record(commands, m_tiles);
}
void ExtinctionVolume::RecordClear(nvrhi::ICommandList *commands, bool dense, bool poison) const
{
    if (dense)
    {
        commands->clearBufferUInt(m_extinction, 0);
        commands->clearBufferUInt(m_overflow, m_depthBudget);
    }
    else
    {
        if (poison)
        {
            commands->clearBufferUInt(m_extinction, 0xdeadbeefu);
            Barrier(commands);
        }
        m_clearPass.Record(commands, (m_width + 7) / 8, (m_height + 7) / 8);
    }
    Barrier(commands);
}
void ExtinctionVolume::RecordSplat(nvrhi::ICommandList *commands, const std::vector<GeometryDraw> &draws) const
{
    nvrhi::GraphicsState state;
    state.framebuffer = m_framebuffer;
    state.bindings = {m_splatBindings};
    state.viewport.addViewportAndScissorRect(m_framebuffer->getFramebufferInfo().getViewport());
    state.indexBuffer = nvrhi::IndexBufferBinding().setBuffer(m_indices).setFormat(nvrhi::Format::R32_UINT);
    for (const auto &draw : draws)
    {
        char label[96];
        snprintf(label, sizeof(label), "Extinction actor %u / material %u", draw.actor, draw.material);
        AVBOIT_GPU_SCOPE(commands, label);
        state.pipeline = m_splatPipelines.at(draw.material);
        commands->setGraphicsState(state);
        commands->drawIndexed(
            nvrhi::DrawArguments().setStartIndexLocation(draw.firstIndex).setVertexCount(draw.indexCount));
    }
    Barrier(commands);
}
void ExtinctionVolume::RecordIntegration(nvrhi::ICommandList *commands, bool dense) const
{
    (dense ? m_densePass : m_integratePass).Record(commands, (m_width + 7) / 8, (m_height + 7) / 8);
}
} // namespace avboit
