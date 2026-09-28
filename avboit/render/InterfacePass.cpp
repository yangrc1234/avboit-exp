// SPDX-License-Identifier: MIT
#include "ResourceReuse.h"
#include "InterfacePass.h"
#include "GpuScope.h"
#include <cstdio>
#include <nvrhi/utils.h>
#include <stdexcept>

namespace avboit
{
void InterfacePass::Create(nvrhi::IDevice *device, const InterfaceInputs &inputs)
{
    auto texture = [&](const nvrhi::TextureHandle &current, nvrhi::Format format, const char *name)
    {
        return ReuseTexture(device, current,
                            nvrhi::TextureDesc()
                                .setWidth(inputs.width)
                                .setHeight(inputs.height)
                                .setFormat(format)
                                .setIsRenderTarget(true)
                                .setDebugName(name)
                                .setKeepInitialState(true)
                                .setInitialState(format == nvrhi::Format::D32 ? nvrhi::ResourceStates::DepthWrite
                                                                              : nvrhi::ResourceStates::RenderTarget));
    };
    m_transmissionSigma = texture(m_transmissionSigma, nvrhi::Format::RGBA16_FLOAT, "Interface transmission and sigma");
    m_offset = texture(m_offset, nvrhi::Format::RG16_FLOAT, "Interface offset");
    m_hardwareDepth = texture(m_hardwareDepth, nvrhi::Format::D32, "Interface depth test");
    m_framebuffer = device->createFramebuffer(nvrhi::FramebufferDesc()
                                                  .addColorAttachment(m_transmissionSigma)
                                                  .addColorAttachment(m_offset)
                                                  .setDepthAttachment(m_hardwareDepth));

    using B = nvrhi::BindingSetItem;
    auto bindings = nvrhi::BindingSetDesc()
                        .addItem(B::TypedBuffer_SRV(0, inputs.vertices, nvrhi::Format::UNKNOWN, inputs.vertexRange))
                        .addItem(B::Texture_SRV(3, inputs.opaqueColorDepth))
                        .addItem(B::Texture_SRV(24, inputs.opaqueHardwareDepth))
                        .addItem(B::TypedBuffer_SRV(15, inputs.camera))
                        .addItem(B::TypedBuffer_SRV(23, inputs.materials));
    m_layout = nullptr;
    m_bindings = nullptr;
    if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::AllGraphics, 0, bindings, m_layout,
                                                 m_bindings))
        throw std::runtime_error("Interface bindings");

    nvrhi::GraphicsPipelineDesc pipeline;
    pipeline.bindingLayouts = {m_layout};
    pipeline.renderState.depthStencilState.depthTestEnable = true;
    pipeline.renderState.depthStencilState.depthWriteEnable = true;
    m_pipelines.clear();
    for (const auto &material : inputs.materialShaders)
    {
        pipeline.VS = material.vertex;
        pipeline.PS = material.interfacePixel;
        pipeline.renderState.rasterState.cullMode = material.cullMode;
        m_pipelines.push_back(device->createGraphicsPipeline(pipeline, m_framebuffer->getFramebufferInfo()));
    }
    m_indices = inputs.indices;
}

void InterfacePass::Record(nvrhi::ICommandList *commands, const std::vector<GeometryDraw> &draws) const
{
    for (const auto &texture : {m_transmissionSigma, m_offset})
        commands->clearTextureFloat(texture, nvrhi::AllSubresources, nvrhi::Color(0));
    commands->clearDepthStencilTexture(m_hardwareDepth, nvrhi::AllSubresources, true, 1, false, 0);
    nvrhi::GraphicsState state;
    state.framebuffer = m_framebuffer;
    state.bindings = {m_bindings};
    state.viewport.addViewportAndScissorRect(m_framebuffer->getFramebufferInfo().getViewport());
    state.indexBuffer = nvrhi::IndexBufferBinding().setBuffer(m_indices).setFormat(nvrhi::Format::R32_UINT);
    for (const auto &draw : draws)
    {
        char label[96];
        snprintf(label, sizeof(label), "Interface actor %u / material %u", draw.actor, draw.material);
        AVBOIT_GPU_SCOPE(commands, label);
        state.pipeline = m_pipelines.at(draw.material);
        commands->setGraphicsState(state);
        commands->drawIndexed(
            nvrhi::DrawArguments().setStartIndexLocation(draw.firstIndex).setVertexCount(draw.indexCount));
    }
}
} // namespace avboit
