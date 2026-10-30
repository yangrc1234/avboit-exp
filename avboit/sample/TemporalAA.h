// SPDX-License-Identifier: MIT
#pragma once
#include <donut/engine/View.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/render/TemporalAntiAliasingPass.h>
#include <array>
#include <memory>

namespace avboit
{
// Full-resolution HDR TAA using the same Donut pass as the ADS demo.
// Camera-only motion is an approximation for translucent surfaces and volumes.
class TemporalAA
{
  public:
    TemporalAA(nvrhi::IDevice *device, nvrhi::ITexture *color, nvrhi::ITexture *depth);
    donut::math::float2 Jitter() const;
    void Invalidate() { valid = false; }
    void Record(nvrhi::ICommandList *commands, const std::array<std::array<float, 4>, 12> &camera);
    nvrhi::ITexture *Output() const { return resolved; }

  private:
    unsigned width, height;
    bool valid = false;
    nvrhi::TextureHandle resolved, motion, history[2];
    donut::engine::PlanarView current, previous, output;
    std::unique_ptr<donut::render::TemporalAntiAliasingPass> pass;
};
} // namespace avboit
