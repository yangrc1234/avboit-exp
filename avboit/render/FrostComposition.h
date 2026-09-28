// SPDX-License-Identifier: MIT
#pragma once
#include <nvrhi/nvrhi.h>

namespace avboit
{
struct FrostCompositionInputs
{
    unsigned width = 0, height = 0;
    nvrhi::BufferHandle warp;
    nvrhi::TextureHandle lut, sceneColor, screenTiles, opaqueDepth, numerator, denominator, totalTau, backNumerator;
    nvrhi::TextureHandle interfaceDepth, interfaceSurface, interfaceOffset;
    nvrhi::TextureHandle backgroundPyramid, sharpBackground;
    nvrhi::BufferHandle camera, materials;
    nvrhi::SamplerHandle linear;
    nvrhi::ShaderHandle vertexShader, pixelShader, copyShader;
};
class FrostComposition
{
  public:
    void Create(nvrhi::IDevice *device, const FrostCompositionInputs &inputs);
    void Record(nvrhi::ICommandList *commands, bool poison = false) const;
    nvrhi::ITexture *Output() const { return m_output; }
    nvrhi::ITexture *Temporary() const { return m_temporary; }

  private:
    unsigned m_tileCount = 0;
    nvrhi::TextureHandle m_output, m_temporary;
    nvrhi::FramebufferHandle m_framebuffer;
    nvrhi::BindingLayoutHandle m_layout;
    nvrhi::BindingSetHandle m_bindings;
    nvrhi::GraphicsPipelineHandle m_pipeline;
    nvrhi::FramebufferHandle m_copyFramebuffer;
    nvrhi::BindingLayoutHandle m_copyLayout;
    nvrhi::BindingSetHandle m_copyBindings;
    nvrhi::GraphicsPipelineHandle m_copyPipeline;
};
} // namespace avboit
