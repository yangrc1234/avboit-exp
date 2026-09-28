// SPDX-License-Identifier: MIT
#pragma once
#include <nvrhi/nvrhi.h>
#include <array>
#include <filesystem>
#include <memory>

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
    nvrhi::ITexture *ShadowTexture() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace avboit
