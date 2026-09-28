// SPDX-License-Identifier: MIT
#pragma once
#include "InterfacePass.h"
#include "ZeroTransmittance.h"
#include "ExtinctionVolume.h"
#include "OitResolve.h"
#include "GaussianPyramid.h"
#include "FrostComposition.h"
#include "DistortionPass.h"
#include "DistortionComposite.h"
#include <functional>

namespace avboit
{
struct TransparencyInputs
{
    bool applyVfx = false;
    unsigned frostMipCount = 3;
    unsigned width = 0, height = 0, volumeWidth = 0, volumeHeight = 0;
    unsigned objectCount = 0;
    unsigned depthBudget = 128;
    bool fixedDepth = false, hardwareDepth = false;
    bool halfAccumulation = false, fullVfx = true;
    nvrhi::BufferHandle vertices, bounds, camera, materials, interfaceIndices, geometryIndices;
    nvrhi::BufferRange transparentRange;
    nvrhi::TextureHandle opaqueColorDepth;
    nvrhi::TextureHandle opaqueHardwareDepth;
    nvrhi::SamplerHandle linear;
    std::vector<DistortionBatch> distortionBatches;
    std::vector<MaterialShaders> materialShaders;
};
struct TransparencyFrame
{
    bool denseExtinction = false, poisonExtinction = false;
    bool poisonResolve = false;
    bool sparseBlur = true, cubic = true;
    unsigned lastBlurMip = 5;
    bool zeroDepth = false;
    std::vector<GeometryDraw> interfaceDraws, geometryDraws;
    std::vector<unsigned> distortionBatches;
};

// No dependency on Donut scenes, UI, profiling implementations or presentation.
// The caller owns uploads and the command list; inputs must remain valid until
// GPU execution completes. Create reconnects consumers whenever resources change.
class TransparencyPipeline
{
  public:
    enum class Stage
    {
        Interface,
        Bounds,
        AdaptiveZ,
        PhysicalMask,
        ExtinctionClear,
        ExtinctionSplat,
        Integration,
        ZeroDepth,
        Accumulation,
        Resolve,
        BlurBase,
        Blur,
        Composition,
        Vfx,
        VfxApply
    };
    using ShaderLoader =
        std::function<nvrhi::ShaderHandle(const char *entry, nvrhi::ShaderType type, const char *file)>;
    using StageCallback = std::function<void(Stage, bool begin)>;
    void Create(nvrhi::IDevice *device, const TransparencyInputs &inputs, const ShaderLoader &shaders);
    void Record(nvrhi::ICommandList *commands, const TransparencyFrame &frame, const StageCallback &timing = {}) const;
    const ZeroTransmittance &Zero() const { return m_zero; }
    const InterfacePass &Interface() const { return m_interface; }
    const ExtinctionVolume &Volume() const { return m_volume; }
    const OitResolve &Resolve() const { return m_resolve; }
    const GaussianPyramid &Gaussian() const { return m_gaussian; }
    const FrostComposition &Composition() const { return m_composition; }
    const DistortionPass &Distortion() const { return m_distortion; }
    const DistortionComposite &DistortionApply() const { return m_distortionApply; }

  private:
    ZeroTransmittance m_zero;
    InterfacePass m_interface;
    ExtinctionVolume m_volume;
    OitResolve m_resolve;
    GaussianPyramid m_gaussian;
    FrostComposition m_composition;
    DistortionPass m_distortion;
    DistortionComposite m_distortionApply;
};
} // namespace avboit
