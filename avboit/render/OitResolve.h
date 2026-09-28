// SPDX-License-Identifier: MIT
#pragma once
#include <nvrhi/nvrhi.h>
#include "GeometryDraw.h"
#include "MaterialShaders.h"
#include <vector>

namespace avboit
{
struct OitResolveInputs
{
    unsigned width = 0, height = 0;
    bool halfAccumulation = false;
    nvrhi::BufferHandle vertices, indices, camera, materials, warp;
    nvrhi::BufferRange vertexRange;
    nvrhi::TextureHandle lut, opaque, interfaceDepth, interfaceSurface, screenTiles;
    nvrhi::TextureHandle opaqueHardwareDepth;
    nvrhi::SamplerHandle linear;
    nvrhi::ShaderHandle backgroundVertex, backgroundPixel;
    std::vector<MaterialShaders> materialShaders;
};

// Four-RT accumulation followed by a background cache on transparency tiles.
// Final normalization/composition is deferred until the Gaussian input is ready.
class OitResolve
{
  public:
    void Create(nvrhi::IDevice *device, const OitResolveInputs &inputs);
    void RecordAccumulation(nvrhi::ICommandList *commands, const std::vector<GeometryDraw> &draws) const;
    void RecordResolve(nvrhi::ICommandList *commands, bool poison = false) const;
    nvrhi::ITexture *Numerator() const { return m_numerator; }
    nvrhi::ITexture *Denominator() const { return m_denominator; }
    nvrhi::ITexture *TotalTau() const { return m_totalTau; }
    nvrhi::ITexture *BackNumerator() const { return m_backNumerator; }
    nvrhi::ITexture *Background() const { return m_background; }

  private:
    struct Pass
    {
        nvrhi::BindingLayoutHandle layout;
        nvrhi::BindingSetHandle bindings;
        nvrhi::GraphicsPipelineHandle pipeline;
        nvrhi::FramebufferHandle framebuffer;
        void Record(nvrhi::ICommandList *commands, unsigned vertices, unsigned instances = 1) const;
    };
    nvrhi::TextureHandle m_numerator, m_denominator, m_totalTau, m_backNumerator;
    nvrhi::TextureHandle m_background;
    Pass m_accumulation, m_backgroundPass;
    nvrhi::BufferHandle m_indices;
    std::vector<nvrhi::GraphicsPipelineHandle> m_accumulationPipelines;
    unsigned m_backgroundTiles = 0;
};
} // namespace avboit
