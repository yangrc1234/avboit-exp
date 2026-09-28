// SPDX-License-Identifier: MIT
#include "DistortionComposite.h"
#include "ResourceReuse.h"
#include <nvrhi/utils.h>
#include <stdexcept>

namespace avboit
{
void DistortionComposite::Create(nvrhi::IDevice *device, const DistortionCompositeInputs &inputs)
{
    m_source = inputs.sceneColor;
    m_framebuffer = nullptr;
    m_pipeline = nullptr;
    m_layout = nullptr;
    m_bindings = nullptr;
    if (!inputs.enabled)
    {
        m_output = nullptr;
        return;
    }
    const auto &source = inputs.sceneColor->getDesc();
    m_output = ReuseTexture(device, m_output,
                            nvrhi::TextureDesc()
                                .setWidth(source.width)
                                .setHeight(source.height)
                                .setFormat(source.format)
                                .setIsRenderTarget(true)
                                .setDebugName("SceneColor after VFX distortion")
                                .setInitialState(nvrhi::ResourceStates::RenderTarget)
                                .setKeepInitialState(true));
    m_framebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_output));
    using B = nvrhi::BindingSetItem;
    auto desc = nvrhi::BindingSetDesc()
                    .addItem(B::Texture_SRV(20, inputs.offset))
                    .addItem(B::Texture_SRV(21, inputs.sceneColor))
                    .addItem(B::TypedBuffer_SRV(15, inputs.camera))
                    .addItem(B::Sampler(0, inputs.linear));
    if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::AllGraphics, 0, desc, m_layout, m_bindings))
        throw std::runtime_error("Distortion composition bindings");
    nvrhi::GraphicsPipelineDesc pipeline;
    pipeline.VS = inputs.vertex;
    pipeline.PS = inputs.pixel;
    pipeline.bindingLayouts = {m_layout};
    pipeline.renderState.depthStencilState.depthTestEnable = false;
    pipeline.renderState.depthStencilState.depthWriteEnable = false;
    pipeline.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    m_pipeline = device->createGraphicsPipeline(pipeline, m_framebuffer->getFramebufferInfo());
}
void DistortionComposite::Record(nvrhi::ICommandList *commands) const
{
    if (!m_output)
        return;
    nvrhi::GraphicsState state;
    state.pipeline = m_pipeline;
    state.framebuffer = m_framebuffer;
    state.bindings = {m_bindings};
    state.viewport.addViewportAndScissorRect(m_framebuffer->getFramebufferInfo().getViewport());
    commands->setGraphicsState(state);
    commands->draw(nvrhi::DrawArguments().setVertexCount(3));
}
} // namespace avboit
