// SPDX-License-Identifier: MIT
#include "ResourceReuse.h"
#include "GaussianPyramid.h"
#include <nvrhi/utils.h>
#include <stdexcept>
#include <cstdio>

namespace avboit
{
void GaussianPyramid::AllocateSchedule(nvrhi::IDevice *device, unsigned width, unsigned height, unsigned mipCount)
{
    m_schedule = TileSchedule::Make(width, height, mipCount);
    m_screenWidth = width;
    m_screenHeight = height;
    auto scratch = [&](nvrhi::BufferHandle current, nvrhi::Format format, unsigned bytes, const char *name)
    {
        return ReuseBuffer(device, current,
                           nvrhi::BufferDesc()
                               .setByteSize(bytes)
                               .setFormat(format)
                               .setCanHaveTypedViews(true)
                               .setCanHaveUAVs(true)
                               .setDebugName(name)
                               .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
                               .setKeepInitialState(true));
    };
    m_workMask = scratch(m_workMask, nvrhi::Format::R32_UINT, m_schedule.entries * 4, "Gaussian output masks");
    m_footprints = scratch(m_footprints, nvrhi::Format::RGBA32_FLOAT, 25 * 16, "Frost producer footprints");
    m_footprintsDirty = true;
    m_rectangles = ReuseBuffer(device, m_rectangles,
                               nvrhi::BufferDesc()
                                   .setByteSize(11 * 16)
                                   .setCanHaveTypedViews(true)
                                   .setCanHaveUAVs(true)
                                   .setFormat(nvrhi::Format::RGBA32_UINT)
                                   .setDebugName("Blur dispatch rectangles")
                                   .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
                                   .setKeepInitialState(true));
}

void GaussianPyramid::Create(nvrhi::IDevice *device, const GaussianInputs &inputs)
{
    const auto chain = FrostChain::Make(inputs.width, inputs.height, inputs.mipCount);
    m_width = chain.width;
    m_height = chain.height;
    m_mipCount = chain.mipCount;
    // Caller must retire GPU work before reconnecting mip bindings.
    m_fused.clear();
    // Separate from sharp B: this grid is invariant to internal render resolution.
    m_pyramid = ReuseTexture(device, m_pyramid,
                             nvrhi::TextureDesc()
                                 .setWidth(m_width)
                                 .setHeight(m_height)
                                 .setMipLevels(m_mipCount)
                                 .setFormat(nvrhi::Format::RGBA16_FLOAT)
                                 .setIsUAV(true)
                                 .setDebugName("Fixed-screen frost pyramid")
                                 .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                 .setKeepInitialState(true));
    using B = nvrhi::BindingSetItem;
    using D = nvrhi::BindingSetDesc;
    auto compute = [&](const D &desc, nvrhi::IShader *shader)
    {
        Pass pass;
        if (!nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::Compute, 0, desc, pass.layout,
                                                     pass.bindings))
            throw std::runtime_error("Gaussian bindings");
        pass.pipeline = device->createComputePipeline(
            nvrhi::ComputePipelineDesc().setComputeShader(shader).addBindingLayout(pass.layout));
        return pass;
    };
    auto blur = [&](nvrhi::ITexture *source, unsigned sourceMip, nvrhi::ITexture *target, unsigned targetMip,
                    unsigned stage, nvrhi::IShader *shader)
    {
        return compute(D().addItem(B::TypedBuffer_SRV(15, inputs.camera))
                           .addItem(B::Texture_SRV(0, source, nvrhi::Format::UNKNOWN,
                                                   nvrhi::TextureSubresourceSet(sourceMip, 1, 0, 1)))
                           .addItem(B::TypedBuffer_SRV(28, m_workMask, nvrhi::Format::UNKNOWN, WorkRange(stage)))
                           .addItem(B::Texture_UAV(0, target, nvrhi::Format::UNKNOWN,
                                                   nvrhi::TextureSubresourceSet(targetMip, 1, 0, 1)))
                           .addItem(B::Sampler(0, inputs.linearSampler)),
                       shader);
    };
    m_sparseBase = compute(
        D().addItem(B::TypedBuffer_SRV(15, inputs.camera))
            .addItem(B::Texture_SRV(27, inputs.background))
            .addItem(B::Texture_SRV(3, inputs.opaque))
            .addItem(B::Texture_SRV(30, inputs.screenTiles))
            .addItem(B::TypedBuffer_SRV(28, m_workMask, nvrhi::Format::UNKNOWN, WorkRange(1)))
            .addItem(B::Texture_UAV(0, m_pyramid, nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet(0, 1, 0, 1)))
            .addItem(B::Sampler(0, inputs.linearSampler)),
        inputs.sparseShader);
    for (unsigned level = 1; level < m_mipCount; ++level)
        m_fused.push_back(blur(m_pyramid, level - 1, m_pyramid, level, level * 2 + 1, inputs.fusedShader));
}
void GaussianPyramid::ResetSchedule(nvrhi::ICommandList *commands, bool cubic) const
{
    if (m_footprintsDirty || cubic != m_lastCubic)
    {
        const auto footprints = FrostChain::Make(m_screenWidth, m_screenHeight, m_mipCount)
                                    .Footprints(m_screenWidth, m_screenHeight, cubic);
        commands->writeBuffer(m_footprints, footprints.data(), sizeof(footprints));
        m_footprintsDirty = false;
        m_lastCubic = cubic;
    }
    commands->clearBufferUInt(m_workMask, 0);
    std::array<std::array<unsigned, 4>, 11> envelopes{};
    for (unsigned i = 0; i < 11; ++i)
        envelopes[i] = {m_schedule.width[i], m_schedule.height[i], 0, 0};
    commands->writeBuffer(m_rectangles, envelopes.data(), sizeof(envelopes));
}
void GaussianPyramid::RecordBlur(nvrhi::ICommandList *commands, unsigned lastMip, bool poison,
                                 const GpuTimingRoute &baseTiming, const GpuTimingRoute &mipTiming) const
{
    AVBOIT_GPU_SCOPE(commands, "Gaussian pyramid");
    if (lastMip >= m_mipCount)
        throw std::out_of_range("Gaussian last mip");
    if (poison)
        commands->clearTextureFloat(m_pyramid, nvrhi::AllSubresources, nvrhi::Color(60000, 0, 60000, 1));
    auto dispatch = [&](unsigned level)
    {
        const auto &pass = level == 0 ? m_sparseBase : m_fused[level - 1];
        commands->setComputeState(nvrhi::ComputeState().setPipeline(pass.pipeline).addBindingSet(pass.bindings));
        commands->dispatch(((m_width >> level) + 3) / 4, ((m_height >> level) + 3) / 4, 1);
    };
    {
        AVBOIT_GPU_SCOPE(commands, "Gaussian base filter (mip 0)", baseTiming);
        dispatch(0);
    }
    {
        AVBOIT_GPU_SCOPE(commands, "Gaussian fused reductions", mipTiming);
        for (unsigned level = 1; level <= lastMip; ++level)
        {
            char name[96];
            snprintf(name, sizeof(name), "Gaussian fused mip %u (%u x %u)", level, m_width >> level, m_height >> level);
            AVBOIT_GPU_SCOPE(commands, name);
            dispatch(level);
        }
    }
}
} // namespace avboit
