// SPDX-License-Identifier: MIT
#include "sample/TemporalAA.h"
#include <donut/app/ApplicationBase.h>
#include <donut/core/vfs/VFS.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/ShaderFactory.h>

namespace avboit
{
using namespace donut;
TemporalAA::TemporalAA(nvrhi::IDevice *device, nvrhi::ITexture *color, nvrhi::ITexture *depth)
    : width(color->getDesc().width), height(color->getDesc().height)
{
    auto make = [&](const char *name, nvrhi::Format format, bool uav)
    {
        return device->createTexture(nvrhi::TextureDesc()
                                         .setWidth(width)
                                         .setHeight(height)
                                         .setFormat(format)
                                         .setDebugName(name)
                                         .setIsUAV(uav)
                                         .setIsRenderTarget(!uav)
                                         .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                         .setKeepInitialState(true));
    };
    resolved = make("TAA resolved HDR", nvrhi::Format::RGBA16_FLOAT, true);
    history[0] = make("TAA history 0", nvrhi::Format::RGBA16_FLOAT, true);
    history[1] = make("TAA history 1", nvrhi::Format::RGBA16_FLOAT, true);
    motion = make("TAA camera motion", nvrhi::Format::RG16_FLOAT, false);
    current.SetViewport(nvrhi::Viewport(float(width), float(height)));
    current.SetMatrices(math::affine3::identity(), math::float4x4::identity());
    current.UpdateCache();
    output = previous = current;
    auto fs = std::make_shared<vfs::RootFileSystem>();
    fs->mount("/shaders/donut", app::GetDirectoryWithExecutable() / "shaders/framework/dxil");
    auto factory = std::make_shared<engine::ShaderFactory>(device, fs, "/shaders");
    auto common = std::make_shared<engine::CommonRenderPasses>(device, factory);
    render::TemporalAntiAliasingPass::CreateParameters parameters;
    parameters.sourceDepth = depth;
    parameters.unresolvedColor = color;
    parameters.resolvedColor = resolved;
    parameters.motionVectors = motion;
    parameters.feedback1 = history[0];
    parameters.feedback2 = history[1];
    pass = std::make_unique<render::TemporalAntiAliasingPass>(device, factory, common, current, parameters);
    pass->SetJitter(render::TemporalAntiAliasingJitter::Halton);
}
math::float2 TemporalAA::Jitter() const
{
    return pass->GetCurrentPixelOffset();
}
void TemporalAA::Record(nvrhi::ICommandList *commands, const std::array<std::array<float, 4>, 12> &camera)
{
    auto vector = [&](unsigned row) { return math::float3(camera[row][0], camera[row][1], camera[row][2]); };
    const auto eye = vector(0), right = vector(1), up = vector(2), forward = vector(3);
    auto worldToView =
        math::affine3::from_cols(right, up, forward, math::float3(-dot(eye, right), -dot(eye, up), -dot(eye, forward)));
    float yScale = 1.f / .57735027f, xScale = yScale * float(height) / width;
    current.SetMatrices(worldToView,
                        math::float4x4(xScale, 0, 0, 0, 0, yScale, 0, 0, 0, 0, camera[5][0], 1, 0, 0, camera[5][1], 0));
    current.SetPixelOffset(math::float2(camera[11][0], camera[11][1]));
    current.UpdateCache();
    // Abrupt camera changes must not blend unrelated views. Ordinary motion reprojects.
    auto delta = eye - previous.GetViewOrigin();
    if (dot(delta, delta) > 1.f || dot(forward, previous.GetViewDirection()) < .9f)
        valid = false;
    if (!valid)
    {
        previous = current;
        commands->clearTextureFloat(history[0], nvrhi::AllSubresources, nvrhi::Color(0.f));
        commands->clearTextureFloat(history[1], nvrhi::AllSubresources, nvrhi::Color(0.f));
    }
    pass->RenderMotionVectors(commands, current, previous);
    render::TemporalAntiAliasingParameters parameters;
    pass->TemporalResolve(commands, parameters, valid, current, output);
    previous = current;
    valid = true;
    pass->AdvanceFrame();
}
} // namespace avboit
