// SPDX-License-Identifier: MIT
#pragma once
#include "StressFixtures.h"
#include <nvrhi/nvrhi.h>
#include <array>
#include <vector>

namespace avboit
{
// Shared with VolumeLighting.hlsli. Matrices are row-major world-to-UVZW,
// followed by their inverses. Shadow rays use near=1, far=0 (Reversed-Z).
using VolumeLightData = std::array<std::array<float, 4>, 39>;
struct SceneLightingSettings
{
    bool animateSpot = true, volumeShadows = true;
    float sunIntensity = 2.f, spotIntensity = 80.f;
    std::array<float, 3> sunDirection = {.08f, -1.f, .25f};
    std::array<float, 3> spotPosition = {-2.f, 3.6f, 2.5f};
    std::array<float, 3> spotTarget = {0.f, 1.75f, 4.5f};
};

// The sample owns light-space resources independently of the sparse camera OIT LUT.
class VolumeLighting
{
  public:
    void Create(nvrhi::IDevice *device, nvrhi::IShader *shader, nvrhi::ITexture *density, nvrhi::ISampler *sampler,
                unsigned volumeCapacity, bool enabled);
    void Record(nvrhi::ICommandList *commands, VolumeLightData data, const std::vector<StressVolume> &volumes);
    nvrhi::BufferHandle lights;
    nvrhi::TextureHandle moments, opticalDepth, fallbackDepth;

  private:
    nvrhi::BufferHandle m_volumes;
    nvrhi::TextureHandle m_fallbackDensity;
    nvrhi::BindingLayoutHandle m_layout;
    nvrhi::BindingSetHandle m_bindings;
    nvrhi::ComputePipelineHandle m_pipeline;
    unsigned m_resolution = 1;
};
} // namespace avboit
