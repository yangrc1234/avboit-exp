// SPDX-License-Identifier: MIT
#pragma once
#include <nvrhi/nvrhi.h>
#include <array>
#include <filesystem>
#include <memory>
#include "VolumeLighting.h"

namespace avboit
{
// Scene loading and opaque rendering belong to the sample, not avboit_render.
class DonutSceneHost
{
  public:
    DonutSceneHost();
    ~DonutSceneHost();
    void Load(nvrhi::IDevice *device, const std::filesystem::path &scenePath,
              const std::filesystem::path &frameworkShaders);
    void Record(nvrhi::ICommandList *commands, nvrhi::IFramebuffer *target,
                const std::array<std::array<float, 4>, 10> &camera);
    void RecordShadow(nvrhi::ICommandList *commands, bool forceRedraw = false);
    void UpdateLights(float time, const std::array<float, 3> &eye, const SceneLightingSettings &settings);
    const VolumeLightData &LightingData() const;
    void SetVolumeLighting(const VolumeLighting &lighting);
    nvrhi::ITexture *ShadowTexture() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace avboit
