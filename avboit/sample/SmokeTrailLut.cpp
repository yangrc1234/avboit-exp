// SPDX-License-Identifier: MIT
#include "SmokeTrailLut.h"
#include <nvrhi/utils.h>

namespace avboit
{
void SmokeTrailLut::Create(nvrhi::IDevice *device, nvrhi::IShader *shader)
{
    m_valid = false;
    capsules = device->createBuffer(nvrhi::BufferDesc()
                                        .setByteSize(sizeof(SmokeTrails::GpuData))
                                        .setFormat(nvrhi::Format::RGBA32_FLOAT)
                                        .setCanHaveTypedViews(true)
                                        .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                        .setKeepInitialState(true)
                                        .setDebugName("Smoke capsules and candidate LUT transform"));
    candidates = device->createTexture(nvrhi::TextureDesc()
                                           .setWidth(SmokeTrails::LutSize)
                                           .setHeight(SmokeTrails::LutSize)
                                           .setDepth(SmokeTrails::LutSize)
                                           .setDimension(nvrhi::TextureDimension::Texture3D)
                                           .setFormat(nvrhi::Format::R32_UINT)
                                           .setIsUAV(true)
                                           .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                           .setKeepInitialState(true)
                                           .setDebugName("Smoke capsule candidates (16 cubed bitmask)"));
    nvrhi::BindingSetDesc bindings;
    bindings.addItem(nvrhi::BindingSetItem::TypedBuffer_SRV(35, capsules))
        .addItem(nvrhi::BindingSetItem::Texture_UAV(0, candidates));
    m_bindings = nullptr;
    m_layout = nullptr;
    nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::Compute, 0, bindings, m_layout, m_bindings);
    m_pipeline =
        device->createComputePipeline(nvrhi::ComputePipelineDesc().setComputeShader(shader).addBindingLayout(m_layout));
}
void SmokeTrailLut::Record(nvrhi::ICommandList *commands, SmokeTrails::GpuData data, SmokeTrails::Point low,
                           SmokeTrails::Point high, bool reference, bool forceRebuild)
{
    // A single capsule is cheaper to evaluate directly than to index first.
    data[0][1] = reference || data[0][0] <= 1.f ? 0.f : 1.f;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        data[SmokeTrails::LutOrigin][axis] = low[axis];
        data[SmokeTrails::LutOrigin + 1][axis] = float(SmokeTrails::LutSize) / std::max(high[axis] - low[axis], .001f);
    }
    commands->writeBuffer(capsules, data.data(), sizeof(data));
    // Lifetime fading changes weights, not candidates. Compaction/replacement of
    // capsule indices and actor-bound changes do invalidate the index.
    auto geometry = data;
    for (unsigned i = 0; i < SmokeTrails::Capacity; ++i)
        geometry[2 + 2 * i][3] = 0;
    if (data[0][1] == 0.f)
    {
        m_valid = false; // Never read an uninitialized/stale LUT on the next active shot.
        return;
    }
    if (m_valid && !forceRebuild && geometry == m_geometry)
        return;
    m_geometry = geometry;
    m_valid = true;
    nvrhi::ComputeState state;
    state.pipeline = m_pipeline;
    state.bindings = {m_bindings};
    commands->setComputeState(state);
    commands->dispatch(4, 4, 4); // 4x4x4 threads, one writer for every cell; no clear/atomics.
}
} // namespace avboit
