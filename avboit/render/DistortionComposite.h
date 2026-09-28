// SPDX-License-Identifier: MIT
#pragma once
#include <nvrhi/nvrhi.h>

namespace avboit
{
struct DistortionCompositeInputs
{
    bool enabled = false;
    nvrhi::TextureHandle sceneColor, offset;
    nvrhi::BufferHandle camera;
    nvrhi::SamplerHandle linear;
    nvrhi::ShaderHandle vertex, pixel;
};
// HDR scene-color warp, independent of presentation, exposure and tone mapping.
// Reconfigure after retiring GPU work; disabling releases the intermediate RT.
class DistortionComposite
{
  public:
    void Create(nvrhi::IDevice *device, const DistortionCompositeInputs &inputs);
    void Record(nvrhi::ICommandList *commands) const;
    bool Enabled() const { return bool(m_output); }
    nvrhi::ITexture *Output() const { return m_output ? m_output.Get() : m_source.Get(); }

  private:
    nvrhi::TextureHandle m_source, m_output;
    nvrhi::FramebufferHandle m_framebuffer;
    nvrhi::BindingLayoutHandle m_layout;
    nvrhi::BindingSetHandle m_bindings;
    nvrhi::GraphicsPipelineHandle m_pipeline;
};
} // namespace avboit
