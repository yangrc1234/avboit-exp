// SPDX-License-Identifier: MIT
#include "ResourceReuse.h"
#include "FrostComposition.h"
#include "GpuScope.h"
#include <nvrhi/utils.h>
#include <stdexcept>

namespace avboit
{
void FrostComposition::Create(nvrhi::IDevice *device, const FrostCompositionInputs &inputs)
{
    // Keep opaque immutable until every spatial background lookup is complete.
    m_output = inputs.sceneColor;
    m_tileCount = ((inputs.width + 63) / 64) * ((inputs.height + 63) / 64);
    m_temporary = ReuseTexture(device, m_temporary,
                               nvrhi::TextureDesc()
                                   .setWidth(inputs.width)
                                   .setHeight(inputs.height)
                                   .setFormat(inputs.sceneColor->getDesc().format)
                                   .setIsRenderTarget(true)
                                   .setDebugName("Resolved transparency tile color")
                                   .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                   .setKeepInitialState(true));
    m_framebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_temporary));
    using B = nvrhi::BindingSetItem;
    auto desc = nvrhi::BindingSetDesc()
                    .addItem(B::Texture_SRV(3, inputs.sceneColor))
                    .addItem(B::Texture_SRV(30, inputs.screenTiles))
                    .addItem(B::Texture_SRV(7, inputs.interfaceDepth))
                    .addItem(B::Texture_SRV(8, inputs.interfaceSurface))
                    .addItem(B::Texture_SRV(13, inputs.backgroundPyramid))
                    .addItem(B::Texture_SRV(27, inputs.sharpBackground))
                    .addItem(B::Texture_SRV(14, inputs.interfaceOffset))
                    .addItem(B::Sampler(0, inputs.linear))
                    .addItem(B::TypedBuffer_SRV(15, inputs.camera))
                    .addItem(B::TypedBuffer_SRV(23, inputs.materials));
    desc.addItem(B::TypedBuffer_SRV(1, inputs.warp))
        .addItem(B::Texture_SRV(2, inputs.lut))
        .addItem(B::Texture_SRV(4, inputs.numerator))
        .addItem(B::Texture_SRV(5, inputs.denominator))
        .addItem(B::Texture_SRV(6, inputs.totalTau))
        .addItem(B::Texture_SRV(9, inputs.backNumerator))
        .addItem(B::Texture_SRV(24, inputs.opaqueDepth));
    m_layout = nullptr;
    m_bindings = nullptr;
    if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::AllGraphics, 0, desc, m_layout, m_bindings))
        throw std::runtime_error("Frost composition bindings");
    nvrhi::GraphicsPipelineDesc pipeline;
    pipeline.VS = inputs.vertexShader;
    pipeline.PS = inputs.pixelShader;
    pipeline.bindingLayouts = {m_layout};
    pipeline.renderState.depthStencilState.depthTestEnable = false;
    pipeline.renderState.depthStencilState.depthWriteEnable = false;
    pipeline.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    m_pipeline = device->createGraphicsPipeline(pipeline, m_framebuffer->getFramebufferInfo());
    auto copyDesc = nvrhi::BindingSetDesc()
                        .addItem(B::Texture_SRV(10, m_temporary))
                        .addItem(B::Texture_SRV(30, inputs.screenTiles))
                        .addItem(B::TypedBuffer_SRV(15, inputs.camera));
    m_copyLayout = nullptr;
    m_copyBindings = nullptr;
    if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::AllGraphics, 0, copyDesc, m_copyLayout,
                                                 m_copyBindings))
        throw std::runtime_error("Transparency tile copy bindings");
    m_copyFramebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_output));
    pipeline.PS = inputs.copyShader;
    pipeline.bindingLayouts = {m_copyLayout};
    // The legacy scene stores view depth in alpha. Copy RGB only, without blending.
    pipeline.renderState.blendState.targets[0].setColorWriteMask(nvrhi::ColorMask::Red | nvrhi::ColorMask::Green |
                                                                 nvrhi::ColorMask::Blue);
    m_copyPipeline = device->createGraphicsPipeline(pipeline, m_copyFramebuffer->getFramebufferInfo());
}
void FrostComposition::Record(nvrhi::ICommandList *commands, bool poison) const
{
    if (poison)
        commands->clearTextureFloat(m_temporary, nvrhi::AllSubresources, nvrhi::Color(60000, 0, 60000, 1));
    auto draw = [&](nvrhi::IGraphicsPipeline *pipeline, nvrhi::IFramebuffer *framebuffer, nvrhi::IBindingSet *bindings)
    {
        nvrhi::GraphicsState state;
        state.pipeline = pipeline;
        state.framebuffer = framebuffer;
        state.bindings = {bindings};
        state.viewport.addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());
        commands->setGraphicsState(state);
        commands->draw(nvrhi::DrawArguments().setVertexCount(6).setInstanceCount(m_tileCount));
    };
    {
        AVBOIT_GPU_SCOPE(commands, "Resolve into temporary tile color");
        draw(m_pipeline, m_framebuffer, m_bindings);
    }
    {
        AVBOIT_GPU_SCOPE(commands, "Copy transparency tiles to SceneColor");
        draw(m_copyPipeline, m_copyFramebuffer, m_copyBindings);
    }
}
} // namespace avboit
