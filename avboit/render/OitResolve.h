// SPDX-License-Identifier: MIT
#pragma once
#include <nvrhi/nvrhi.h>
#include "GeometryDraw.h"
#include "GpuScope.h"
#include "MaterialShaders.h"
#include <vector>
#include <array>

namespace avboit
{
struct OitResolveInputs
{
    unsigned width = 0, height = 0;
    bool marchHeatmap = false;
    nvrhi::ShaderHandle vdbMarchCompute;
    unsigned vdbRaySpacing = 1;
    bool halfAccumulation = false;
    bool lowResolutionVolumes = true;
    nvrhi::BufferHandle vertices, indices, camera, materials, warp;
    nvrhi::BufferRange vertexRange;
    nvrhi::TextureHandle lut, opaque, interfaceDepth, interfaceSurface, interfaceOffset, screenTiles;
    nvrhi::TextureHandle opaqueHardwareDepth, volumeDensity, gameplaySmokeMask;
    nvrhi::BufferHandle smokeBricks, smokeTrails, smokeSampling;
    nvrhi::TextureHandle smokeTrailCandidates;
    nvrhi::BufferHandle volumeLights;
    nvrhi::TextureHandle regularLightDepth, lightMoments, lightOpticalDepth;
    nvrhi::SamplerHandle linear;
    nvrhi::ShaderHandle backgroundVertex, backgroundPixel, upsamplePixel;
    std::vector<MaterialShaders> materialShaders;
};

struct AccumulationTiming
{
    GpuTimingRoute clear, lowResolution, upsample, fullResolution;
};

// Four-RT accumulation followed by a refracted background cache on transparency tiles.
// Final normalization/composition is deferred until the Gaussian input is ready.
class OitResolve
{
  public:
    void Create(nvrhi::IDevice *device, const OitResolveInputs &inputs);
    void RecordAccumulation(nvrhi::ICommandList *commands, const std::vector<GeometryDraw> &draws,
                            const AccumulationTiming &timing = {}) const;
    void RecordResolve(nvrhi::ICommandList *commands, bool poison = false) const;
    nvrhi::ITexture *MarchCounters() const { return m_lowTargets[0] ? m_lowTargets[0].Get() : m_numerator.Get(); }
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
    Pass m_accumulation, m_backgroundPass, m_lowAccumulation, m_upsample;
    std::array<nvrhi::TextureHandle, 4> m_lowTargets;
    std::vector<nvrhi::GraphicsPipelineHandle> m_lowPipelines;
    nvrhi::BufferHandle m_indices;
    std::vector<nvrhi::GraphicsPipelineHandle> m_accumulationPipelines;
    nvrhi::ComputePipelineHandle m_marchPipeline;
    nvrhi::BindingLayoutHandle m_marchLayout;
    nvrhi::BindingSetHandle m_marchBindings;
    std::vector<bool> m_computeMaterials;
    bool m_marchHeatmap = false;
    unsigned m_screenTileCount = 0;
    unsigned m_vdbRaySpacing = 1;
};
} // namespace avboit
