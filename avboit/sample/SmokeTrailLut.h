// SPDX-License-Identifier: MIT
#pragma once
#include "SmokeTrails.h"
#include <nvrhi/nvrhi.h>

namespace avboit
{
// Conservative candidate index only: density and capsule boundaries stay analytic.
class SmokeTrailLut
{
  public:
    void Create(nvrhi::IDevice *device, nvrhi::IShader *shader);
    void Record(nvrhi::ICommandList *commands, SmokeTrails::GpuData data, SmokeTrails::Point low,
                SmokeTrails::Point high, bool reference = false, bool forceRebuild = false);
    nvrhi::BufferHandle capsules;
    nvrhi::TextureHandle candidates;

  private:
    SmokeTrails::GpuData m_geometry{};
    bool m_valid = false;
    nvrhi::BindingLayoutHandle m_layout;
    nvrhi::BindingSetHandle m_bindings;
    nvrhi::ComputePipelineHandle m_pipeline;
};
} // namespace avboit
