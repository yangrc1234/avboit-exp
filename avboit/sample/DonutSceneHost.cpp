// SPDX-License-Identifier: MIT
#include "DonutSceneHost.h"
#include <donut/core/vfs/VFS.h>
#include <donut/engine/Scene.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/TextureCache.h>
#include <donut/render/ForwardShadingPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/render/DepthPass.h>
#include <donut/render/PlanarShadowMap.h>
#include <stdexcept>
#include <donut/core/log.h>
#include <algorithm>
#include <tuple>
#include <unordered_map>

namespace avboit
{
using namespace donut;
struct DonutSceneHost::Impl
{
    std::shared_ptr<engine::ShaderFactory> shaders;
    std::shared_ptr<engine::CommonRenderPasses> common;
    std::shared_ptr<engine::TextureCache> textures;
    std::unique_ptr<engine::Scene> scene;
    std::unique_ptr<render::ForwardShadingPass> forward;
    engine::PlanarView view;
    std::shared_ptr<render::PlanarShadowMap> shadow;
    std::unique_ptr<render::DepthPass> shadowPass;
    nvrhi::FramebufferHandle shadowFramebuffer;
    bool shadowDirty = true;
    render::InstancedOpaqueDrawStrategy draws;
    render::PassthroughDrawStrategy stableDraws;
    std::vector<render::DrawItem> visible;
    std::unordered_map<const engine::MeshInstance *, unsigned> instanceOrder;
    std::unordered_map<const engine::MeshGeometry *, unsigned> geometryOrder;
    unsigned frame = 0;
    void SelectDraws(const engine::IView &view)
    {
        draws.PrepareForView(scene->GetSceneGraph()->GetRootNode(), view);
        visible.clear();
        while (const auto *item = draws.GetNextItem())
            visible.push_back(*item);
        std::stable_sort(visible.begin(), visible.end(),
                         [&](const auto &a, const auto &b)
                         {
                             return std::make_tuple(instanceOrder.at(a.instance), geometryOrder.at(a.geometry)) <
                                    std::make_tuple(instanceOrder.at(b.instance), geometryOrder.at(b.geometry));
                         });
        stableDraws.SetData(visible.data(), visible.size());
    }
};
DonutSceneHost::DonutSceneHost() : m_impl(std::make_unique<Impl>()) {}
DonutSceneHost::~DonutSceneHost() = default;

void DonutSceneHost::Load(nvrhi::IDevice *device, const std::filesystem::path &scenePath,
                          const std::filesystem::path &frameworkShaders)
{
    auto state = std::make_unique<Impl>();
    auto fs = std::make_shared<vfs::NativeFileSystem>();
    auto shaderFS = std::make_shared<vfs::RootFileSystem>();
    shaderFS->mount("/shaders/donut", frameworkShaders);
    state->shaders = std::make_shared<engine::ShaderFactory>(device, shaderFS, "/shaders");
    state->common = std::make_shared<engine::CommonRenderPasses>(device, state->shaders);
    state->textures = std::make_shared<engine::TextureCache>(device, fs, nullptr);
    state->scene = std::make_unique<engine::Scene>(device, *state->shaders, fs, state->textures, nullptr, nullptr);
    if (!std::filesystem::is_regular_file(scenePath))
        throw std::runtime_error("Sponza not found: " + scenePath.string() +
                                 ". Run python tools/fetch_sponza.py, or pass --sponza-path <Sponza.gltf>.");
    if (!state->scene->Load(scenePath))
        throw std::runtime_error("Donut scene load failed");
    state->textures->ProcessRenderingThreadCommands(*state->common, 0.f);
    state->textures->LoadingFinished();
    auto graph = state->scene->GetSceneGraph();
    auto root = graph->GetRootNode();
    // Match the existing Sponza fixture placement: (x,y,z) -> (-z,y,x+8).
    root->SetRotation(math::dquat(0.7071067811865476, 0, -0.7071067811865476, 0));
    root->SetTranslation(math::double3(0, 0, 8));
    auto sun = std::make_shared<engine::DirectionalLight>();
    graph->AttachLeafNode(root, sun);
    sun->SetDirection(math::double3(.4, -1, .25));
    sun->irradiance = 2.f;
    state->scene->FinishedLoading(0);
    // Scene global IDs are assigned from unordered registries; use graph traversal
    // order instead for reproducible draws of coplanar calibration geometry.
    for (engine::SceneGraphWalker walker(root.get()); walker; walker.Next(true))
    {
        if (auto *instance = dynamic_cast<engine::MeshInstance *>(walker->GetLeaf().get()))
        {
            state->instanceOrder.emplace(instance, unsigned(state->instanceOrder.size()));
            for (const auto &geometry : instance->GetMesh()->geometries)
                state->geometryOrder.emplace(geometry.get(), unsigned(state->geometryOrder.size()));
        }
    }
    state->forward = std::make_unique<render::ForwardShadingPass>(device, state->common);
    state->forward->Init(*state->shaders, render::ForwardShadingPass::CreateParameters());
    state->shadow = std::make_shared<render::PlanarShadowMap>(device, 2048, nvrhi::Format::D32);
    state->shadow->SetupWholeSceneDirectionalLightView(*sun, root->GetGlobalBoundingBox());
    state->shadow->SetLitOutOfBounds(true);
    sun->shadowMap = state->shadow;
    state->shadowFramebuffer =
        device->createFramebuffer(nvrhi::FramebufferDesc().setDepthAttachment(state->shadow->GetTexture()));
    state->shadowPass = std::make_unique<render::DepthPass>(device, state->common);
    render::DepthPass::CreateParameters shadowParams;
    shadowParams.depthBias = 100;
    shadowParams.slopeScaledDepthBias = 2.f;
    state->shadowPass->Init(*state->shaders, shadowParams);
    m_impl = std::move(state);
    log::info("Donut scene resources loaded (retained across transparency quality changes)");
}

void DonutSceneHost::Record(nvrhi::ICommandList *commands, nvrhi::IFramebuffer *target,
                            const std::array<std::array<float, 4>, 10> &camera)
{
    auto &state = *m_impl;
    auto v = [&](unsigned index) { return math::float3(camera[index][0], camera[index][1], camera[index][2]); };
    const auto eye = v(0), right = v(1), up = v(2), forward = v(3);
    auto worldToView = math::affine3::from_cols(
        right, up, forward, math::float3(-math::dot(eye, right), -math::dot(eye, up), -math::dot(eye, forward)));
    const auto &info = target->getFramebufferInfo();
    state.view.SetViewport(info.getViewport());
    state.view.SetMatrices(worldToView,
                           math::perspProjD3DStyle(math::radians(60.f), float(info.width) / info.height, .05f, 80.f));
    state.view.UpdateCache();
    state.scene->Refresh(commands, ++state.frame);
    render::ForwardShadingPass::Context context;
    state.forward->PrepareLights(context, commands, state.scene->GetSceneGraph()->GetLights(), math::float3(.12f),
                                 math::float3(.035f), {});
    state.SelectDraws(state.view);
    render::RenderView(commands, &state.view, nullptr, target, state.stableDraws, *state.forward, context);
}

void DonutSceneHost::RecordShadow(nvrhi::ICommandList *commands, bool forceRedraw)
{
    auto &state = *m_impl;
    if (!state.shadowDirty && !forceRedraw)
        return;
    commands->clearDepthStencilTexture(state.shadow->GetTexture(), nvrhi::AllSubresources, true, 1.f, false, 0);
    auto view = state.shadow->GetPlanarView();
    state.SelectDraws(*view);
    render::DepthPass::Context context;
    render::RenderView(commands, view.get(), nullptr, state.shadowFramebuffer, state.stableDraws, *state.shadowPass,
                       context);
    state.shadowDirty = false;
}
nvrhi::ITexture *DonutSceneHost::ShadowTexture() const
{
    return m_impl->shadow->GetTexture();
}
} // namespace avboit
