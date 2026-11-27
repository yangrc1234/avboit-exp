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
    m_lowAccumulation = {};
    m_upsample = {};
    m_lowPipelines.clear();
    m_proxyMark = nullptr;
    m_proxyStencil = nullptr;
    m_proxyCells = inputs.vdbProxyCells;
    m_brickQuadFirstIndex = inputs.brickQuadFirstIndex;
    m_groupMarch = bool(inputs.vdbGroupVertex);
    m_marchPipeline = nullptr;
    m_marchBindings = nullptr;
    m_marchLayout = nullptr;
    m_marchHeatmap = inputs.marchHeatmap;
    m_vdbRaySpacing = inputs.vdbRaySpacing;
    m_computeMaterials.clear();
    for (const auto &material : inputs.materialShaders)
        m_computeMaterials.push_back(material.computeVolume);
    m_screenTileCount = ((inputs.width + 63) / 64) * ((inputs.height + 63) / 64);
    auto texture = [&](const nvrhi::TextureHandle &current, nvrhi::Format format, const char *name)
    {
        return ReuseTexture(device, current,
                            nvrhi::TextureDesc()
                                .setWidth(inputs.width)
                                .setHeight(inputs.height)
                                .setFormat(format)
                                .setIsRenderTarget(true)
                                .setIsUAV(inputs.vdbMarchCompute && !inputs.lowResolutionVolumes)
                                .setDebugName(name)
                                .setClearValue(nvrhi::Color(0))
                                .setUseClearValue(true)
                                .setInitialState(nvrhi::ResourceStates::RenderTarget)
                                .setKeepInitialState(true));
    };
    const auto accumFormat = inputs.halfAccumulation ? nvrhi::Format::RGBA16_FLOAT : nvrhi::Format::R11G11B10_FLOAT;
    m_numerator = texture(m_numerator, inputs.marchHeatmap ? nvrhi::Format::RGBA32_FLOAT : accumFormat, "Numerator");
    m_denominator = texture(m_denominator, accumFormat, "Denominator");
    m_totalTau = texture(m_totalTau, accumFormat, "Total optical depth");
    m_backNumerator = texture(m_backNumerator, accumFormat, "Back numerator");
    m_background = ReuseTexture(device, m_background,
                                nvrhi::TextureDesc()
                                    .setWidth(inputs.width)
                                    .setHeight(inputs.height)
                                    .setFormat(nvrhi::Format::RGBA16_FLOAT)
                                    .setIsRenderTarget(true)
                                    .setClearValue(nvrhi::Color(0))
                                    .setUseClearValue(true)
                                    .setDebugName("Refracted background B prime cache")
                                    .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                    .setKeepInitialState(true));
    auto accumColors = nvrhi::FramebufferDesc()
                           .addColorAttachment(m_numerator)
                           .addColorAttachment(m_denominator)
                           .addColorAttachment(m_totalTau)
                           .addColorAttachment(m_backNumerator);
    if (inputs.lowResolutionVolumes)
        m_upsample.framebuffer = device->createFramebuffer(accumColors);
    m_accumulation.framebuffer = device->createFramebuffer(accumColors.setDepthAttachment(
        nvrhi::FramebufferAttachment().setTexture(inputs.opaqueHardwareDepth).setReadOnly(true)));
    // Shared by full/half-resolution volume accumulation. Bound with the PSO.
    auto shadowComparison = device->createSampler(nvrhi::SamplerDesc()
                                                      .setAllFilters(true)
                                                      .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
                                                      .setReductionType(nvrhi::SamplerReductionType::Comparison));
    m_classifyPipeline = nullptr;
    m_classifyBindings = nullptr;
    m_brickLowPipeline = nullptr;
    m_brickCube = inputs.brickCube;
    if (inputs.brickClassify)
    {
        using B = nvrhi::BindingSetItem;
        auto desc = nvrhi::BufferDesc().setCanHaveTypedViews(true).setCanHaveUAVs(true)
            .setKeepInitialState(true).setInitialState(nvrhi::ResourceStates::UnorderedAccess);
        m_brickArgs = device->createBuffer(desc.setByteSize(40).setFormat(nvrhi::Format::R32_UINT)
            .setIsDrawIndirectArgs(true).setDebugName("VDB brick indexed draw arguments"));
        m_brickList = device->createBuffer(desc.setByteSize(size_t(m_proxyCells) * 16).setFormat(nvrhi::Format::RGBA32_UINT)
            .setIsDrawIndirectArgs(false).setDebugName("VDB classified bricks"));
        auto bindings = nvrhi::BindingSetDesc()
            .addItem(B::TypedBuffer_SRV(0, inputs.vertices, nvrhi::Format::UNKNOWN, inputs.vertexRange))
            .addItem(B::TypedBuffer_SRV(1, inputs.warp)).addItem(B::Texture_SRV(2, inputs.lut))
            .addItem(B::TypedBuffer_SRV(15, inputs.camera)).addItem(B::TypedBuffer_SRV(23, inputs.materials))
            .addItem(B::TypedBuffer_SRV(33, inputs.smokeBricks)).addItem(B::Texture_SRV(38, inputs.smokeVisibility))
            .addItem(B::ConstantBuffer(3, inputs.smokeSampling)).addItem(B::ConstantBuffer(2, inputs.volumeLights))
            .addItem(B::Texture_SRV(28, inputs.regularLightDepth)).addItem(B::Texture_SRV(29, inputs.lightMoments))
            .addItem(B::Texture_SRV(31, inputs.lightOpticalDepth)).addItem(B::Sampler(0, inputs.linear))
            .addItem(B::Sampler(4, shadowComparison)).addItem(B::PushConstants(4, sizeof(uint32_t)))
            .addItem(B::TypedBuffer_UAV(0, m_brickArgs)).addItem(B::TypedBuffer_UAV(1, m_brickList));
        nvrhi::BindingLayoutHandle layout;
        if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::Compute, 0, bindings, layout, m_classifyBindings))
            throw std::runtime_error("VDB classifier bindings");
        m_classifyPipeline = device->createComputePipeline(
            nvrhi::ComputePipelineDesc().setComputeShader(inputs.brickClassify).addBindingLayout(layout));
        if (!m_classifyPipeline)
            throw std::runtime_error("VDB classifier pipeline");
    }
    else
    {
        m_brickArgs = nullptr;
        m_brickList = nullptr;
    }
    m_cornerPipeline = nullptr;
    m_cornerBindings = nullptr;
    m_cornerLayout = nullptr;
    if (inputs.vdbCornerCache)
    {
        const auto &grid = inputs.cornerGrid;
        m_cornerLight = ReuseTexture(device, m_cornerLight, nvrhi::TextureDesc()
            .setDimension(nvrhi::TextureDimension::Texture3D).setWidth(grid[0]).setHeight(grid[1]).setDepth(grid[2])
            .setFormat(nvrhi::Format::RGBA16_FLOAT).setIsUAV(true).setDebugName("VDB shared corner lighting")
            .setInitialState(nvrhi::ResourceStates::ShaderResource).setKeepInitialState(true));
        using B = nvrhi::BindingSetItem;
        auto desc = nvrhi::BindingSetDesc()
            .addItem(B::TypedBuffer_SRV(0, inputs.vertices, nvrhi::Format::UNKNOWN, inputs.vertexRange))
            .addItem(B::TypedBuffer_SRV(23, inputs.materials))
            .addItem(B::TypedBuffer_SRV(33, inputs.smokeBricks))
            .addItem(B::Texture_SRV(38, inputs.smokeVisibility))
            .addItem(B::ConstantBuffer(3, inputs.smokeSampling))
            .addItem(B::ConstantBuffer(2, inputs.volumeLights))
            .addItem(B::Texture_SRV(28, inputs.regularLightDepth))
            .addItem(B::Texture_SRV(29, inputs.lightMoments))
            .addItem(B::Texture_SRV(31, inputs.lightOpticalDepth))
            .addItem(B::Sampler(0, inputs.linear)).addItem(B::Sampler(4, shadowComparison))
            .addItem(B::PushConstants(4, sizeof(uint32_t))).addItem(B::Texture_UAV(0, m_cornerLight));
        if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::Compute, 0, desc,
                                                     m_cornerLayout, m_cornerBindings))
            throw std::runtime_error("VDB corner lighting bindings");
        m_cornerPipeline = device->createComputePipeline(
            nvrhi::ComputePipelineDesc().setComputeShader(inputs.vdbCornerCache).addBindingLayout(m_cornerLayout));
        if (!m_cornerPipeline)
            throw std::runtime_error("VDB corner lighting pipeline");
    }
    else
        m_cornerLight = nullptr;

    auto create = [&](Pass &pass, nvrhi::BindingSetDesc desc, nvrhi::IShader *vs, nvrhi::IShader *ps, bool additive)
    {
        desc.addItem(nvrhi::BindingSetItem::TypedBuffer_SRV(15, inputs.camera));
        desc.addItem(nvrhi::BindingSetItem::Texture_SRV(24, inputs.opaqueHardwareDepth));
        desc.addItem(nvrhi::BindingSetItem::TypedBuffer_SRV(23, inputs.materials));
        if (additive && m_brickList)
            desc.addItem(nvrhi::BindingSetItem::TypedBuffer_SRV(39, m_brickList));
        if (additive && m_cornerLight)
            desc.addItem(nvrhi::BindingSetItem::Texture_SRV(37, m_cornerLight));
        if (additive && inputs.volumeDensity)
            desc.addItem(nvrhi::BindingSetItem::Texture_SRV(26, inputs.volumeDensity));
        if (additive && inputs.smokeTrails)
            desc.addItem(nvrhi::BindingSetItem::TypedBuffer_SRV(35, inputs.smokeTrails))
                .addItem(nvrhi::BindingSetItem::Texture_SRV(36, inputs.smokeTrailCandidates));
        if (additive && inputs.smokeBricks)
            desc.addItem(nvrhi::BindingSetItem::TypedBuffer_SRV(33, inputs.smokeBricks))
                .addItem(nvrhi::BindingSetItem::Texture_SRV(34, inputs.gameplaySmokeMask));
        if (additive && inputs.smokeSampling)
            desc.addItem(nvrhi::BindingSetItem::ConstantBuffer(3, inputs.smokeSampling))
                .addItem(nvrhi::BindingSetItem::Texture_SRV(38, inputs.smokeVisibility));
        if (additive && (inputs.vdbProxyVertex || inputs.vdbGroupVertex))
            desc.addItem(nvrhi::BindingSetItem::PushConstants(4, sizeof(uint32_t)));
        if (additive && inputs.volumeLights)
        {
            desc.addItem(nvrhi::BindingSetItem::ConstantBuffer(2, inputs.volumeLights));
            desc.addItem(nvrhi::BindingSetItem::Texture_SRV(28, inputs.regularLightDepth));
            desc.addItem(nvrhi::BindingSetItem::Sampler(4, shadowComparison));
            desc.addItem(nvrhi::BindingSetItem::Texture_SRV(29, inputs.lightMoments));
            desc.addItem(nvrhi::BindingSetItem::Texture_SRV(31, inputs.lightOpticalDepth));
        }
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
        pipeline.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::GreaterOrEqual;
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
                pipeline.VS = material.accumulationVertex ? material.accumulationVertex : material.vertex;
                const bool fullBricks = m_groupMarch && material.computeVolume && !inputs.lowResolutionVolumes;
                if (fullBricks)
                    pipeline.VS = inputs.vdbGroupVertex;
                // The screen proxy has no surface depth. The brick PS clips its ray to scene depth.
                pipeline.renderState.depthStencilState.depthTestEnable = !fullBricks || m_brickCube;
                pipeline.PS = material.accumulationPixel;
                pipeline.renderState.rasterState.cullMode = fullBricks ? nvrhi::RasterCullMode::None : material.cullMode;
                if (fullBricks && m_brickCube)
                    pipeline.renderState.rasterState.cullMode = nvrhi::RasterCullMode::Back;
                if (fullBricks && inputs.brickLowPixel)
                {
                    auto lowLight = pipeline;
                    lowLight.PS = inputs.brickLowPixel;
                    m_brickLowPipeline = device->createGraphicsPipeline(lowLight, pass.framebuffer->getFramebufferInfo());
                }
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
    if (inputs.lowResolutionVolumes)
    {
        nvrhi::FramebufferDesc fb;
        for (unsigned j = 0; j < 4; ++j)
        {
            m_lowTargets[j] = ReuseTexture(device, m_lowTargets[j],
                                           nvrhi::TextureDesc()
                                               .setWidth(inputs.width / 2)
                                               .setHeight(inputs.height / 2)
                                               .setFormat(j == 0 && inputs.marchHeatmap ? nvrhi::Format::RGBA32_FLOAT : accumFormat)
                                               .setIsUAV(bool(inputs.vdbMarchCompute))
                                               .setIsRenderTarget(true)
                                               .setClearValue(nvrhi::Color(0))
                                               .setUseClearValue(true)
                                               .setInitialState(nvrhi::ResourceStates::RenderTarget)
                                               .setKeepInitialState(true)
                                               .setDebugName("Half-resolution volume accumulation"));
            fb.addColorAttachment(m_lowTargets[j]);
        }
        if (inputs.vdbProxyVertex)
        {
            m_proxyStencil = device->createTexture(nvrhi::TextureDesc()
                .setWidth(inputs.width / 2).setHeight(inputs.height / 2).setFormat(nvrhi::Format::D24S8)
                .setIsRenderTarget(true).setInitialState(nvrhi::ResourceStates::DepthWrite)
                .setKeepInitialState(true).setDebugName("Half-resolution VDB proxy stencil"));
            fb.setDepthAttachment(m_proxyStencil);
        }
        m_lowAccumulation.framebuffer = device->createFramebuffer(fb);
        if (m_proxyStencil)
        {
            nvrhi::GraphicsPipelineDesc mark;
            mark.VS = inputs.vdbProxyVertex;
            mark.bindingLayouts = {m_accumulation.layout};
            mark.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
            auto &ds = mark.renderState.depthStencilState;
            ds.setDepthTestEnable(false).setDepthWriteEnable(false).setStencilEnable(true).setDynamicStencilRef(true);
            ds.frontFaceStencil.setPassOp(nvrhi::StencilOp::Replace);
            ds.backFaceStencil = ds.frontFaceStencil;
            for (auto &target : mark.renderState.blendState.targets)
                target.setColorWriteMask(nvrhi::ColorMask(0));
            m_proxyMark = device->createGraphicsPipeline(mark, m_lowAccumulation.framebuffer->getFramebufferInfo());
            if (!m_proxyMark)
                throw std::runtime_error("VDB proxy stencil pipeline");
        }
        m_lowAccumulation.bindings = m_accumulation.bindings;
        for (const auto &material : inputs.materialShaders)
        {
            nvrhi::GraphicsPipelineHandle low;
            if (material.halfResolutionPixel)
            {
                nvrhi::GraphicsPipelineDesc p;
                p.VS = material.accumulationVertex ? material.accumulationVertex : material.vertex;
                p.PS = material.halfResolutionPixel;
                if (m_groupMarch && material.computeVolume)
                    p.VS = inputs.vdbGroupVertex;
                if (m_proxyMark && material.computeVolume)
                {
                    auto &ds = p.renderState.depthStencilState;
                    ds.setStencilEnable(true).setDynamicStencilRef(true).setStencilWriteMask(0);
                    ds.frontFaceStencil.setStencilFunc(nvrhi::ComparisonFunc::Equal);
                    ds.backFaceStencil = ds.frontFaceStencil;
                }
                p.bindingLayouts = {m_accumulation.layout};
                p.renderState.depthStencilState.depthTestEnable = false;
                p.renderState.depthStencilState.depthWriteEnable = false;
                p.renderState.rasterState.cullMode = m_groupMarch && material.computeVolume ?
                    nvrhi::RasterCullMode::None : material.cullMode;
                for (unsigned j = 0; j < 4; ++j)
                    p.renderState.blendState.targets[j]
                        .setBlendEnable(true)
                        .setSrcBlend(nvrhi::BlendFactor::One)
                        .setDestBlend(nvrhi::BlendFactor::One);
                if (material.computeVolume && m_brickCube)
                    p.renderState.rasterState.cullMode = nvrhi::RasterCullMode::Back;
                if (material.computeVolume && inputs.brickLowPixel)
                {
                    auto lowLight = p;
                    lowLight.PS = inputs.brickLowPixel;
                    m_brickLowPipeline = device->createGraphicsPipeline(lowLight, m_lowAccumulation.framebuffer->getFramebufferInfo());
                }
                low = device->createGraphicsPipeline(p, m_lowAccumulation.framebuffer->getFramebufferInfo());
            }
            m_lowPipelines.push_back(low);
        }
        create(m_upsample,
               D().addItem(B::Texture_SRV(3, inputs.opaque))
                   .addItem(B::Texture_SRV(4, m_lowTargets[0]))
                   .addItem(B::Texture_SRV(5, m_lowTargets[1]))
                   .addItem(B::Texture_SRV(6, m_lowTargets[2]))
                   .addItem(B::Texture_SRV(9, m_lowTargets[3]))
                   .addItem(B::Texture_SRV(7, inputs.interfaceDepth))
                   .addItem(B::Texture_SRV(30, inputs.screenTiles)),
               inputs.backgroundVertex, inputs.upsamplePixel, false);
    }
    else
        m_lowTargets = {};
    if (inputs.brickLowPixel && !m_brickLowPipeline)
        throw std::runtime_error("VDB low-frequency lighting pipeline");
    if (inputs.vdbMarchCompute)
    {
        auto desc = *m_accumulation.bindings->getDesc();
        const std::array<nvrhi::TextureHandle, 4> full = {m_numerator, m_denominator, m_totalTau, m_backNumerator};
        const auto &targets = inputs.lowResolutionVolumes ? m_lowTargets : full;
        for (unsigned j = 0; j < targets.size(); ++j)
            desc.addItem(B::Texture_UAV(j, targets[j]));
        desc.addItem(B::PushConstants(4, 8));
        if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::Compute, 0, desc, m_marchLayout,
                                                     m_marchBindings))
            throw std::runtime_error("VDB compute bindings");
        m_marchPipeline = device->createComputePipeline(
            nvrhi::ComputePipelineDesc().setComputeShader(inputs.vdbMarchCompute).addBindingLayout(m_marchLayout));
        if (!m_marchPipeline)
            throw std::runtime_error("VDB compute pipeline");
    }
    auto resolveBindings = D().addItem(B::TypedBuffer_SRV(1, inputs.warp))
                               .addItem(B::Texture_SRV(2, inputs.lut))
                               .addItem(B::Sampler(0, inputs.linear))
                               .addItem(B::Texture_SRV(7, inputs.interfaceDepth))
                               .addItem(B::Texture_SRV(8, inputs.interfaceSurface))
                               .addItem(B::Texture_SRV(14, inputs.interfaceOffset))
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
void OitResolve::RecordAccumulation(nvrhi::ICommandList *commands, const std::vector<GeometryDraw> &draws,
                                    const AccumulationTiming &timing) const
{
    {
        AVBOIT_GPU_SCOPE(commands, "Accumulation RT clear", timing.clear);
        for (auto texture : {m_numerator, m_denominator, m_totalTau, m_backNumerator})
            commands->clearTextureFloat(texture, nvrhi::AllSubresources, nvrhi::Color(0));
    }
    bool firstClassify = true;
    auto drawPass = [&](bool low)
    {
        const auto &pass = low ? m_lowAccumulation : m_accumulation;
        nvrhi::GraphicsState state;
        state.framebuffer = pass.framebuffer;
        state.bindings = {pass.bindings};
        state.viewport.addViewportAndScissorRect(pass.framebuffer->getFramebufferInfo().getViewport());
        state.indexBuffer = nvrhi::IndexBufferBinding().setBuffer(m_indices).setFormat(nvrhi::Format::R32_UINT);
        // Full-resolution draws sample scene depth while hardware depth testing reads the same DSV.
        // NVRHI tracks each binding separately; request the combined read state once per graphics batch.
        bool prepareFullDraw = true;
        auto bindDraw = [&]()
        {
            if (low)
            {
                commands->setGraphicsState(state);
                return;
            }
            if (prepareFullDraw)
            {
                const auto &fb = pass.framebuffer->getDesc();
                for (const auto &color : fb.colorAttachments)
                    commands->setTextureState(color.texture, color.subresources, nvrhi::ResourceStates::RenderTarget);
                for (const auto &item : pass.bindings->getDesc()->bindings)
                {
                    using namespace nvrhi;
                    if (item.type == ResourceType::Texture_SRV)
                    {
                        auto *texture = static_cast<ITexture *>(item.resourceHandle);
                        auto read = ResourceStates::ShaderResource;
                        if (texture == fb.depthAttachment.texture)
                            read = read | ResourceStates::DepthRead;
                        commands->setTextureState(texture, item.subresources, read);
                    }
                    else if (item.type == ResourceType::TypedBuffer_SRV || item.type == ResourceType::ConstantBuffer)
                        commands->setBufferState(static_cast<IBuffer *>(item.resourceHandle),
                            item.type == ResourceType::ConstantBuffer ? ResourceStates::ConstantBuffer : ResourceStates::ShaderResource);
                    else if (item.type != ResourceType::Sampler && item.type != ResourceType::PushConstants)
                        throw std::logic_error("Unhandled OIT graphics resource state");
                }
                commands->setBufferState(m_indices, nvrhi::ResourceStates::IndexBuffer);
                if (state.indirectParams)
                    commands->setBufferState(state.indirectParams, nvrhi::ResourceStates::IndirectArgument);
                commands->commitBarriers();
                prepareFullDraw = false;
            }
            commands->setEnableAutomaticBarriers(false);
            commands->setGraphicsState(state);
            commands->setEnableAutomaticBarriers(true);
        };
        // Seed the cleared targets with one VDB. Additional actors use additive OM
        // blending, including overlapping volumes. No packed UAV loads are needed.
        const GeometryDraw *computed = nullptr;
        if (m_marchPipeline && low == bool(m_lowAccumulation.framebuffer))
        {
            for (const auto &draw : draws)
                if (m_computeMaterials.at(draw.material))
                {
                    computed = &draw;
                    break;
                }
            if (computed)
            {
                AVBOIT_GPU_SCOPE(commands, "VDB wave raymarch");
                nvrhi::ComputeState cs;
                cs.pipeline = m_marchPipeline;
                cs.bindings = {m_marchBindings};
                commands->setComputeState(cs);
                const unsigned args[] = {computed->firstVertex, low ? 2u : 1u};
                commands->setPushConstants(args, sizeof(args));
                const auto &fb = pass.framebuffer->getFramebufferInfo();
                unsigned groupWidth = 8 * m_vdbRaySpacing, groupHeight = 4 * m_vdbRaySpacing;
                commands->dispatch((fb.width + groupWidth - 1) / groupWidth, (fb.height + groupHeight - 1) / groupHeight);
            }
        }
        unsigned proxyRef = 0;
        for (const auto &draw : draws)
        {
            if (&draw == computed)
                continue;
            bool eligible = !m_lowPipelines.empty() && m_lowPipelines.at(draw.material);
            if (eligible != low)
                continue;
            char label[96];
            snprintf(label, sizeof(label), "Accumulation actor %u / material %u", draw.actor, draw.material);
            AVBOIT_GPU_SCOPE(commands, label);
            if (m_cornerPipeline && m_computeMaterials.at(draw.material))
            {
                // Each actor has a different world transform. Consume its cache before reusing it.
                AVBOIT_GPU_SCOPE(commands, "VDB shared corner lighting");
                nvrhi::ComputeState cs;
                cs.pipeline = m_cornerPipeline;
                cs.bindings = {m_cornerBindings};
                commands->setComputeState(cs);
                commands->setPushConstants(&draw.firstVertex, sizeof(draw.firstVertex));
                const auto &grid = m_cornerLight->getDesc();
                commands->dispatch((grid.width + 3) / 4, (grid.height + 3) / 4, (grid.depth + 3) / 4);
                prepareFullDraw = true;
            }
            if (m_classifyPipeline && m_computeMaterials.at(draw.material))
            {
                {
                    AVBOIT_GPU_SCOPE(commands, "VDB classify and light bricks", firstClassify ? timing.classify : GpuTimingRoute{});
                    firstClassify = false;
                    const unsigned indices = m_brickCube ? 36u : 6u;
                    const unsigned start = m_brickQuadFirstIndex + (m_brickCube ? 6u : 0u);
                    const unsigned args[] = {indices, 0, start, 0, 0, indices, 0, start, 0, 0};
                    commands->writeBuffer(m_brickArgs, args, sizeof(args));
                    nvrhi::ComputeState cs;
                    cs.pipeline = m_classifyPipeline;
                    cs.bindings = {m_classifyBindings};
                    commands->setComputeState(cs);
                    commands->setPushConstants(&draw.firstVertex, sizeof(draw.firstVertex));
                    commands->dispatch((m_proxyCells + 63) / 64);
                    prepareFullDraw = true;
                }
                AVBOIT_GPU_SCOPE(commands, "VDB classified brick draws");
                state.indirectParams = m_brickArgs;
                {
                    AVBOIT_GPU_SCOPE(commands, "VDB classified high draw");
                    state.pipeline = low ? m_lowPipelines.at(draw.material) : m_accumulationPipelines.at(draw.material);
                    bindDraw();
                    commands->setPushConstants(&draw.firstVertex, sizeof(draw.firstVertex));
                    commands->drawIndexedIndirect(0);
                }
                {
                    AVBOIT_GPU_SCOPE(commands, "VDB classified low draw");
                    state.pipeline = m_brickLowPipeline;
                    bindDraw();
                    const unsigned lowActor = draw.firstVertex | 0x80000000u;
                    commands->setPushConstants(&lowActor, sizeof(lowActor));
                    commands->drawIndexedIndirect(5 * sizeof(unsigned));
                }
                state.indirectParams = nullptr;
                continue;
            }
            if (low && m_proxyMark && m_computeMaterials.at(draw.material))
            {
                AVBOIT_GPU_SCOPE(commands, "VDB proxy stencil mark");
                if (!proxyRef || proxyRef == 255)
                {
                    commands->clearDepthStencilTexture(m_proxyStencil, nvrhi::AllSubresources, false, 0.f, true, 0);
                    proxyRef = 0;
                }
                state.dynamicStencilRefValue = uint8_t(++proxyRef);
                state.pipeline = m_proxyMark;
                commands->setGraphicsState(state);
                commands->setPushConstants(&draw.firstVertex, sizeof(draw.firstVertex));
                commands->draw(nvrhi::DrawArguments().setVertexCount(6).setInstanceCount(m_proxyCells));
            }
            state.pipeline = low ? m_lowPipelines.at(draw.material) : m_accumulationPipelines.at(draw.material);
            bindDraw();
            if (m_proxyMark || m_groupMarch)
                commands->setPushConstants(&draw.firstVertex, sizeof(draw.firstVertex));
            if (m_groupMarch && m_computeMaterials.at(draw.material))
            {
                AVBOIT_GPU_SCOPE(commands, "VDB instanced brick raymarch");
                auto args = nvrhi::DrawArguments().setVertexCount(6).setInstanceCount(m_proxyCells);
                if (m_brickQuadFirstIndex != ~0u)
                    commands->drawIndexed(args.setStartIndexLocation(m_brickQuadFirstIndex));
                else
                    commands->draw(args);
                continue;
            }
            commands->drawIndexed(
                nvrhi::DrawArguments().setStartIndexLocation(draw.firstIndex).setVertexCount(draw.indexCount));
        }
    };
    bool hasLow = false;
    for (const auto &draw : draws)
        hasLow |= !m_lowPipelines.empty() && m_lowPipelines.at(draw.material);
    // Emit empty timestamp intervals when disabled so all profiler slots are
    // initialized and no stale samples survive a live resolution switch.
    {
        AVBOIT_GPU_SCOPE(commands, "Half-resolution volume accumulation", timing.lowResolution);
        if (hasLow || (m_marchHeatmap && m_lowAccumulation.framebuffer))
        {
            for (const auto &target : m_lowTargets)
                commands->clearTextureFloat(target, nvrhi::AllSubresources, nvrhi::Color(0));
            drawPass(true);
        }
    }
    {
        AVBOIT_GPU_SCOPE(commands, "Volume depth-aware upsample", timing.upsample);
        if (hasLow)
            // Reuse the occupancy-produced transparency tiles. Empty tiles
            // collapse in the VS; untouched full-resolution targets stay clear.
            m_upsample.Record(commands, 6, m_screenTileCount);
    }
    {
        AVBOIT_GPU_SCOPE(commands, "Full-resolution accumulation", timing.fullResolution);
        drawPass(false);
    }
    if (firstClassify && timing.classify)
    {
        timing.classify(true);
        timing.classify(false);
    }
}
void OitResolve::RecordResolve(nvrhi::ICommandList *commands, bool poison) const
{
    // Poison old contents first to verify that the required clear covers them.
    if (poison)
        commands->clearTextureFloat(m_background, nvrhi::AllSubresources, nvrhi::Color(60000, 0, 60000, 1));
    // No border: bilinear taps outside the cache contribute zero color/validity.
    commands->clearTextureFloat(m_background, nvrhi::AllSubresources, nvrhi::Color(0));
    m_backgroundPass.Record(commands, 6, m_screenTileCount);
}
} // namespace avboit
