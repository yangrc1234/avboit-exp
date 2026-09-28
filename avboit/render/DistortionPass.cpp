// SPDX-License-Identifier: MIT
#include "ResourceReuse.h"
#include "DistortionPass.h"
#include "GpuScope.h"
#include <nvrhi/utils.h>
#include <cstdio>
#include <stdexcept>

namespace avboit
{
void DistortionPass::Create(nvrhi::IDevice *device, const DistortionInputs &inputs)
{
    m_bindings.clear();
    m_vertexCounts.clear();
    m_materials.clear();
    m_pipelines.clear();
    m_offset = ReuseTexture(device, m_offset,
                            nvrhi::TextureDesc()
                                .setWidth(inputs.width)
                                .setHeight(inputs.height)
                                .setFormat(nvrhi::Format::RG16_FLOAT)
                                .setIsRenderTarget(true)
                                .setDebugName("VFX displacement")
                                .setInitialState(nvrhi::ResourceStates::RenderTarget)
                                .setKeepInitialState(true));
    m_framebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(m_offset));

    using B = nvrhi::BindingSetItem;
    for (const auto &batch : inputs.batches)
    {
        auto desc = nvrhi::BindingSetDesc()
                        .addItem(B::TypedBuffer_SRV(0, inputs.vertices, nvrhi::Format::UNKNOWN, batch.vertices))
                        .addItem(B::TypedBuffer_SRV(1, inputs.depthWarp))
                        .addItem(B::Texture_SRV(2, inputs.transmittance))
                        .addItem(B::Texture_SRV(3, inputs.opaqueColorDepth))
                        .addItem(B::Texture_SRV(7, inputs.interfaceDepth))
                        .addItem(B::Sampler(0, inputs.linearSampler))
                        .addItem(B::Texture_SRV(24, inputs.opaqueHardwareDepth))
                        .addItem(B::TypedBuffer_SRV(15, inputs.camera))
                        .addItem(B::TypedBuffer_SRV(23, inputs.materials));
        nvrhi::BindingSetHandle bindings;
        if (m_bindings.empty())
        {
            m_layout = nullptr;
            if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::AllGraphics, 0, desc, m_layout,
                                                         bindings))
                throw std::runtime_error("Distortion bindings");
        }
        else
        {
            bindings = device->createBindingSet(desc, m_layout);
        }
        m_bindings.push_back(bindings);
        m_vertexCounts.push_back(batch.vertexCount);
        m_materials.push_back(batch.material);
    }
    if (m_bindings.empty())
        throw std::invalid_argument("Distortion requires a geometry batch");

    nvrhi::GraphicsPipelineDesc pipeline;
    pipeline.bindingLayouts = {m_layout};
    pipeline.renderState.depthStencilState.depthTestEnable = false;
    pipeline.renderState.depthStencilState.depthWriteEnable = false;
    pipeline.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    pipeline.renderState.blendState.targets[0]
        .setBlendEnable(true)
        .setSrcBlend(nvrhi::BlendFactor::One)
        .setDestBlend(nvrhi::BlendFactor::One);
    for (const auto &batch : inputs.batches)
    {
        const auto &material = inputs.materialShaders.at(batch.material);
        pipeline.VS = material.vertex;
        pipeline.PS = material.distortionPixel;
        pipeline.renderState.rasterState.cullMode = material.cullMode;
        m_pipelines.push_back(device->createGraphicsPipeline(pipeline, m_framebuffer->getFramebufferInfo()));
    }
}

void DistortionPass::Record(nvrhi::ICommandList *commands, const std::vector<unsigned> &batches) const
{
    {
        AVBOIT_GPU_SCOPE(commands, batches.empty() ? "Clear offset (no active VFX batches)" : "Clear offset");
        commands->clearTextureFloat(m_offset, nvrhi::AllSubresources, nvrhi::Color(0));
    }
    for (unsigned batch : batches)
    {
        char name[96];
        snprintf(name, sizeof(name), "VFX actor %u / material %u (%u vertices)", batch, m_materials.at(batch),
                 m_vertexCounts.at(batch));
        AVBOIT_GPU_SCOPE(commands, name);
        nvrhi::GraphicsState state;
        state.pipeline = m_pipelines.at(batch);
        state.framebuffer = m_framebuffer;
        state.bindings = {m_bindings.at(batch)};
        state.viewport.addViewportAndScissorRect(m_framebuffer->getFramebufferInfo().getViewport());
        commands->setGraphicsState(state);
        commands->draw(nvrhi::DrawArguments().setVertexCount(m_vertexCounts.at(batch)));
    }
}
} // namespace avboit
