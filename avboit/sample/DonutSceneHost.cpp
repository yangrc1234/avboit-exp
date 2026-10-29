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
#include "render/GpuScope.h"
#include <cmath>
using namespace donut::math;
#include <donut/shaders/forward_cb.h>

namespace avboit
{
using namespace donut;
namespace
{
// Lights live under the rotated Sponza root. Compose local-to-parent in row
// vector order: desired local-to-world * world-to-parent. Donut's convenience
// SetDirection assumes a different parent composition order for this case.
void SetWorldLightDirection(engine::Light &light, math::double3 direction)
{
    auto node = light.GetNode();
    const auto parentToWorld =
        node->GetParent() ? node->GetParent()->GetLocalToWorldTransform() : math::daffine3::identity();
    auto localToParent = math::inverse(math::lookatZ(direction)) * math::inverse(parentToWorld);
    math::dquat rotation;
    math::double3 scale;
    math::decomposeAffine<double>(localToParent, nullptr, &rotation, &scale);
    node->SetTransform(nullptr, &rotation, &scale);
}
// Keep Donut's material/lighting pass, with the sample's Reversed-Z shadow ABI.
class SceneShadingPass : public render::ForwardShadingPass
{
  public:
    using ForwardShadingPass::ForwardShadingPass;
    nvrhi::BufferHandle volumeLights;
    nvrhi::TextureHandle volumeMoments, volumeTau;
    void Init(engine::ShaderFactory &factory, const CreateParameters &params) override
    {
        ForwardShadingPass::Init(factory, params);
        m_ShadowSampler = m_Device->createSampler(
            nvrhi::SamplerDesc().setAllAddressModes(nvrhi::SamplerAddressMode::Border).setBorderColor(0.f));
    }

  protected:
    nvrhi::BindingLayoutHandle CreateShadingBindingLayout() override
    {
        auto base = ForwardShadingPass::CreateShadingBindingLayout();
        auto desc = *base->getDesc();
        desc.addItem(nvrhi::BindingLayoutItem::TypedBuffer_SRV(27))
            .addItem(nvrhi::BindingLayoutItem::Texture_SRV(29))
            .addItem(nvrhi::BindingLayoutItem::Texture_SRV(31))
            .addItem(nvrhi::BindingLayoutItem::Sampler(10));
        return m_Device->createBindingLayout(desc);
    }
    nvrhi::BindingSetHandle CreateShadingBindingSet(nvrhi::ITexture *shadowMap, nvrhi::ITexture *diffuse,
                                                    nvrhi::ITexture *specular, nvrhi::ITexture *brdf) override
    {
        using B = nvrhi::BindingSetItem;
        nvrhi::BindingSetDesc d;
        d.addItem(B::ConstantBuffer(FORWARD_BINDING_LIGHT_CONSTANTS, m_ForwardLightCB))
            .addItem(B::Texture_SRV(FORWARD_BINDING_SHADOW_MAP_TEXTURE, shadowMap))
            .addItem(B::Texture_SRV(FORWARD_BINDING_DIFFUSE_LIGHT_PROBE_TEXTURE,
                                    diffuse ? diffuse : m_CommonPasses->m_BlackCubeMapArray.Get()))
            .addItem(B::Texture_SRV(FORWARD_BINDING_SPECULAR_LIGHT_PROBE_TEXTURE,
                                    specular ? specular : m_CommonPasses->m_BlackCubeMapArray.Get()))
            .addItem(B::Texture_SRV(FORWARD_BINDING_ENVIRONMENT_BRDF_TEXTURE,
                                    brdf ? brdf : m_CommonPasses->m_BlackTexture.Get()))
            .addItem(B::Sampler(FORWARD_BINDING_MATERIAL_SAMPLER, m_CommonPasses->m_AnisotropicWrapSampler))
            .addItem(B::Sampler(FORWARD_BINDING_SHADOW_MAP_SAMPLER, m_ShadowSampler))
            .addItem(B::Sampler(FORWARD_BINDING_LIGHT_PROBE_SAMPLER, m_CommonPasses->m_LinearWrapSampler))
            .addItem(B::Sampler(FORWARD_BINDING_ENVIRONMENT_BRDF_SAMPLER, m_CommonPasses->m_LinearClampSampler))
            .addItem(B::TypedBuffer_SRV(27, volumeLights))
            .addItem(B::Texture_SRV(29, volumeMoments))
            .addItem(B::Texture_SRV(31, volumeTau))
            .addItem(B::Sampler(10, m_CommonPasses->m_LinearClampSampler));
        return m_Device->createBindingSet(d, m_ShadingBindingLayout);
    }
    nvrhi::ShaderHandle CreatePixelShader(engine::ShaderFactory &factory, const CreateParameters &,
                                          bool transmissive) override
    {
        const std::vector<engine::ShaderMacro> macros = {{"TRANSMISSIVE_MATERIAL", transmissive ? "1" : "0"}};
        auto shader =
            factory.CreateShader("avboit/shaders/SceneForward.hlsl", "main", &macros, nvrhi::ShaderType::Pixel);
        if (!shader)
            throw std::runtime_error("Scene shading shader missing");
        return shader;
    }
};
} // namespace
struct DonutSceneHost::Impl
{
    std::shared_ptr<engine::ShaderFactory> shaders;
    std::shared_ptr<engine::CommonRenderPasses> common;
    std::shared_ptr<engine::TextureCache> textures;
    std::unique_ptr<engine::Scene> scene;
    std::unique_ptr<SceneShadingPass> forward;
    engine::PlanarView view;
    std::shared_ptr<render::PlanarShadowMap> shadow;
    std::shared_ptr<render::PlanarShadowMap> spotShadow;
    std::array<std::shared_ptr<render::PlanarShadowMap>, 2> volumeCascades;
    std::shared_ptr<engine::DirectionalLight> sun;
    std::shared_ptr<engine::SpotLight> spot;
    nvrhi::FramebufferHandle spotFramebuffer;
    VolumeLightData lightData{};
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
    shaderFS->mount("/shaders/avboit",
                    frameworkShaders.parent_path().parent_path() / "avboit" / frameworkShaders.filename());
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
    graph->Refresh(0);
    auto sun = std::make_shared<engine::DirectionalLight>();
    state->sun = sun;
    graph->AttachLeafNode(root, sun);
    SetWorldLightDirection(*sun, math::double3(.08, -1, .25));
    sun->irradiance = 2.f;
    state->spot = std::make_shared<engine::SpotLight>();
    graph->AttachLeafNode(root, state->spot);
    // Donut evaluates these as angles from the cone axis (half angles).
    // Match the 65-degree full-FOV shadow projection and volume cone below.
    state->spot->innerAngle = 21.f;
    state->spot->outerAngle = 32.5f;
    state->spot->range = 25.f;
    state->spot->color = math::float3(1.f, .85f, .65f);
    state->spot->intensity = 80.f;
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
    state->forward = std::make_unique<SceneShadingPass>(device, state->common);
    state->forward->Init(*state->shaders, render::ForwardShadingPass::CreateParameters());
    auto shadowTexture = device->createTexture(nvrhi::TextureDesc()
                                                   .setWidth(2048)
                                                   .setHeight(2048)
                                                   .setFormat(nvrhi::Format::D32)
                                                   .setDimension(nvrhi::TextureDimension::Texture2DArray)
                                                   .setArraySize(2)
                                                   .setIsTypeless(true)
                                                   .setIsRenderTarget(true)
                                                   .setClearValue(nvrhi::Color(0))
                                                   .setUseClearValue(true)
                                                   .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                                   .setKeepInitialState(true)
                                                   .setDebugName("Scene shadow depth"));
    state->shadow =
        std::make_shared<render::PlanarShadowMap>(device, shadowTexture, 0, nvrhi::Viewport(2048.f, 2048.f));
    state->shadow->SetupWholeSceneDirectionalLightView(*sun, root->GetGlobalBoundingBox());
    auto shadowView = state->shadow->GetPlanarView();
    auto shadowProjection = shadowView->GetProjectionMatrix(false);
    // Preserve Donut's fitted light bounds while mapping near to 1 and far to 0.
    for (unsigned row = 0; row < 4; ++row)
        shadowProjection[row][2] = shadowProjection[row][3] - shadowProjection[row][2];
    shadowView->SetMatrices(shadowView->GetViewMatrix(), shadowProjection);
    shadowView->UpdateCache();
    state->shadow->SetLitOutOfBounds(true);
    sun->shadowMap = state->shadow;
    state->shadowFramebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().setDepthAttachment(
        nvrhi::FramebufferAttachment().setTexture(shadowTexture).setArraySlice(0)));
    state->spotShadow =
        std::make_shared<render::PlanarShadowMap>(device, shadowTexture, 1, nvrhi::Viewport(2048.f, 2048.f));
    state->spotShadow->SetLitOutOfBounds(true);
    state->spot->shadowMap = state->spotShadow;
    state->spotFramebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().setDepthAttachment(
        nvrhi::FramebufferAttachment().setTexture(shadowTexture).setArraySlice(1)));
    for (auto &cascade : state->volumeCascades)
        cascade = std::make_shared<render::PlanarShadowMap>(device, shadowTexture, 0, nvrhi::Viewport(256.f, 256.f));
    state->shadowPass = std::make_unique<render::DepthPass>(device, state->common);
    render::DepthPass::CreateParameters shadowParams;
    shadowParams.depthBias = -100;
    shadowParams.slopeScaledDepthBias = -2.f;
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
    const float yScale = 1.f / .57735027f, xScale = yScale * info.height / info.width;
    state.view.SetMatrices(
        worldToView, math::float4x4(xScale, 0, 0, 0, 0, yScale, 0, 0, 0, 0, camera[5][0], 1, 0, 0, camera[5][1], 0));
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
    auto draw = [&](const std::shared_ptr<render::PlanarShadowMap> &map, nvrhi::IFramebuffer *fb, unsigned slice)
    {
        commands->clearDepthStencilTexture(map->GetTexture(), nvrhi::TextureSubresourceSet(0, 1, slice, 1), true, 0.f,
                                           false, 0);
        auto view = map->GetPlanarView();
        state.SelectDraws(*view);
        render::DepthPass::Context context;
        render::RenderView(commands, view.get(), nullptr, fb, state.stableDraws, *state.shadowPass, context);
    };
    if (state.shadowDirty || forceRedraw)
    {
        AVBOIT_GPU_SCOPE(commands, "Sun / regular depth shadow");
        draw(state.shadow, state.shadowFramebuffer, 0);
    }
    {
        AVBOIT_GPU_SCOPE(commands, "Spot / regular depth shadow");
        draw(state.spotShadow, state.spotFramebuffer, 1);
    }
    state.shadowDirty = false;
}
void DonutSceneHost::UpdateLights(float time, const std::array<float, 3> &eye, const SceneLightingSettings &settings)
{
    using namespace math;
    auto &s = *m_impl;
    float3 direction(settings.sunDirection[0], settings.sunDirection[1], settings.sunDirection[2]);
    direction = length(direction) > 1e-5f ? normalize(direction) : float3(0, -1, 0);
    if (length(direction - float3(s.sun->GetDirection())) > 1e-5f)
    {
        SetWorldLightDirection(*s.sun, double3(direction));
        s.scene->GetSceneGraph()->Refresh(s.frame);
        s.shadow->SetupWholeSceneDirectionalLightView(*s.sun,
                                                      s.scene->GetSceneGraph()->GetRootNode()->GetGlobalBoundingBox());
        auto view = s.shadow->GetPlanarView();
        auto projection = view->GetProjectionMatrix(false);
        for (unsigned row = 0; row < 4; ++row)
            projection[row][2] = projection[row][3] - projection[row][2];
        view->SetMatrices(view->GetViewMatrix(), projection);
        view->UpdateCache();
        s.shadowDirty = true;
    }
    s.sun->irradiance = settings.sunIntensity;
    float3 position(settings.spotPosition[0], settings.spotPosition[1], settings.spotPosition[2]);
    if (settings.animateSpot)
        position.x += 1.5f * std::sin(time * .6f);
    float3 target(settings.spotTarget[0], settings.spotTarget[1], settings.spotTarget[2]);
    float3 spotDirection = length(target - position) > 1e-5f ? normalize(target - position) : float3(0, 0, 1);
    s.spot->SetPosition(double3(position));
    SetWorldLightDirection(*s.spot, double3(spotDirection));
    s.spot->intensity = settings.spotIntensity;
    float3 right =
        normalize(cross(std::abs(spotDirection.y) > .99f ? float3(1, 0, 0) : float3(0, 1, 0), spotDirection));
    float3 up = cross(spotDirection, right);
    auto view = affine3::from_cols(right, up, spotDirection,
                                   float3(-dot(position, right), -dot(position, up), -dot(position, spotDirection)));
    const float scale = 1.f / std::tan(65.f * 3.14159265f / 360.f);
    auto projection = float4x4(scale, 0, 0, 0, 0, scale, 0, 0, 0, 0, -.1f / 24.9f, 1, 0, 0, 2.5f / 24.9f, 0);
    s.spotShadow->GetPlanarView()->SetMatrices(view, projection);
    s.spotShadow->GetPlanarView()->UpdateCache();
    auto &data = s.lightData;
    data = {};
    data[0] = {direction.x, direction.y, direction.z, settings.sunIntensity};
    data[1] = {position.x, position.y, position.z, settings.spotIntensity};
    data[2] = {spotDirection.x, spotDirection.y, spotDirection.z, std::cos(65.f * 3.14159265f / 360.f)};
    data[3] = {1.f, .85f, .65f, std::cos(42.f * 3.14159265f / 360.f)};
    data[4] = {.035f, 1.f, settings.volumeShadows ? 1.f : 0.f, 0};
    auto matrix = [&](unsigned base, float4x4 value)
    {
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned column = 0; column < 4; ++column)
                data[base + row][column] = value[row][column];
    };
    matrix(5, s.shadow->GetWorldToUvzwMatrix());
    matrix(9, s.spotShadow->GetWorldToUvzwMatrix());
    for (unsigned cascade = 0; cascade < 2; ++cascade)
    {
        auto &map = s.volumeCascades[cascade];
        const float radius = cascade == 0 ? 6.f : 20.f;
        map->SetupDynamicDirectionalLightView(*s.sun, float3(eye[0], eye[1], eye[2]), float3(radius, radius, 40.f));
        auto v = map->GetPlanarView();
        auto p = v->GetProjectionMatrix(false);
        for (unsigned row = 0; row < 4; ++row)
            p[row][2] = p[row][3] - p[row][2];
        v->SetMatrices(v->GetViewMatrix(), p);
        v->UpdateCache();
        matrix(13 + cascade * 4, map->GetWorldToUvzwMatrix());
        matrix(25 + cascade * 4, inverse(map->GetWorldToUvzwMatrix()));
    }
    matrix(21, s.spotShadow->GetWorldToUvzwMatrix());
    matrix(33, inverse(s.spotShadow->GetWorldToUvzwMatrix()));
}
const VolumeLightData &DonutSceneHost::LightingData() const
{
    return m_impl->lightData;
}
void DonutSceneHost::SetVolumeLighting(const VolumeLighting &lighting)
{
    auto &pass = *m_impl->forward;
    pass.volumeLights = lighting.lights;
    pass.volumeMoments = lighting.moments;
    pass.volumeTau = lighting.opticalDepth;
    pass.ResetBindingCache();
}
nvrhi::ITexture *DonutSceneHost::ShadowTexture() const
{
    return m_impl->shadow->GetTexture();
}
} // namespace avboit
