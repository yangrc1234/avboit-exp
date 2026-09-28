// SPDX-License-Identifier: MIT
#pragma once
#include <nvrhi/nvrhi.h>
#include <tuple>

namespace avboit
{
// Per-owner reuse, not a global pool or aliasing allocator. The caller must have
// finished GPU work before replacing resources; producers initialize their own
// contents each frame. Debug labels do not affect allocation compatibility.
inline nvrhi::TextureHandle ReuseTexture(nvrhi::IDevice *device, const nvrhi::TextureHandle &current,
                                         const nvrhi::TextureDesc &requested)
{
    auto key = [](const nvrhi::TextureDesc &d)
    {
        return std::make_tuple(d.width, d.height, d.depth, d.arraySize, d.mipLevels, d.sampleCount, d.sampleQuality,
                               d.format, d.dimension, d.isShaderResource, d.isRenderTarget, d.isUAV, d.isTypeless,
                               d.isShadingRateSurface, d.sharedResourceFlags, d.isVirtual, d.isTiled, d.initialState,
                               d.keepInitialState);
    };
    // Optimized clear metadata is part of compatibility, just like the format.
    if (current && requested.useClearValue == current->getDesc().useClearValue &&
        (!requested.useClearValue || requested.clearValue == current->getDesc().clearValue) &&
        key(current->getDesc()) == key(requested))
        return current;
    return device->createTexture(requested);
}
inline nvrhi::BufferHandle ReuseBuffer(nvrhi::IDevice *device, const nvrhi::BufferHandle &current,
                                       const nvrhi::BufferDesc &requested)
{
    auto key = [](const nvrhi::BufferDesc &d)
    {
        return std::make_tuple(d.byteSize, d.structStride, d.maxVersions, d.format, d.canHaveUAVs, d.canHaveTypedViews,
                               d.canHaveRawViews, d.isVertexBuffer, d.isIndexBuffer, d.isConstantBuffer,
                               d.isDrawIndirectArgs, d.isAccelStructBuildInput, d.isAccelStructStorage,
                               d.isShaderBindingTable, d.isVolatile, d.isVirtual, d.initialState, d.keepInitialState,
                               d.cpuAccess, d.sharedResourceFlags);
    };
    if (current && key(current->getDesc()) == key(requested))
        return current;
    return device->createBuffer(requested);
}
} // namespace avboit
