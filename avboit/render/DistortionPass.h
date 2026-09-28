// SPDX-License-Identifier: MIT
#pragma once
#include <nvrhi/nvrhi.h>
#include <vector>
#include "MaterialShaders.h"

namespace avboit
{

// A vertex-pulled material batch. The host supplies ranges, never fixture IDs.
struct DistortionBatch
{
    nvrhi::BufferRange vertices;
    unsigned vertexCount = 0;
    unsigned material = 0;
};

struct DistortionInputs
{
    unsigned width = 0, height = 0;
    nvrhi::BufferHandle vertices, camera, materials, depthWarp;
    nvrhi::TextureHandle transmittance, opaqueColorDepth, interfaceDepth;
    nvrhi::TextureHandle opaqueHardwareDepth;
    nvrhi::SamplerHandle linearSampler;
    std::vector<MaterialShaders> materialShaders;
    std::vector<DistortionBatch> batches;
};

// Owns only displacement production. Composition and presentation are consumers.
class DistortionPass
{
  public:
    void Create(nvrhi::IDevice *device, const DistortionInputs &inputs);
    // Records into an already open list; never submits or waits. Clearing is
    // required even with no batches, since downstream composition always reads RG.
    void Record(nvrhi::ICommandList *commands, const std::vector<unsigned> &batches) const;
    nvrhi::ITexture *Output() const { return m_offset; }

  private:
    nvrhi::TextureHandle m_offset;
    nvrhi::FramebufferHandle m_framebuffer;
    nvrhi::BindingLayoutHandle m_layout;
    std::vector<nvrhi::GraphicsPipelineHandle> m_pipelines;
    std::vector<nvrhi::BindingSetHandle> m_bindings;
    std::vector<unsigned> m_vertexCounts;
    std::vector<unsigned> m_materials;
};
} // namespace avboit
