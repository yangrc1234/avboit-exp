// SPDX-License-Identifier: MIT
#include "ResourceReuse.h"
#include "OitResolve.h"
#include "GpuScope.h"
#include <cstdio>
#include <nvrhi/utils.h>
#include <stdexcept>

namespace avboit
{
void OitResolve::Create(nvrhi::IDevice *device, const OitResolveInputs &inputs)
{
    m_accumulation = {};
    m_backgroundPass = {};
    m_backgroundTiles = ((inputs.width + 63) / 64) * ((inputs.height + 63) / 64);
    auto texture = [&](const nvrhi::TextureHandle &current, nvrhi::Format format, const char *name)
    {
        return ReuseTexture(device, current,
                            nvrhi::TextureDesc()
                                .setWidth(inputs.width)
                                .setHeight(inputs.height)
                                .setFormat(format)
                                .setIsRenderTarget(true)
                                .setDebugName(name)
                                .setClearValue(nvrhi::Color(0))
                                .setUseClearValue(true)
                                .setInitialState(nvrhi::ResourceStates::RenderTarget)
                                .setKeepInitialState(true));
    };
    const auto accumFormat = inputs.halfAccumulation ? nvrhi::Format::RGBA16_FLOAT : nvrhi::Format::R11G11B10_FLOAT;
    m_numerator = texture(m_numerator, accumFormat, "Numerator");
    m_denominator = texture(m_denominator, accumFormat, "Denominator");
    m_totalTau = texture(m_totalTau, accumFormat, "Total optical depth");
    m_backNumerator = texture(m_backNumerator, accumFormat, "Back numerator");
    m_background = ReuseTexture(device, m_background,
                                nvrhi::TextureDesc()
                                    .setWidth(inputs.width)
                                    .setHeight(inputs.height)
                                    .setFormat(nvrhi::Format::RGBA16_FLOAT)
                                    .setIsRenderTarget(true)
                                    .setDebugName("Background B prime cache")
                                    .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                    .setKeepInitialState(true));
    m_accumulation.framebuffer = device->createFramebuffer(
        nvrhi::FramebufferDesc()
            .addColorAttachment(m_numerator)
            .addColorAttachment(m_denominator)
            .addColorAttachment(m_totalTau)
            .addColorAttachment(m_backNumerator)
            .setDepthAttachment(
                nvrhi::FramebufferAttachment().setTexture(inputs.opaqueHardwareDepth).setReadOnly(true)));
    auto create = [&](Pass &pass, nvrhi::BindingSetDesc desc, nvrhi::IShader *vs, nvrhi::IShader *ps, bool additive)
    {
        desc.addItem(nvrhi::BindingSetItem::TypedBuffer_SRV(15, inputs.camera));
        desc.addItem(nvrhi::BindingSetItem::Texture_SRV(24, inputs.opaqueHardwareDepth));
        desc.addItem(nvrhi::BindingSetItem::TypedBuffer_SRV(23, inputs.materials));
        pass.layout = nullptr;
        pass.bindings = nullptr;
        if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::AllGraphics, 0, desc, pass.layout,
                                                     pass.bindings))
            throw std::runtime_error("OIT bindings");
        nvrhi::GraphicsPipelineDesc pipeline;
        pipeline.VS = vs;
        pipeline.PS = ps;
        pipeline.bindingLayouts = {pass.layout};
        pipeline.renderState.depthStencilState.depthTestEnable = additive;
        pipeline.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::LessOrEqual;
        pipeline.renderState.depthStencilState.depthWriteEnable = false;
        pipeline.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
        if (additive)
            for (unsigned target = 0; target < 4; ++target)
                pipeline.renderState.blendState.targets[target]
                    .setBlendEnable(true)
                    .setSrcBlend(nvrhi::BlendFactor::One)
                    .setDestBlend(nvrhi::BlendFactor::One);
        if (additive)
        {
            m_accumulationPipelines.clear();
            for (const auto &material : inputs.materialShaders)
            {
                pipeline.VS = material.vertex;
                pipeline.PS = material.accumulationPixel;
                pipeline.renderState.rasterState.cullMode = material.cullMode;
                m_accumulationPipelines.push_back(
                    device->createGraphicsPipeline(pipeline, pass.framebuffer->getFramebufferInfo()));
            }
        }
        else
            pass.pipeline = device->createGraphicsPipeline(pipeline, pass.framebuffer->getFramebufferInfo());
    };
    using B = nvrhi::BindingSetItem;
    using D = nvrhi::BindingSetDesc;
    create(m_accumulation,
           D().addItem(B::TypedBuffer_SRV(0, inputs.vertices, nvrhi::Format::UNKNOWN, inputs.vertexRange))
               .addItem(B::TypedBuffer_SRV(1, inputs.warp))
               .addItem(B::Texture_SRV(2, inputs.lut))
               .addItem(B::Texture_SRV(3, inputs.opaque))
               .addItem(B::Texture_SRV(7, inputs.interfaceDepth))
               .addItem(B::Sampler(0, inputs.linear)),
           nullptr, nullptr, true);
    auto resolveBindings = D().addItem(B::TypedBuffer_SRV(1, inputs.warp))
                               .addItem(B::Texture_SRV(2, inputs.lut))
                               .addItem(B::Sampler(0, inputs.linear))
                               .addItem(B::Texture_SRV(7, inputs.interfaceDepth))
                               .addItem(B::Texture_SRV(8, inputs.interfaceSurface))
                               .addItem(B::Texture_SRV(9, m_backNumerator))
                               .addItem(B::Texture_SRV(3, inputs.opaque))
                               .addItem(B::Texture_SRV(4, m_numerator))
                               .addItem(B::Texture_SRV(5, m_denominator))
                               .addItem(B::Texture_SRV(6, m_totalTau))
                               .addItem(B::Texture_SRV(30, inputs.screenTiles));
    m_backgroundPass.framebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_background));
    create(m_backgroundPass, resolveBindings, inputs.backgroundVertex, inputs.backgroundPixel, false);
    m_indices = inputs.indices;
}
void OitResolve::Pass::Record(nvrhi::ICommandList *commands, unsigned vertices, unsigned instances) const
{
    nvrhi::GraphicsState state;
    state.pipeline = pipeline;
    state.framebuffer = framebuffer;
    state.bindings = {bindings};
    state.viewport.addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());
    commands->setGraphicsState(state);
    commands->draw(nvrhi::DrawArguments().setVertexCount(vertices).setInstanceCount(instances));
}
void OitResolve::RecordAccumulation(nvrhi::ICommandList *commands, const std::vector<GeometryDraw> &draws) const
{
    for (auto texture : {m_numerator, m_denominator, m_totalTau, m_backNumerator})
        commands->clearTextureFloat(texture, nvrhi::AllSubresources, nvrhi::Color(0));
    nvrhi::GraphicsState state;
    state.framebuffer = m_accumulation.framebuffer;
    state.bindings = {m_accumulation.bindings};
    state.viewport.addViewportAndScissorRect(m_accumulation.framebuffer->getFramebufferInfo().getViewport());
    state.indexBuffer = nvrhi::IndexBufferBinding().setBuffer(m_indices).setFormat(nvrhi::Format::R32_UINT);
    for (const auto &draw : draws)
    {
        char label[96];
        snprintf(label, sizeof(label), "Accumulation actor %u / material %u", draw.actor, draw.material);
        AVBOIT_GPU_SCOPE(commands, label);
        state.pipeline = m_accumulationPipelines.at(draw.material);
        commands->setGraphicsState(state);
        commands->drawIndexed(
            nvrhi::DrawArguments().setStartIndexLocation(draw.firstIndex).setVertexCount(draw.indexCount));
    }
}
void OitResolve::RecordResolve(nvrhi::ICommandList *commands, bool poison) const
{
    if (poison)
        commands->clearTextureFloat(m_background, nvrhi::AllSubresources, nvrhi::Color(60000, 0, 60000, 1));
    m_backgroundPass.Record(commands, 6, m_backgroundTiles);
}
} // namespace avboit
