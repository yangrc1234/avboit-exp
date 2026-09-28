// SPDX-License-Identifier: MIT
#include "ZeroTransmittance.h"
#include <nvrhi/utils.h>
#include <stdexcept>
namespace avboit
{
void ZeroTransmittance::Create(nvrhi::IDevice *device, const ZeroTransmittanceInputs &i)
{
    m_tileCount = ((i.width + 15) / 16) * ((i.height + 15) / 16);
    m_depth = i.opaqueDepth;
    using B = nvrhi::BindingSetItem;
    auto vs = nvrhi::BindingSetDesc()
                  .addItem(B::TypedBuffer_SRV(15, i.camera))
                  .addItem(B::TypedBuffer_SRV(1, i.warp))
                  .addItem(B::Texture_SRV(2, i.zeroSlice));
    m_drawLayout = nullptr;
    m_drawBindings = nullptr;
    if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::Vertex, 0, vs, m_drawLayout,
                                                 m_drawBindings))
        throw std::runtime_error("Zero VS bindings");
    m_framebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().setDepthAttachment(m_depth));
    nvrhi::GraphicsPipelineDesc pipeline;
    pipeline.VS = i.vertex;
    pipeline.bindingLayouts = {m_drawLayout};
    pipeline.primType = nvrhi::PrimitiveType::TriangleStrip;
    pipeline.renderState.depthStencilState.depthTestEnable = true;
    pipeline.renderState.depthStencilState.depthWriteEnable = true;
    pipeline.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::LessOrEqual;
    pipeline.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    m_draw = device->createGraphicsPipeline(pipeline, m_framebuffer->getFramebufferInfo());
}
void ZeroTransmittance::RecordDepth(nvrhi::ICommandList *cmd, bool enabled) const
{
    if (!enabled)
        return;
    nvrhi::GraphicsState state;
    state.pipeline = m_draw;
    state.framebuffer = m_framebuffer;
    state.bindings = {m_drawBindings};
    state.viewport.addViewportAndScissorRect(m_framebuffer->getFramebufferInfo().getViewport());
    cmd->setGraphicsState(state);
    cmd->draw(nvrhi::DrawArguments().setVertexCount(4).setInstanceCount(m_tileCount));
}
} // namespace avboit
