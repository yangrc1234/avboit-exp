// SPDX-License-Identifier: MIT
#pragma once
#include <nvrhi/nvrhi.h>
namespace avboit
{
struct ZeroTransmittanceInputs
{
    unsigned width = 0, height = 0;
    nvrhi::TextureHandle opaqueDepth, zeroSlice;
    nvrhi::BufferHandle camera, warp;
    nvrhi::ShaderHandle vertex;
};
// Writes directly into host SceneDepth. Resolve closes rays whose filtered LUT
// is RGB zero at that depth; no stencil bit or separate cutoff texture is used.
class ZeroTransmittance
{
  public:
    void Create(nvrhi::IDevice *device, const ZeroTransmittanceInputs &inputs);
    void RecordDepth(nvrhi::ICommandList *commands, bool enabled) const;
    nvrhi::ITexture *Depth() const { return m_depth; }

  private:
    unsigned m_tileCount = 0;
    nvrhi::TextureHandle m_depth;
    nvrhi::BindingLayoutHandle m_drawLayout;
    nvrhi::BindingSetHandle m_drawBindings;
    nvrhi::GraphicsPipelineHandle m_draw;
    nvrhi::FramebufferHandle m_framebuffer;
};
} // namespace avboit
