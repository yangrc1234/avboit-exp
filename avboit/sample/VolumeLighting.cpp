// SPDX-License-Identifier: MIT
#include "VolumeLighting.h"
#include "render/GpuScope.h"
#include <donut/core/math/math.h>
#include <nvrhi/utils.h>
#include <algorithm>

namespace avboit
{
void VolumeLighting::Create(nvrhi::IDevice *device, nvrhi::IShader *shader, nvrhi::ITexture *density,
                            nvrhi::ISampler *sampler, unsigned capacity, bool enabled)
{
    m_resolution = enabled && capacity ? 256 : 1;
    auto buffer = [&](unsigned size, const char *name)
    {
        return device->createBuffer(nvrhi::BufferDesc()
                                        .setByteSize(size)
                                        .setFormat(nvrhi::Format::RGBA32_FLOAT)
                                        .setCanHaveTypedViews(true)
                                        .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                        .setKeepInitialState(true)
                                        .setDebugName(name));
    };
    lights = buffer(sizeof(VolumeLightData), "Volume light transforms and radiance");
    m_volumes = buffer(std::max(capacity, 1u) * 48, "Volume shadow caster bounds and extinction");
    auto texture = [&](nvrhi::Format format, const char *name)
    {
        return device->createTexture(nvrhi::TextureDesc()
                                         .setWidth(m_resolution)
                                         .setHeight(m_resolution)
                                         .setArraySize(3)
                                         .setDimension(nvrhi::TextureDimension::Texture2DArray)
                                         .setFormat(format)
                                         .setIsUAV(true)
                                         .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                         .setKeepInitialState(true)
                                         .setDebugName(name));
    };
    moments = texture(nvrhi::Format::RGBA16_UNORM, "Volume shadow power moments (sun x2, spot)");
    opticalDepth = texture(nvrhi::Format::R16_FLOAT, "Volume shadow total optical depth");
    fallbackDepth = device->createTexture(nvrhi::TextureDesc()
                                              .setWidth(1)
                                              .setHeight(1)
                                              .setArraySize(2)
                                              .setDimension(nvrhi::TextureDimension::Texture2DArray)
                                              .setFormat(nvrhi::Format::D32)
                                              .setIsRenderTarget(true)
                                              .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                              .setKeepInitialState(true));
    if (!density)
    {
        m_fallbackDensity = device->createTexture(nvrhi::TextureDesc()
                                                      .setWidth(1)
                                                      .setHeight(1)
                                                      .setDepth(1)
                                                      .setDimension(nvrhi::TextureDimension::Texture3D)
                                                      .setFormat(nvrhi::Format::RG16_FLOAT)
                                                      .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                                      .setKeepInitialState(true));
        density = m_fallbackDensity;
    }
    using B = nvrhi::BindingSetItem;
    nvrhi::BindingSetDesc bindings;
    bindings.addItem(B::TypedBuffer_SRV(27, lights))
        .addItem(B::TypedBuffer_SRV(32, m_volumes))
        .addItem(B::Texture_SRV(26, density))
        .addItem(B::Sampler(0, sampler))
        .addItem(B::Texture_UAV(0, moments))
        .addItem(B::Texture_UAV(1, opticalDepth));
    m_layout = nullptr;
    m_bindings = nullptr;
    nvrhi::utils::CreateBindingSetAndLayout(device, nvrhi::ShaderType::Compute, 0, bindings, m_layout, m_bindings);
    m_pipeline =
        device->createComputePipeline(nvrhi::ComputePipelineDesc().setComputeShader(shader).addBindingLayout(m_layout));
}

void VolumeLighting::Record(nvrhi::ICommandList *commands, VolumeLightData data,
                            const std::vector<StressVolume> &volumes)
{
    using namespace donut::math;
    std::vector<std::array<float, 4>> casters;
    for (const auto &v : volumes)
    {
        casters.push_back({v.center[0], v.center[1], v.center[2], v.textured ? 1.f : 0.f});
        casters.push_back({v.extent[0], v.extent[1], v.extent[2], 0});
        casters.push_back({v.extinction[0], v.extinction[1], v.extinction[2], 0});
    }
    data[38][0] = float(volumes.size());
    // Test all world-space casters, including off-camera ones. Conservative
    // clip-plane rejection avoids missing shadows from volumes across a plane.
    for (unsigned slice = 0; slice < 3; ++slice)
    {
        bool overlaps = false;
        const auto *rows = data.data() + 13 + slice * 4;
        float4x4 matrix;
        for (unsigned r = 0; r < 4; ++r)
            for (unsigned c = 0; c < 4; ++c)
                matrix[r][c] = rows[r][c];
        for (const auto &v : volumes)
        {
            unsigned outside = 63;
            for (unsigned corner = 0; corner < 8; ++corner)
            {
                float4 p(v.center[0] + ((corner & 1) ? 1 : -1) * v.extent[0],
                         v.center[1] + ((corner & 2) ? 1 : -1) * v.extent[1],
                         v.center[2] + ((corner & 4) ? 1 : -1) * v.extent[2], 1);
                p = p * matrix;
                unsigned bits = (p.x < 0 ? 1 : 0) | (p.x > p.w ? 2 : 0) | (p.y < 0 ? 4 : 0) | (p.y > p.w ? 8 : 0) |
                                (p.z < 0 ? 16 : 0) | (p.z > p.w ? 32 : 0);
                outside &= bits;
            }
            overlaps |= outside == 0;
        }
        const bool illuminated = slice < 2 ? data[0][3] > 0 : data[1][3] > 0;
        data[37][slice] = overlaps && illuminated && data[4][2] > 0 && m_resolution > 1 ? 1.f : 0.f;
    }
    commands->writeBuffer(lights, data.data(), sizeof(data));
    if (!casters.empty())
        commands->writeBuffer(m_volumes, casters.data(), casters.size() * sizeof(casters[0]));
    if (m_resolution == 1 || !(data[37][0] || data[37][1] || data[37][2]))
        return;
    AVBOIT_GPU_SCOPE(commands, "Volumetric MSM / two sun cascades + spot");
    nvrhi::ComputeState state;
    state.pipeline = m_pipeline;
    state.bindings = {m_bindings};
    commands->setComputeState(state);
    commands->dispatch((m_resolution + 7) / 8, (m_resolution + 7) / 8, 3);
}
} // namespace avboit
