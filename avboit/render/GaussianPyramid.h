// SPDX-License-Identifier: MIT
#pragma once
#include <nvrhi/nvrhi.h>
#include <vector>
#include "FrostChain.h"
#include "TileSchedule.h"
#include "GpuScope.h"

namespace avboit
{
struct GaussianInputs
{
    unsigned width = 0, height = 0;
    unsigned mipCount = 3;
    nvrhi::TextureHandle background, opaque, screenTiles;
    nvrhi::BufferHandle camera;
    nvrhi::SamplerHandle linearSampler;
    nvrhi::ShaderHandle sparseShader, fusedShader;
};

// Bounds raster marks producer masks using the precomputed footprint table.
// Gaussian dispatches its fixed grid and rejects empty groups before filtering.
class GaussianPyramid
{
  public:
    static constexpr unsigned MipCount = FrostChain::MaxMips;
    void AllocateSchedule(nvrhi::IDevice *device, unsigned width, unsigned height, unsigned mipCount);
    void Create(nvrhi::IDevice *device, const GaussianInputs &inputs);
    void ResetSchedule(nvrhi::ICommandList *commands, bool cubic) const;
    void RecordBlur(nvrhi::ICommandList *commands, unsigned lastMip, bool poison = false,
                    const GpuTimingRoute &baseTiming = {}, const GpuTimingRoute &mipTiming = {}) const;
    nvrhi::ITexture *Output() const { return m_pyramid; }
    nvrhi::IBuffer *Rectangles() const { return m_rectangles; }
    nvrhi::IBuffer *WorkMask() const { return m_workMask; }
    const TileSchedule &ScheduleLayout() const { return m_schedule; }
    nvrhi::BufferRange WorkRange(unsigned stage) const
    {
        return nvrhi::BufferRange((m_schedule.offset[stage] + 1) * 4, m_schedule.capacity[stage] * 4);
    }
    nvrhi::IBuffer *Footprints() const { return m_footprints; }

  private:
    struct Pass
    {
        nvrhi::BindingLayoutHandle layout;
        nvrhi::BindingSetHandle bindings;
        nvrhi::ComputePipelineHandle pipeline;
    };
    nvrhi::TextureHandle m_pyramid;
    nvrhi::BufferHandle m_rectangles, m_workMask, m_footprints;
    TileSchedule m_schedule;
    std::vector<Pass> m_fused;
    Pass m_sparseBase;
    unsigned m_width = 0, m_height = 0;
    unsigned m_mipCount = 3;
    unsigned m_screenWidth = 0, m_screenHeight = 0;
    mutable bool m_footprintsDirty = true, m_lastCubic = false;
};
} // namespace avboit
