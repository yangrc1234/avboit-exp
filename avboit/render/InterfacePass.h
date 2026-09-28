// SPDX-License-Identifier: MIT
#pragma once
#include <nvrhi/nvrhi.h>
#include "GeometryDraw.h"
#include "MaterialShaders.h"
#include <vector>

namespace avboit
{
struct InterfaceInputs
{
    unsigned width = 0, height = 0;
    nvrhi::BufferHandle vertices, camera, materials, indices;
    nvrhi::BufferRange vertexRange;
    nvrhi::TextureHandle opaqueColorDepth;
    nvrhi::TextureHandle opaqueHardwareDepth;
    std::vector<MaterialShaders> materialShaders;
};

// One hardware-depth-selected special surface. Ordinary transparency is rejected
// by the material shader, so it cannot hide a special interface behind it.
class InterfacePass
{
  public:
    void Create(nvrhi::IDevice *device, const InterfaceInputs &inputs);
    void Record(nvrhi::ICommandList *commands, const std::vector<GeometryDraw> &draws) const;
    nvrhi::ITexture *TransmissionSigma() const { return m_transmissionSigma; }
    nvrhi::ITexture *Offset() const { return m_offset; }
    nvrhi::ITexture *HardwareDepth() const { return m_hardwareDepth; }

  private:
    nvrhi::TextureHandle m_transmissionSigma, m_offset, m_hardwareDepth;
    nvrhi::FramebufferHandle m_framebuffer;
    nvrhi::BindingLayoutHandle m_layout;
    nvrhi::BindingSetHandle m_bindings;
    std::vector<nvrhi::GraphicsPipelineHandle> m_pipelines;
    nvrhi::BufferHandle m_indices;
};
} // namespace avboit
