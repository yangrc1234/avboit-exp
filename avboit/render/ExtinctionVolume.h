// SPDX-License-Identifier: MIT
#pragma once
#include <nvrhi/nvrhi.h>
#include "GeometryDraw.h"
#include "MaterialShaders.h"
#include <vector>

namespace avboit
{
struct ExtinctionShaders
{
    nvrhi::ShaderHandle bounds, adaptiveDepth, physicalMask;
    nvrhi::ShaderHandle clear, integrate, integrateDense;
};
struct ExtinctionInputs
{
    unsigned width = 160, height = 96, objectCount = 0;
    unsigned screenWidth = 640, screenHeight = 384, depthBudget = 128;
    nvrhi::BufferHandle vertices, indices, bounds, camera, materials;
    nvrhi::BufferHandle frostFootprints, frostWorkMask, frostRectangles;
    nvrhi::BufferRange vertexRange;
    nvrhi::TextureHandle opaqueColorDepth;
    nvrhi::TextureHandle opaqueHardwareDepth;
    ExtinctionShaders shaders;
    std::vector<MaterialShaders> materialShaders;
};

// Owns AVBOIT's low-resolution representation; full-resolution accumulation is
// a consumer. Stage entry points keep external GPU timestamps outside the module.
class ExtinctionVolume
{
  public:
    void Create(nvrhi::IDevice *device, const ExtinctionInputs &inputs);
    void RecordBounds(nvrhi::ICommandList *commands) const;
    void RecordAdaptiveDepth(nvrhi::ICommandList *commands) const;
    void RecordPhysicalMask(nvrhi::ICommandList *commands) const;
    void RecordClear(nvrhi::ICommandList *commands, bool dense, bool poison) const;
    void RecordSplat(nvrhi::ICommandList *commands, const std::vector<GeometryDraw> &draws) const;
    void RecordIntegration(nvrhi::ICommandList *commands, bool dense) const;

    nvrhi::ITexture *ScreenTiles() const { return m_screenTiles; }
    nvrhi::IBuffer *Occupancy() const { return m_masks; }
    nvrhi::IBuffer *Warp() const { return m_warp; }
    nvrhi::IBuffer *PhysicalMask() const { return m_physicalMasks; }
    nvrhi::IBuffer *PackedExtinction() const { return m_extinction; }
    nvrhi::IBuffer *Overflow() const { return m_overflow; }
    nvrhi::ITexture *RasterTarget() const { return m_rasterTarget; }
    nvrhi::ITexture *ZeroSlice() const { return m_zeroSlice; }
    nvrhi::ITexture *Transmittance() const { return m_lut; }

  private:
    struct ComputePass
    {
        nvrhi::BindingLayoutHandle layout;
        nvrhi::BindingSetHandle bindings;
        nvrhi::ComputePipelineHandle pipeline;
        void Record(nvrhi::ICommandList *commands, unsigned x, unsigned y = 1) const;
    };
    void Barrier(nvrhi::ICommandList *commands) const;
    ComputePass m_boundsPass, m_warpPass, m_physicalPass, m_clearPass, m_integratePass, m_densePass;
    nvrhi::BufferHandle m_masks, m_warp, m_physicalMasks, m_extinction, m_overflow;
    nvrhi::TextureHandle m_lut, m_rasterTarget, m_zeroSlice, m_screenTiles;
    nvrhi::FramebufferHandle m_framebuffer;
    nvrhi::BindingLayoutHandle m_splatLayout;
    nvrhi::BindingSetHandle m_splatBindings;
    std::vector<nvrhi::GraphicsPipelineHandle> m_splatPipelines;
    nvrhi::BufferHandle m_indices;
    unsigned m_depthBudget = 128;
    unsigned m_width = 0, m_height = 0, m_tiles = 0, m_objects = 0;
};
} // namespace avboit
