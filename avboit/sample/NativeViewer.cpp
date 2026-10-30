// SPDX-License-Identifier: MIT
// Native DX12 raster baseline. All pass transitions are visible in Render().
#include <donut/app/ApplicationBase.h>
#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>
#include <donut/core/vfs/VFS.h>
#include <donut/engine/ShaderFactory.h>
#include <nvrhi/utils.h>
#include <array>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <cmath>
#include <GLFW/glfw3.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/CommonRenderPasses.h>
#include "sample/SceneLoader.h"
#include "sample/DonutSceneHost.h"
#include "sample/SampleCamera.h"
#include "debug/GpuProfiler.h"
#include "debug/StablePowerState.h"
#include "sample/FixtureGeometry.h"
#include "sample/StressFixtures.h"
#include "sample/ViewerUi.h"
#include "sample/ViewerOptions.h"
#include "sample/TemporalAA.h"
#include "sample/TranslationGizmo.h"
#include "sample/MaterialTable.h"
#include "sample/GeometrySubmission.h"
#include "sample/SubmissionOrder.h"
#include "render/TransparencyPipeline.h"
#include <imgui.h>
#include <atomic>
#include <chrono>
#include <thread>

using namespace donut;
using F4 = std::array<float, 4>;
using F3 = std::array<float, 3>;

class NativeViewer : public app::IRenderPass
{
    unsigned Width = 640, Height = 384;
    unsigned windowWidth = 0, windowHeight = 0;
    bool followWindow = false;
    int fpsLimit = 120;
    std::chrono::steady_clock::time_point lastFrameStart{};
    struct Pass
    {
        nvrhi::BindingLayoutHandle layout;
        nvrhi::BindingSetHandle bindings;
        nvrhi::GraphicsPipelineHandle graphics;
    };
    avboit::TransparencyPipeline transparency;
    avboit::GpuProfiler profiler;
    bool detailedProfiling = true, profilingEnabled = false, profilerDirty = false, rebuildRequested = false;
    ImVec2 uiCubic, uiSparse, uiFrost, uiSmoke, uiProfile, uiGlassPanes, uiSlopeSplat;
    ImVec2 uiMipLow, uiMipHigh, uiBackground, uiDemoCase;
    ImVec2 uiFrostTiling, uiSharedMaterials;
    ImVec2 uiOccupancy;
    std::array<ImVec2, 8> uiOccupancyChoices;
    int occupancyOverlay = 0, occupancySlice = 0;
    float occupancyOpacity = .5f;
    bool occupancyGrid = true;
    nvrhi::BufferHandle occupancySettings;
    std::array<ImVec2, 3> uiBackgroundChoices;
    std::array<ImVec2, avboit::StressPresetCount + 3> uiDemoChoices;
    float uiScrollRequest = 0; // UI interaction tests can request normal scrolling.
    ImVec2 uiVolume;
    std::array<ImVec2, 2> uiVolumeChoices;
    std::unique_ptr<engine::ShaderFactory> shaders;
    avboit::SceneData scene;
    bool useDonutScene = false;
    std::unique_ptr<avboit::DonutSceneHost> donutScene;
    avboit::VolumeLighting volumeLighting;
    avboit::SceneLightingSettings sceneLighting;
    bool lowResolutionVolumes = false, stochasticVolumeUpsample = false;
    unsigned volumeSampleFrame = 0;
    bool taaEnabled = false;
    std::unique_ptr<avboit::TemporalAA> temporalAA;
    float volumeStepSize = .5f, volumeLowT = .1f, volumeStepScale = 4.f;
    int volumeMaxSteps = 256;
    std::vector<avboit::StressVolume> localVolumes;
    std::vector<unsigned> localVolumeRecords;
    bool halfAccumulation = false, halfComposition = false, fullVfx = true, allInterface = false, animateOrder = false;
    unsigned submissionOrder = 0, orderSeed = 1, orderFrame = 0, uploadedOrder = 0, uploadedOrderSeed = 1;
    unsigned depthBudget = 128;
    bool fixedDepth = false, calibrationBoard = false, sceneGlassPanes = true;
    float emissiveGain = 1;
    std::vector<unsigned> emissiveMaterials;
    std::vector<std::array<unsigned, 2>> viewVertexRanges;
    std::vector<unsigned> viewObjects;
    bool qBound = true, protectBackground = false, zeroTShortcut = false, zeroDepth = true, mirrorBlur = true;
    float backgroundThreshold = .001f;
    unsigned volumeScale = 8, volumeWidth = 320, volumeHeight = 180, rays = 320 * 180, occupancyTiles = 920;
    unsigned stressPreset = 0, cameraPreset = 0;
    int customRenderSize[2] = {2560, 1440};
    int pendingSceneMode = 0;
    bool sceneSourceRequested = false;
    std::array<char, 2048> scenePathUi{};
    std::array<char, 1024> csvPathUi{};
    std::string runtimeError;
    int interactiveBenchmarkFrames = 180;
    bool benchmarkRequested = false;
    struct FixtureMotion
    {
        unsigned firstRecord, object;
        F3 amplitude;
        float frequency;
    };
    std::vector<FixtureMotion> fixtureMotions;
    float uploadedFixtureTime = 0;
    std::filesystem::path sponzaPath;
    bool useSponza = false, emissiveBalls = false, emissiveFront = false, showSmoke = true, glassSphere = false,
         rgbGlass = false;
    float sphereRoughness = 0;
    nvrhi::BufferHandle sceneVertices;
    std::vector<Pass> scenePasses, shadowPasses;
    nvrhi::TextureHandle shadowDepth;
    bool shadowDirty = true, redrawShadows = false, hardwareDepth = false;
    nvrhi::FramebufferHandle shadowFb;
    std::vector<nvrhi::TextureHandle> sceneTextures;
    std::unique_ptr<engine::TextureCache> textureCache;
    std::shared_ptr<engine::CommonRenderPasses> commonPasses;
    nvrhi::CommandListHandle commands;
    nvrhi::BufferHandle vertexBuffer, boundsBuffer, cameraBuffer, interfaceIndexBuffer, geometryIndexBuffer,
        opaqueIndexBuffer;
    std::vector<F4> submittedVertices;
    std::vector<unsigned> interfaceIndices, geometryIndices, opaqueFirstRecords;
    std::vector<avboit::GeometryDraw> interfaceDraws, geometryDraws, opaqueDraws;
    bool interfaceSelectionDirty = true, interfaceFrostEnabled = false;
    // Borrowed outputs, refreshed after volume recreation.
    nvrhi::IBuffer *masks = nullptr, *warp = nullptr, *extinction = nullptr, *physicalMasks = nullptr;
    nvrhi::ITexture *lut = nullptr;
    nvrhi::TextureHandle opaque, depth;
    nvrhi::TextureHandle frostNormal, volumeDensity;
    nvrhi::SamplerHandle frostSampler;
    nvrhi::ITexture *numerator = nullptr, *denominator = nullptr, *totalTau = nullptr;
    nvrhi::ITexture *backNumerator = nullptr;
    nvrhi::ITexture *blurPyramid = nullptr;
    Pass displayPass;
    nvrhi::IBuffer *blurRectangles = nullptr;
    bool fullBlur = false, sparseBlur = true;
    unsigned frostMipCount = 3;
    float maxFrostSigma = 24.f;
    float frostDetailRadius = 24.f;
    float frostNormalTiling = 1.f;
    unsigned maxBlurMip = 0;
    float planarIor = 1.3f, sphereIor = 1.5f, refractionStrength = 1.f;
    bool normalSphere = false;
    avboit::MaterialTable materials;
    nvrhi::BufferHandle materialBuffer;
    std::vector<unsigned> objectFirstRecords, objectMaterials;
    std::vector<F4> uploadedMaterials;
    int selectedMaterial = 0, selectedActor = -1;
    avboit::TranslationGizmo actorGizmo;
    bool showActorGizmo = true, actorGeometryDirty = false;
    std::vector<F3> actorHomeCenters;
    F3 ActorCenter(unsigned actor) const
    {
        F3 center;
        for (unsigned a = 0; a < 3; ++a)
            center[a] = (bounds[3 + actor * 2][a] + bounds[4 + actor * 2][a]) * .5f;
        return center;
    }
    void TranslateActor(unsigned actor, F3 delta)
    {
        if (actor >= objectFirstRecords.size() || delta == F3{})
            return;
        for (float x : delta)
            if (!std::isfinite(x))
                return;
        const unsigned first = objectFirstRecords[actor];
        const unsigned end = actor + 1 < objectFirstRecords.size() ? objectFirstRecords[actor + 1]
                                                                   : (opaqueVertices + transparentVertices) * 4;
        for (unsigned a = 0; a < 3; ++a)
        {
            bounds[3 + actor * 2][a] += delta[a];
            bounds[4 + actor * 2][a] += delta[a];
            for (unsigned v = first; v < end; v += 4)
                vertices[v][a] += delta[a];
        }
        // The light-space density integration uses a separate volume proxy list.
        for (unsigned v = 0; v < localVolumeRecords.size(); ++v)
            if (localVolumeRecords[v] == first)
                for (unsigned a = 0; a < 3; ++a)
                    localVolumes[v].center[a] += delta[a];
        actorGeometryDirty = true;
        if (temporalAA)
            temporalAA->Invalidate();
    }
    void SetActorPosition(unsigned actor, F3 position)
    {
        const auto center = ActorCenter(actor);
        for (unsigned a = 0; a < 3; ++a)
            position[a] -= center[a];
        TranslateActor(actor, position);
    }
    void DrawActorGizmo()
    {
        if (!showActorGizmo || selectedActor < 0 || unsigned(selectedActor) >= objectMaterials.size() ||
            std::find(viewObjects.begin(), viewObjects.end(), unsigned(selectedActor)) != viewObjects.end())
        {
            actorGizmo.Reset();
            return;
        }
        const unsigned actor = unsigned(selectedActor);
        F3 low, high, center, delta;
        for (unsigned a = 0; a < 3; ++a)
        {
            float offset = 0;
            for (const auto &motion : fixtureMotions)
                if (motion.object == actor)
                    offset += motion.amplitude[a] * std::sin(time * motion.frequency);
            low[a] = bounds[3 + actor * 2][a] + offset;
            high[a] = bounds[4 + actor * 2][a] + offset;
            center[a] = (low[a] + high[a]) * .5f;
        }
        avboit::TranslationGizmo::View view{navigation.Eye(),     navigation.Right(),         navigation.Up(),
                                            navigation.Forward(), ImGui::GetIO().DisplaySize, float(Width) / Height};
        if (actorGizmo.Draw(view, center, low, high, delta))
            TranslateActor(actor, delta);
        if (actorGizmo.Active())
            animate = false;
    }
    double pointerX = 0, pointerY = 0;
    bool preserveMaterialsOnRebuild = false;
    std::string ActorLabel(unsigned actor) const
    {
        unsigned kind = materials.entries[objectMaterials[actor]].kind;
        const char *type = kind == 6   ? "Volume smoke"
                           : kind == 5 ? "Homogeneous volume"
                           : kind == 1 ? "Smoke card"
                           : kind == 4 ? "Glass sphere"
                                       : "Glass";
        return std::string(type) + " / Actor " + std::to_string(actor);
    }
    void SelectActor(unsigned actor)
    {
        if (actor >= objectMaterials.size())
            return;
        actorGizmo.Reset();
        selectedActor = int(actor);
        selectedMaterial = int(objectMaterials[actor]);
    }
    void PickActor()
    {
        if (!ImGui::GetCurrentContext() || ImGui::GetIO().WantCaptureMouse ||
            actorGizmo.HitTest(ImVec2(float(pointerX), float(pointerY))))
            return;
        const auto size = ImGui::GetIO().DisplaySize;
        if (size.x <= 0 || size.y <= 0)
            return;
        const float x = (float(pointerX) / size.x * 2.f - 1.f) * (float(Width) / Height) * .57735027f;
        const float y = (1.f - float(pointerY) / size.y * 2.f) * .57735027f;
        const auto eye = navigation.Eye(), right = navigation.Right(), up = navigation.Up(),
                   forward = navigation.Forward();
        F3 direction;
        for (unsigned axis = 0; axis < 3; ++axis)
            direction[axis] = forward[axis] + x * right[axis] + y * up[axis];
        float nearest = 80.f;
        int hit = -1;
        // Bounds picking also works for a volume's empty proxy geometry. The actor
        // list remains available for overlapping bounds and occluded actors.
        for (unsigned actor = 0; actor < objectMaterials.size(); ++actor)
        {
            F3 origin = eye, ray = direction;
            if (std::find(viewObjects.begin(), viewObjects.end(), actor) != viewObjects.end())
            {
                origin = {0, 0, 0};
                ray = {x, y, 1};
            }
            F3 low, high;
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                low[axis] = bounds[3 + actor * 2][axis];
                high[axis] = bounds[4 + actor * 2][axis];
                for (const auto &motion : fixtureMotions)
                    if (motion.object == actor)
                    {
                        float shift = motion.amplitude[axis] * std::sin(time * motion.frequency);
                        low[axis] += shift;
                        high[axis] += shift;
                    }
            }
            float enter = .05f, leave = nearest;
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                if (std::abs(ray[axis]) < 1e-7f)
                {
                    if (origin[axis] < low[axis] || origin[axis] > high[axis])
                        leave = -1;
                }
                else
                {
                    float a = (low[axis] - origin[axis]) / ray[axis];
                    float b = (high[axis] - origin[axis]) / ray[axis];
                    enter = std::max(enter, std::min(a, b));
                    leave = std::min(leave, std::max(a, b));
                }
            }
            if (leave >= enter && enter < nearest)
            {
                nearest = enter;
                hit = int(actor);
            }
        }
        if (hit >= 0)
            SelectActor(unsigned(hit));
    }
    avboit::MaterialDefaults MaterialDefaults() const
    {
        return {planarIor, sphereIor, refractionStrength, normalSphere};
    }
    nvrhi::ITexture *composedHdr = nullptr;
    bool poisonResolve = false, tileResolve = true;
    unsigned vfxMode = 0, vfxLayers = 3, vfxVertices = 6;
    bool HasVfxDraws() const { return vfx && ((vfxLayers & 1u) != 0 || ((vfxLayers & 2u) != 0 && vfxVertices > 6)); }
    float vfxStrength = 20;
    nvrhi::FramebufferHandle opaqueFb;
    nvrhi::SamplerHandle linear;
    bool denseExtinction = false, poisonExtinction = false, slopeSplat = true;
    Pass opaquePass;
    std::vector<nvrhi::GraphicsPipelineHandle> opaqueMaterialPipelines;
    unsigned opaqueVertices = 0, transparentVertices = 0, objectCount = 0;
    std::vector<F4> vertices, bounds;
    avboit::SampleCamera navigation;
    bool cubic = true, frost = true, vfx = false, vfxBehind = false, animate = false;
    bool cubicTrilinear = false;
    float time = 0;
    std::array<F4, 12> camera;
    void UpdateCamera()
    {
        float theta = 0;
        for (unsigned index : objectMaterials)
            theta = std::max(theta, materials.Resolve(index, MaterialDefaults()).FrostTheta());
        auto gpuMaterials = materials.GpuRecords(MaterialDefaults());
        for (unsigned material : emissiveMaterials)
            for (unsigned channel = 0; channel < 3; ++channel)
                gpuMaterials[material * 4][channel] *= emissiveGain;
        if (gpuMaterials != uploadedMaterials)
        {
            if (temporalAA)
                temporalAA->Invalidate();
            interfaceSelectionDirty = true;
            commands->writeBuffer(materialBuffer, gpuMaterials.data(), gpuMaterials.size() * 16);
            uploadedMaterials = std::move(gpuMaterials);
        }
        const auto chain = avboit::FrostChain::Make(Width, Height, frostMipCount);
        // Material sigma uses fixed reference units. All allocated levels are
        // available; B cache coverage imposes no filter-radius restriction.
        const float sigmaCeiling = std::min(maxFrostSigma, std::sqrt(chain.VarianceAt(chain.mipCount - 1)));
        float sigma = frost ? std::min(sigmaCeiling, .5f * (1440.f / 1.15470054f) * theta) : 0.f;
        float variance = std::pow(sigma * 1.001f, 2.f);
        maxBlurMip = 0;
        while (sigma > 0 && maxBlurMip + 1 < chain.mipCount && chain.VarianceAt(maxBlurMip) < variance)
            ++maxBlurMip;
        if (fullBlur)
            maxBlurMip = chain.mipCount - 1;
        const auto eye = navigation.Eye(), right = navigation.Right(), up = navigation.Up(),
                   forward = navigation.Forward();
        camera = {{{eye[0], eye[1], eye[2], cubic ? (cubicTrilinear ? 2.f : 1.f) : 0.f},
                   {right[0], right[1], right[2], float(frost)},
                   {up[0], up[1], up[2], time},
                   {forward[0], forward[1], forward[2], vfx ? vfxStrength : 0.f},
                   {sigmaCeiling, float(maxBlurMip), float(sparseBlur), float(hardwareDepth)},
                   {-.05f / 79.95f, 4.f / 79.95f, .05f, 80.f},
                   {float(Width), float(Height), float(volumeWidth), float(volumeHeight)},
                   {float(qBound), float(protectBackground), backgroundThreshold,
                    float((zeroTShortcut ? 1 : 0) | (zeroDepth ? 2 : 0) | (mirrorBlur ? 4 : 0) | (fullVfx ? 8 : 0))},
                   {float(chain.width), float(chain.height), float(chain.mipCount), tileResolve ? 0.f : 1.f},
                   {float(slopeSplat), frostDetailRadius, frostNormalTiling,
                    (stochasticVolumeUpsample || taaEnabled) ? float(1 + (volumeSampleFrame++ % 4096)) : 0.f},
                   {volumeStepSize, volumeLowT, volumeStepScale, float(volumeMaxSteps)},
                   {0, 0, 0, 0}}};
        if (temporalAA)
        {
            const auto jitter = temporalAA->Jitter();
            camera[11] = {jitter.x, jitter.y, 0, 0};
        }
        auto projected = bounds;
        auto animatedBounds = bounds;
        {
            unsigned seed = orderSeed + (animateOrder ? orderFrame++ : 0);
            bool updateVertices = actorGeometryDirty || (!fixtureMotions.empty() && time != uploadedFixtureTime) ||
                                  submissionOrder != uploadedOrder ||
                                  (submissionOrder == 2 && seed != uploadedOrderSeed);
            std::vector<F4> movingVertices;
            if (updateVertices)
                movingVertices = vertices;
            for (const auto &motion : fixtureMotions)
            {
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    float offset = motion.amplitude[axis] * std::sin(time * motion.frequency);
                    if (updateVertices)
                        for (unsigned vertex = 0; vertex < 6; ++vertex)
                            movingVertices[motion.firstRecord + vertex * 4][axis] += offset;
                    animatedBounds[3 + motion.object * 2][axis] += offset;
                    animatedBounds[4 + motion.object * 2][axis] += offset;
                }
            }
            if (updateVertices)
            {
                avboit::ReorderTransparentTriangles(movingVertices, opaqueVertices, transparentVertices,
                                                    submissionOrder, seed);
                commands->writeBuffer(vertexBuffer, movingVertices.data(), movingVertices.size() * 16);
                submittedVertices = std::move(movingVertices);
                interfaceSelectionDirty = true;
                uploadedOrder = submissionOrder;
                uploadedOrderSeed = seed;
                actorGeometryDirty = false;
            }
            uploadedFixtureTime = time;
        }
        const bool useFrost = frost && sigmaCeiling > 0;
        if (interfaceSelectionDirty || interfaceFrostEnabled != useFrost)
        {
            auto geometry = avboit::BuildGeometrySubmission(submittedVertices, opaqueVertices, transparentVertices,
                                                            materials, MaterialDefaults(), useFrost);
            if (geometry.indices != geometryIndices && !geometry.indices.empty())
                commands->writeBuffer(geometryIndexBuffer, geometry.indices.data(), geometry.indices.size() * 4);
            geometryIndices = std::move(geometry.indices);
            geometryDraws = std::move(geometry.draws);
            auto selected = avboit::BuildGeometrySubmission(submittedVertices, opaqueVertices, transparentVertices,
                                                            materials, MaterialDefaults(), useFrost, !allInterface);
            if (selected.indices != interfaceIndices && !selected.indices.empty())
                commands->writeBuffer(interfaceIndexBuffer, selected.indices.data(), selected.indices.size() * 4);
            interfaceIndices = std::move(selected.indices);
            interfaceDraws = std::move(selected.draws);
            interfaceSelectionDirty = false;
            interfaceFrostEnabled = useFrost;
        }
        projected[2][2] = float(fullBlur && useFrost ? 1u : 0u);
        projected[2][3] = float(maxBlurMip);
        for (unsigned object = 0; object < objectCount; ++object)
        {
            auto lower = animatedBounds[3 + object * 2], upper = animatedBounds[4 + object * 2];
            auto material = materials.Resolve(objectMaterials[object], MaterialDefaults());
            // UVs clamp to screen edges. Unbounded analytic sphere refraction
            // therefore schedules full-screen Gaussian outputs, not a clipped effect.
            float displacement = std::min(material.MaximumDisplacement(), float(std::max(Width, Height)));
            const bool hasFrost = useFrost && material.FrostTheta() > 0;
            float objectSigma = std::min(sigmaCeiling, .5f * (1440.f / 1.15470054f) * material.FrostTheta());
            unsigned objectMip = 0;
            while (hasFrost && objectMip < maxBlurMip &&
                   chain.VarianceAt(objectMip) < std::pow(objectSigma * 1.001f, 2.f))
                ++objectMip;
            // Mark special-surface kinds without expanding their screen bounds.
            projected[3 + object * 2][3] = float((material.value.ScalarExtinction() ? 0u : 1u) | (hasFrost ? 2u : 0u) |
                                                 ((!hasFrost && displacement > 0) ? 4u : 0u) | (objectMip << 3));
            projected[4 + object * 2][3] = 0;
            F3 low = {1e30f, 1e30f, 1e30f}, high = {-1e30f, -1e30f, -1e30f};
            if (std::find(viewObjects.begin(), viewObjects.end(), object) != viewObjects.end())
            {
                low = {lower[0], lower[1], lower[2]};
                high = {upper[0], upper[1], upper[2]};
            }
            else
                for (unsigned c = 0; c < 8; ++c)
                {
                    F3 d;
                    for (unsigned j = 0; j < 3; ++j)
                        d[j] = ((c & (1u << j)) ? upper[j] : lower[j]) - eye[j];
                    for (unsigned axis = 0; axis < 3; ++axis)
                    {
                        float v = 0;
                        for (unsigned j = 0; j < 3; ++j)
                            v += d[j] * camera[axis + 1][j];
                        low[axis] = std::min(low[axis], v);
                        high[axis] = std::max(high[axis], v);
                    }
                }
            for (unsigned j = 0; j < 3; ++j)
            {
                projected[3 + object * 2][j] = low[j];
                projected[4 + object * 2][j] = high[j];
            }
        }
        commands->writeBuffer(cameraBuffer, camera.data(), sizeof(camera));
        commands->writeBuffer(boundsBuffer, projected.data(), projected.size() * 16);
    }

    nvrhi::BufferHandle Buffer(unsigned bytes, nvrhi::Format format, bool uav, const char *name)
    {
        return GetDevice()->createBuffer(
            nvrhi::BufferDesc()
                .setByteSize(bytes)
                .setCanHaveTypedViews(true)
                .setCanHaveUAVs(uav)
                .setFormat(format)
                .setDebugName(name)
                .setInitialState(uav ? nvrhi::ResourceStates::UnorderedAccess : nvrhi::ResourceStates::CopyDest)
                .setKeepInitialState(true));
    }
    nvrhi::TextureHandle Texture(unsigned w, unsigned h, nvrhi::Format format, const char *name)
    {
        return GetDevice()->createTexture(nvrhi::TextureDesc()
                                              .setWidth(w)
                                              .setHeight(h)
                                              .setFormat(format)
                                              .setIsRenderTarget(true)
                                              .setClearValue(nvrhi::Color(0))
                                              .setUseClearValue(format == nvrhi::Format::D32)
                                              .setDebugName(name)
                                              .setInitialState(format == nvrhi::Format::D32
                                                                   ? nvrhi::ResourceStates::DepthWrite
                                                                   : nvrhi::ResourceStates::RenderTarget)
                                              .setKeepInitialState(true));
    }
    nvrhi::ShaderHandle Shader(const char *entry, nvrhi::ShaderType type, const char *file = "")
    {
        const bool transparency = !*file;
        if (transparency)
        {
            file = "shaders/TransparencyGeometry.hlsl";
            if (std::string(entry) == "splatPixel")
                file = "shaders/ExtinctionVolume.hlsl";
            if (std::string(entry) == "integrateLut")
                file = "shaders/ExtinctionVolume.hlsl";
            if (std::string(entry) == "integrateDenseLut")
                file = "shaders/ExtinctionVolume.hlsl";
            if (std::string(entry) == "clearOccupiedExtinction")
                file = "shaders/ExtinctionVolume.hlsl";
            if (std::string(entry) == "accumulationPixel")
                file = "shaders/OitResolve.hlsl";
            if (std::string(entry) == "interfacePixel")
                file = "shaders/Interface.hlsl";
            if (std::string(entry) == "composePixel")
                file = "shaders/FrostComposition.hlsl";
            if (std::string(entry) == "vfxPixel")
                file = "shaders/Distortion.hlsl";
            if (std::string(entry) == "displayPixel")
                file = "shaders/Display.hlsl";
        }
        std::vector<engine::ShaderMacro> defines = {{"VOLUME_SCALE", std::to_string(volumeScale)},
                                                    {"SLICE_COUNT", std::to_string(depthBudget)}};
        auto *permutations =
            (transparency || std::string(file) == "shaders/ZeroTransmittance.hlsl" ||
             std::string(file) == "shaders/OitResolve.hlsl" || std::string(file) == "shaders/FrostComposition.hlsl" ||
             std::string(file) == "shaders/ResolveTiles.hlsl" ||
             std::string(file) == "shaders/LocalVolumeExtinction.hlsl" ||
             std::string(file) == "shaders/LocalVolumeAccumulation.hlsl")
                ? &defines
                : nullptr;
        auto shader = shaders->CreateShader(file, entry, permutations, type);
        if (!shader)
            throw std::runtime_error(entry);
        return shader;
    }
    Pass Bind(nvrhi::BindingSetDesc desc, nvrhi::ShaderType visibility)
    {
        if (visibility == nvrhi::ShaderType::AllGraphics)
        {
            desc.addItem(nvrhi::BindingSetItem::TypedBuffer_SRV(15, cameraBuffer));
            desc.addItem(nvrhi::BindingSetItem::TypedBuffer_SRV(23, materialBuffer));
        }
        Pass p;
        if (!nvrhi::utils::CreateBindingSetAndLayout(GetDevice(), visibility, 0, desc, p.layout, p.bindings))
            throw std::runtime_error("Binding creation");
        return p;
    }
    void Graphics(Pass &p, const char *vs, const char *ps, nvrhi::IFramebuffer *fb, bool depthTest = false,
                  bool additive = false, const char *file = "")
    {
        nvrhi::GraphicsPipelineDesc d;
        d.VS = Shader(vs, nvrhi::ShaderType::Vertex, file);
        d.PS = Shader(ps, nvrhi::ShaderType::Pixel, file);
        d.bindingLayouts = {p.layout};
        d.renderState.depthStencilState.depthTestEnable = depthTest;
        d.renderState.depthStencilState.depthWriteEnable = depthTest;
        d.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Greater;
        d.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
        for (unsigned i = 0; i < 4; ++i)
            if (additive)
                d.renderState.blendState.targets[i]
                    .setBlendEnable(true)
                    .setSrcBlend(nvrhi::BlendFactor::One)
                    .setDestBlend(nvrhi::BlendFactor::One);
        p.graphics = GetDevice()->createGraphicsPipeline(d, fb->getFramebufferInfo());
    }
    void Quad(F3 center, F3 x, F3 y, F4 color, F3 transmission, unsigned kind, bool transparent, float roughness = 0,
              float distortion = 0, float shape = 0, bool viewSpace = false)
    {
        unsigned firstRecord = unsigned(vertices.size());
        if (!transparent && kind == 0)
            opaqueFirstRecords.push_back(firstRecord);
        if (useSponza && !viewSpace)
            center[1] += 1.75f;
        const float corners[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
        F3 low = {1e30f, 1e30f, 1e30f}, high = {-1e30f, -1e30f, -1e30f};
        for (auto &c : corners)
        {
            F4 position = {0, 0, 0, float(kind)};
            for (unsigned j = 0; j < 3; ++j)
            {
                position[j] = center[j] + x[j] * c[0] + y[j] * c[1];
                low[j] = std::min(low[j], position[j]);
                high[j] = std::max(high[j], position[j]);
            }
            vertices.push_back(position);
            vertices.push_back(color);
            vertices.push_back({transmission[0], transmission[1], transmission[2], roughness});
            vertices.push_back({c[0], c[1], distortion, shape});
        }
        if (transparent)
        {
            bounds.push_back({low[0], low[1], low[2], float((kind == 1 ? 0 : 1) | (roughness > 0 ? 2 : 0))});
            bounds.push_back(
                {high[0], high[1], high[2], roughness > 0 ? std::ceil(std::abs(distortion) + 2.f * 32.f + 1.f) : 0.f});
            objectFirstRecords.push_back(firstRecord);
            ++objectCount;
        }
    }
    void Draw(Pass &p, nvrhi::IFramebuffer *fb, unsigned count, unsigned first = 0)
    {
        nvrhi::GraphicsState s;
        s.pipeline = p.graphics;
        s.framebuffer = fb;
        s.bindings = {p.bindings};
        s.viewport.addViewportAndScissorRect(fb->getFramebufferInfo().getViewport());
        commands->setGraphicsState(s);
        commands->draw(nvrhi::DrawArguments().setVertexCount(count).setStartVertexLocation(first));
    }

  public:
    using IRenderPass::IRenderPass;
    void SetFrostChain(unsigned levels) { frostMipCount = levels; }
    void SetFrostDetailRadius(float radius) { frostDetailRadius = std::clamp(radius, 0.f, 64.f); }
    void SetFrostNormalTiling(float tiling) { frostNormalTiling = std::clamp(tiling, .25f, 64.f); }
    void SetPrecisionAndOrder(bool accumHalf, unsigned order, unsigned seed)
    {
        halfAccumulation = accumHalf;
        submissionOrder = order;
        orderSeed = seed;
    }
    void SetSparseBlur(bool enabled) { sparseBlur = enabled; }
    void SetCubicTrilinear(bool enabled) { cubicTrilinear = enabled; }
    void SetPoisonResolve(bool poison) { poisonResolve = poison; }
    void SetVfxMaterial(unsigned mode, float strength, unsigned layers)
    {
        vfxMode = mode;
        vfxStrength = strength;
        vfxLayers = layers;
    }
    void SetVolumeScale(unsigned scale)
    {
        if (scale != 2 && scale != 8)
            throw std::runtime_error("Invalid volume scale");
        volumeScale = scale;
    }
    void SetRedrawShadows(bool enabled) { redrawShadows = enabled; }
    void SetRefraction(float plane, float sphere, float strength, bool normal)
    {
        if (!std::isfinite(plane) || !std::isfinite(sphere) || !std::isfinite(strength))
            throw std::runtime_error("Refraction parameters must be finite");
        planarIor = std::clamp(plane, 1.f, 2.5f);
        sphereIor = std::clamp(sphere, 1.f, 2.5f);
        refractionStrength = std::clamp(strength, 0.f, 4.f);
        normalSphere = normal;
    }
    void SetSponzaPath(const std::filesystem::path &path)
    {
        sponzaPath = path;
        snprintf(scenePathUi.data(), scenePathUi.size(), "%s", path.string().c_str());
    }
    void SetCsvPath(const std::string &path) { snprintf(csvPathUi.data(), csvPathUi.size(), "%s", path.c_str()); }
    void ApplyCameraPreset(unsigned preset)
    {
        if (temporalAA)
            temporalAA->Invalidate();
        cameraPreset = preset;
        float heading = 0;
        F3 eye = useSponza ? F3{-.6f, 1.75f, 0} : F3{0, 0, 0};
        if (preset == 1)
            eye[0] += 2.f;
        if (preset == 2)
        {
            eye[0] -= 3.f;
            heading = .4f;
        }
        if (preset == 3)
        {
            eye[2] += 3.8f;
            heading = -.8f;
        }
        if (preset == 4)
            eye[2] -= 14.f;
        if (preset == 5)
            heading = 3.14159265f;
        if (preset >= 7 && preset <= 9)
        {
            // Orbit the crossing at z=4.5 without changing its projected center.
            heading = (preset == 7 ? 30.f : preset == 8 ? 40.f : -30.f) * 3.14159265f / 180.f;
            eye = {-4.5f * std::sin(heading), useSponza ? 1.75f : 0.f, 4.5f * (1.f - std::cos(heading))};
        }
        navigation.SetPose(eye, heading, preset == 6 ? 1.57079632679f : 0.f);
    }
    void SetStress(unsigned preset, float fixtureTime)
    {
        if (preset >= avboit::StressPresetCount || !std::isfinite(fixtureTime))
            throw std::runtime_error("Expected a valid stress preset and finite --time");
        stressPreset = preset;
        time = fixtureTime;
    }
    void SetFixtureOptions(bool emissive, bool smoke, bool sphere, bool rgb, float roughness)
    {
        emissiveBalls = emissive;
        showSmoke = smoke;
        glassSphere = sphere;
        sceneGlassPanes = !sphere;
        rgbGlass = rgb;
        sphereRoughness = std::clamp(roughness, 0.f, 1.f);
    }
    void SetProfilingOptions(bool detailed, bool enabled)
    {
        detailedProfiling = detailed;
        profilingEnabled = enabled;
    }
    void SetCalibration(bool board, float gain)
    {
        calibrationBoard = board;
        emissiveGain = gain;
    }
    void SetEmissiveFront(bool front) { emissiveFront = front; }
    void SetVolumeMarch(float step, float threshold, float scale, int budget)
    {
        volumeStepSize = step;
        volumeLowT = threshold;
        volumeStepScale = scale;
        volumeMaxSteps = budget;
    }
    void SetVolumeResolution(bool half, bool stochastic)
    {
        lowResolutionVolumes = half;
        stochasticVolumeUpsample = stochastic;
    }
    void SetSceneLighting(float sun, float spot, bool shadows)
    {
        sceneLighting.sunIntensity = sun;
        sceneLighting.spotIntensity = spot;
        sceneLighting.volumeShadows = shadows;
    }
    void SetDonutScene(bool enabled) { useDonutScene = enabled; }
    void SetCompactOptions(bool half, bool full, bool all)
    {
        halfComposition = half;
        fullVfx = full;
        allInterface = all;
    }
    void SetZeroOptions(bool depth, bool mirror)
    {
        zeroDepth = depth;
        mirrorBlur = mirror;
    }
    void SetBackgroundOptions(bool bound, bool protect, float threshold, bool shortcut)
    {
        qBound = bound;
        protectBackground = protect;
        backgroundThreshold = threshold;
        zeroTShortcut = shortcut;
    }
    void SetDepthBudget(unsigned budget, bool fixed = false)
    {
        depthBudget = budget;
        fixedDepth = fixed;
    }
    void SetRenderSize(unsigned width, unsigned height)
    {
        Width = avboit::AlignRenderDimension(width);
        Height = avboit::AlignRenderDimension(height);
    }
    void SetTileResolve(bool enabled) { tileResolve = enabled; }
    void SetFpsLimit(unsigned value) { fpsLimit = int(value); }
    void SetOccupancyOverlay(unsigned mode, unsigned slice)
    {
        occupancyOverlay = int(mode);
        occupancySlice = int(slice);
    }
    void SetFollowWindow(bool enabled)
    {
        followWindow = enabled;
        SyncWindowSize();
    }
    void SyncWindowSize()
    {
        if (!followWindow || !windowWidth || !windowHeight)
            return;
        // Bound allocation size, then align the internal extent to four pixels.
        const double scale = std::min(1., 2048. / std::max(windowWidth, windowHeight));
        const unsigned width = avboit::AlignRenderDimension(std::max(32u, unsigned(std::round(windowWidth * scale))));
        const unsigned height = avboit::AlignRenderDimension(std::max(32u, unsigned(std::round(windowHeight * scale))));
        if (width == Width && height == Height)
            return;
        Width = width;
        Height = height;
        rebuildRequested = true;
        preserveMaterialsOnRebuild = true;
    }
    void SetTaa(bool enabled) { taaEnabled = enabled; }
    void SetHardwareDepth(bool enabled) { hardwareDepth = enabled; }
    void SetExtinctionOptions(bool dense, bool poison, bool slope)
    {
        denseExtinction = dense;
        poisonExtinction = poison;
        slopeSplat = slope;
    }
    void Configure(bool useCubic, bool useFrost, bool sponza, bool vfxEnabled, bool behind, bool fullBlurReference,
                   unsigned preset, float sigmaLimit)
    {
        maxFrostSigma = std::clamp(sigmaLimit, 0.f, 24.f);
        fullBlur = fullBlurReference;
        vfx = vfxEnabled;
        vfxBehind = behind;
        cubic = useCubic;
        frost = useFrost;
        useSponza = sponza;
        pendingSceneMode = sponza ? (useDonutScene ? 1 : 2) : 0;
        ApplyCameraPreset(preset);
    }
    unsigned CurrentDemoCase() const
    {
        if (stressPreset == 0 && glassSphere)
            return avboit::StressPresetCount + (sphereRoughness > 0 ? 1u : 0u);
        if (stressPreset == 10 && vfx && vfxMode == 2)
            return avboit::StressPresetCount + 2;
        return stressPreset;
    }
    const char *DemoCaseName(unsigned index) const
    {
        if (index < avboit::StressPresetCount)
            return avboit::StressPresetName(index);
        static const char *names[] = {"Solid RGB glass sphere (inversion)", "Frosted RGB glass sphere",
                                      "Shock wave + frost / refraction"};
        return names[index - avboit::StressPresetCount];
    }
    void SelectDemoCase(unsigned index)
    {
        stressPreset = index < avboit::StressPresetCount ? index : 0;
        const unsigned sphereCase = avboit::StressPresetCount, shockCase = sphereCase + 2;
        glassSphere = index == sphereCase || index == sphereCase + 1;
        rgbGlass = glassSphere;
        if (index == 0)
            sceneGlassPanes = true;
        if (glassSphere)
            sceneGlassPanes = false; // Sphere presets remain isolated; UI toggles are independent.
        sphereRoughness = index == sphereCase + 1 ? .55f : 0.f;
        calibrationBoard = glassSphere;
        emissiveBalls = index != 17;
        emissiveFront = false;
        showSmoke = true;
        vfx = index == shockCase;
        vfxMode = index == shockCase ? 2 : 0;
        vfxLayers = 3;
        vfxBehind = false;
        if (index == shockCase)
            stressPreset = 10;
        time = 0;
        animate = index == shockCase;
        ApplyCameraPreset(index == 17 ? 8 : 0);
        rebuildRequested = true;
        preserveMaterialsOnRebuild = false;
    }
    void DrawUi()
    {
        ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(390, std::max(300.f, ImGui::GetIO().DisplaySize.y - 36.f)),
                                 ImGuiCond_FirstUseEver);
        ImGui::Begin("AVBOIT - Quality and fixtures");
        if (uiScrollRequest != 0)
        {
            ImGui::SetScrollY(ImGui::GetScrollY() + uiScrollRequest);
            uiScrollRequest = 0;
        }
        ImGui::TextUnformatted("WASD / QE move; RMB look; Home reset");
        auto itemCenter = []()
        {
            auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            return ImVec2((a.x + b.x) * .5f, (a.y + b.y) * .5f);
        };
        const char *backgrounds[] = {"Calibration background", "Sponza", "Sponza (legacy reference)"};
        if (ImGui::CollapsingHeader("Scene actors", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextWrapped("Select an actor here or click its bounds in the scene. Drag XYZ handles to move.");
            if (ImGui::BeginListBox("##actors", ImVec2(-1, 110)))
            {
                for (unsigned actor = 0; actor < objectMaterials.size(); ++actor)
                    if (ImGui::Selectable(ActorLabel(actor).c_str(), selectedActor == int(actor)))
                        SelectActor(actor);
                ImGui::EndListBox();
            }
            if (selectedActor >= 0 && unsigned(selectedActor) < objectMaterials.size())
            {
                ImGui::Text("Selected: %s", ActorLabel(unsigned(selectedActor)).c_str());
                F3 position = ActorCenter(unsigned(selectedActor));
                ImGui::SetNextItemWidth(-1);
                if (ImGui::DragFloat3("##actor-position", position.data(), .02f, -1000.f, 1000.f, "%.3f"))
                    SetActorPosition(unsigned(selectedActor), position);
                ImGui::TextDisabled("Position XYZ (world units). Ctrl-click to type.");
                if (ImGui::Checkbox("Move gizmo", &showActorGizmo))
                    actorGizmo.Reset();
                ImGui::SameLine();
                if (ImGui::Button("Reset position"))
                    SetActorPosition(unsigned(selectedActor), actorHomeCenters[selectedActor]);
                if (ImGui::Button("Reset all actor positions"))
                    for (unsigned a = 0; a < actorHomeCenters.size(); ++a)
                        SetActorPosition(a, actorHomeCenters[a]);
                ImGui::TextWrapped("White center: move in screen plane. Esc: cancel drag. Dragging pauses animation.");
                ImGui::TextDisabled("Scene preset changes reset edits; quality changes keep them.");
                ImGui::Separator();
                avboit::DrawMaterialEditor(materials, MaterialDefaults(), selectedMaterial);
            }
            bool sharedMaterialsOpen = ImGui::TreeNode("Shared material defaults");
            uiSharedMaterials = itemCenter();
            if (sharedMaterialsOpen)
            {
                ImGui::TextWrapped(
                    "These settings apply to all actors that inherit scene optics and to all frost detail.");
                ImGui::Checkbox("Normal-driven sphere (artistic)", &normalSphere);
                ImGui::SliderFloat("Planar IOR", &planarIor, 1, 2.5f, "%.2f");
                ImGui::SliderFloat("Sphere IOR", &sphereIor, 1, 2.5f, "%.2f");
                ImGui::SliderFloat("Self-refraction gain", &refractionStrength, 0, 4, "%.2f");
                ImGui::SliderFloat("VFX strength (px)", &vfxStrength, 0, 64);
                ImGui::SliderFloat("Post-blur offset (1440p px)", &frostDetailRadius, 0.f, 64.f, "%.1f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Reserve this much of the material offset for the Gaussian lookup. Larger offsets remain "
                        "in B. No new RT/pass.");
                ImGui::TextWrapped(
                    "No B-source radius/offset limit. Material sigma selects the available Gaussian levels.");
                ImGui::SetNextItemWidth(135);
                ImGui::SliderFloat("Frost normal tiling", &frostNormalTiling, .25f, 64.f, "%.2f",
                                   ImGuiSliderFlags_Logarithmic);
                uiFrostTiling = ImGui::GetItemRectMin();
                uiFrostTiling.x += 100;
                uiFrostTiling.y = (uiFrostTiling.y + ImGui::GetItemRectMax().y) * .5f;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Normal-map repeats across a pane. Larger values make finer detail. Changes live.");
                ImGui::TreePop();
            }
        }
        ImGui::TextUnformatted("Background");
        ImGui::SetNextItemWidth(-1);
        bool backgroundOpen = ImGui::BeginCombo("##Background", backgrounds[pendingSceneMode]);
        uiBackground = itemCenter();
        if (backgroundOpen)
        {
            for (int choice = 0; choice < 3; ++choice)
            {
                if (ImGui::Selectable(backgrounds[choice], pendingSceneMode == choice))
                {
                    pendingSceneMode = choice;
                    sceneSourceRequested = true;
                }
                uiBackgroundChoices[choice] = itemCenter();
            }
            ImGui::EndCombo();
        }
        const unsigned demo = CurrentDemoCase();
        ImGui::TextUnformatted("Test case");
        ImGui::SetNextItemWidth(-1);
        bool demoOpen = ImGui::BeginCombo("##TestCase", DemoCaseName(demo), ImGuiComboFlags_HeightLargest);
        uiDemoCase = itemCenter();
        if (demoOpen)
        {
            for (unsigned choice = 0; choice < uiDemoChoices.size(); ++choice)
            {
                if (ImGui::Selectable(DemoCaseName(choice), demo == choice))
                    SelectDemoCase(choice);
                uiDemoChoices[choice] = itemCenter();
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Reset view"))
            ApplyCameraPreset(0);
        ImGui::SameLine();
        if (ImGui::Button("Look at sky"))
            ApplyCameraPreset(6);
        ImGui::TextWrapped("Selections apply immediately; test cases reset the view.");
        ImGui::SliderInt("FPS limit", &fpsLimit, 0, 240);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("CPU frame pacing. 0 = unlimited.");
        ImGui::Text("CPU frame rate: %.1f FPS", ImGui::GetIO().Framerate);
        const char *overlayLabels[] = {"Off",
                                       "Screen requests (RGB)",
                                       "Transparent / main resolve",
                                       "Frost requests",
                                       "Sharp B requests",
                                       "B prime cache tiles",
                                       "Extinction XY (any Z)",
                                       "Extinction physical Z slice"};
        ImGui::TextUnformatted("Occupancy overlay");
        ImGui::SetNextItemWidth(-1);
        bool overlayOpen = ImGui::BeginCombo("##OccupancyOverlay", overlayLabels[occupancyOverlay]);
        uiOccupancy = itemCenter();
        if (overlayOpen)
        {
            for (int choice = 0; choice < 8; ++choice)
            {
                if (ImGui::Selectable(overlayLabels[choice], occupancyOverlay == choice))
                    occupancyOverlay = choice;
                uiOccupancyChoices[choice] = itemCenter();
            }
            ImGui::EndCombo();
        }
        if (occupancyOverlay)
        {
            ImGui::SetNextItemWidth(140);
            ImGui::SliderFloat("Overlay opacity", &occupancyOpacity, 0, 1, "%.2f");
            ImGui::SameLine();
            ImGui::Checkbox("Grid", &occupancyGrid);
            if (occupancyOverlay == 7)
            {
                occupancySlice = std::min(occupancySlice, int(depthBudget) - 1);
                ImGui::SliderInt("Physical Z", &occupancySlice, 0, int(depthBudget) - 1);
            }
            if (occupancyOverlay == 1)
                ImGui::TextWrapped("R: transparent; G: frost; B: sharp source. White = all three.");
            else if (occupancyOverlay >= 6)
                ImGui::TextWrapped("Red: scalar extinction; cyan: RGB extinction; white: both.");
            else
                ImGui::TextWrapped("Colored = requested work; dimmed = inactive.");
            ImGui::TextWrapped("Conservative bounds, not exact fragment coverage. Debug cost is in display.");
        }
        ImGui::Separator();
        ImGui::Checkbox("Cubic reconstruction", &cubic);
        uiCubic = itemCenter();
        if (cubic)
        {
            ImGui::Checkbox("4-tap cubic + trilinear (approximate)", &cubicTrilinear);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Share the finer mip's cubic offsets across both mip levels.\n"
                                  "Off: exact 8-tap cubic. Compare highlights and roughness transitions.");
        }
        ImGui::Checkbox("Slope-aware extinction splat", &slopeSplat);
        uiSlopeSplat = itemCenter();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Bounded slope push; occupancy reserves the same log-depth range.");
        int volumeChoice = volumeScale == 2 ? 0 : 1;
        const char *volumeLabels[] = {"1/2 resolution (reference)", "1/8 resolution (default)"};
        bool volumeOpen = ImGui::BeginCombo("Extinction XY", volumeLabels[volumeChoice]);
        uiVolume = itemCenter();
        if (volumeOpen)
        {
            for (unsigned choice = 0; choice < 2; ++choice)
            {
                if (ImGui::Selectable(volumeLabels[choice], int(choice) == volumeChoice))
                {
                    volumeScale = choice == 0 ? 2u : 8u;
                    rebuildRequested = true;
                    preserveMaterialsOnRebuild = true;
                }
                uiVolumeChoices[choice] = itemCenter();
            }
            ImGui::EndCombo();
        }
        ImGui::Checkbox("Sparse first filter (16 taps vs 64)", &sparseBlur);
        uiSparse = itemCenter();
        ImGui::Checkbox("Frost enabled", &frost);
        uiFrost = itemCenter();
        ImGui::SetNextItemWidth(135);
        int levels = int(frostMipCount);
        if (ImGui::SliderInt("Frost mip count", &levels, 3, 5))
        {
            frostMipCount = unsigned(levels);
            rebuildRequested = true;
            preserveMaterialsOnRebuild = true;
        }
        {
            auto p = ImGui::GetItemRectMin();
            auto q = ImGui::GetItemRectMax();
            uiMipLow = ImVec2(p.x + 7, (p.y + q.y) * .5f);
            uiMipHigh = ImVec2(p.x + 128, (p.y + q.y) * .5f);
        }
        auto chain = avboit::FrostChain::Make(Width, Height, frostMipCount);
        ImGui::Text("Allocated chain: %u x %u -> %u x 45 (%u levels)", chain.width, chain.height,
                    chain.width >> (frostMipCount - 1), frostMipCount);
        ImGui::SetNextItemWidth(135);
        ImGui::SliderFloat("Maximum sigma (1440p px)", &maxFrostSigma, 0.f, 24.f, "%.2f");
        ImGui::Text("Active levels: %u / %u (bounds may be empty)", maxBlurMip + 1, frostMipCount);
        ImGui::Checkbox("VFX distortion", &vfx);
        ImGui::SameLine();
        ImGui::Checkbox("Animate", &animate);
        if (ImGui::CollapsingHeader("VFX material and layers"))
        {
            int mode = int(vfxMode);
            if (ImGui::Combo("VFX fixture", &mode,
                             "Oscillating ring\0Heat wave\0Expanding shock\0Front + rear shocks\0"))
            {
                vfxMode = unsigned(mode);
                vfx = true;
                rebuildRequested = true;
                preserveMaterialsOnRebuild = false;
            }
            bool primary = (vfxLayers & 1) != 0, secondary = (vfxLayers & 2) != 0;
            if (ImGui::Checkbox("Primary layer", &primary))
                vfxLayers = (vfxLayers & ~1u) | (primary ? 1u : 0u);
            if (ImGui::Checkbox("Secondary layer", &secondary))
                vfxLayers = (vfxLayers & ~2u) | (secondary ? 2u : 0u);
            if (ImGui::Button("Play expanding shock"))
            {
                time = 0;
                animate = true;
                vfx = true;
                vfxMode = 2;
                rebuildRequested = true;
                preserveMaterialsOnRebuild = false;
            }
        }
        if (ImGui::CollapsingHeader("Depth quality"))
        {
            if (ImGui::Checkbox("Fixed log-Z reference", &fixedDepth))
            {
                rebuildRequested = true;
                preserveMaterialsOnRebuild = true;
            }
            int choice = depthBudget == 16 ? 0 : depthBudget == 32 ? 1 : depthBudget == 64 ? 2 : 3;
            if (ImGui::Combo("Z budget", &choice, "16\00032\00064\000128\000"))
            {
                depthBudget = 16u << choice;
                rebuildRequested = true;
                preserveMaterialsOnRebuild = true;
            }
            ImGui::Text("Extinction and T-LUT: %u physical slots", depthBudget);
        }
        if (ImGui::CollapsingHeader("Internal resolution"))
        {
            if (ImGui::Checkbox("Follow window size", &followWindow))
                SyncWindowSize();
            const unsigned widths[] = {512, 640, 960, 1280, 1920, 2560};
            const unsigned heights[] = {320, 384, 576, 768, 1080, 1440};
            ImGui::Text("Actual render targets: %u x %u", Width, Height);
            for (unsigned i = 0; i < 6; ++i)
            {
                char label[32];
                snprintf(label, sizeof(label), "%u x %u", widths[i], heights[i]);
                if (ImGui::Selectable(label, Width == widths[i] && Height == heights[i]))
                {
                    followWindow = false;
                    SetRenderSize(widths[i], heights[i]);
                    rebuildRequested = true;
                    preserveMaterialsOnRebuild = true;
                }
            }
        }
        if (donutScene && ImGui::CollapsingHeader("Dynamic lighting / volume shadows"))
        {
            ImGui::Checkbox("Animate spot light", &sceneLighting.animateSpot);
            ImGui::Checkbox("Volumetric moment shadows", &sceneLighting.volumeShadows);
            ImGui::SliderFloat("Sun irradiance", &sceneLighting.sunIntensity, 0.f, 12.f);
            ImGui::SliderFloat3("Sun direction", sceneLighting.sunDirection.data(), -1.f, 1.f);
            ImGui::SliderFloat("Spot intensity", &sceneLighting.spotIntensity, 0.f, 500.f);
            ImGui::DragFloat3("Spot position", sceneLighting.spotPosition.data(), .05f);
            ImGui::DragFloat3("Spot target", sceneLighting.spotTarget.data(), .05f);
            ImGui::TextWrapped("Regular depth shadows + independent volumetric moments. Sun: two 256px cascades. Spot: "
                               "256px. Moments are evaluated only for lights overlapping volume bounds.");
        }
        if (ImGui::CollapsingHeader("Custom resolution / camera"))
        {
            ImGui::InputInt2("Internal width / height", customRenderSize);
            ImGui::TextDisabled("Internal dimensions round up to multiples of 4.");
            if (ImGui::Button("Apply custom resolution"))
            {
                followWindow = false;
                SetRenderSize(std::clamp(customRenderSize[0], 256, 4096), std::clamp(customRenderSize[1], 256, 4096));
                customRenderSize[0] = int(Width);
                customRenderSize[1] = int(Height);
                rebuildRequested = true;
                preserveMaterialsOnRebuild = true;
            }
            int pose = int(cameraPreset);
            if (ImGui::Combo("Camera preset", &pose,
                             "Default\0Right\0Left oblique\0Close oblique\0Far\0Reverse\0Sky\0"
                             "Crossing +30 deg\0Crossing +40 deg\0Crossing -30 deg\0"))
                ApplyCameraPreset(unsigned(pose));
            if (ImGui::Button("Reset camera preset"))
                ApplyCameraPreset(cameraPreset);
        }
        if (ImGui::CollapsingHeader("Resolve pipeline", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Cull empty resolve tiles", &tileResolve);
            if (ImGui::Checkbox("TAA (Halton + HDR history)", &taaEnabled))
            {
                rebuildRequested = true;
                preserveMaterialsOnRebuild = true;
            }
            ImGui::TextDisabled("Camera motion only; moving transparency may leave trails.");
            if (!taaEnabled)
                ImGui::Checkbox("Temporal volume dithering / upsample (requires TAA)", &stochasticVolumeUpsample);
            ImGui::SliderFloat("Volume step (world units)", &volumeStepSize, .005f, 2.f, "%.3f",
                               ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Volume low-T threshold", &volumeLowT, .001f, .5f, "%.3f", ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Volume low-T step multiplier", &volumeStepScale, 1.f, 8.f, "%.1fx");
            ImGui::SliderInt("Volume maximum steps", &volumeMaxSteps, 16, 512);
            if (ImGui::Checkbox("Half-resolution local volumes", &lowResolutionVolumes))
            {
                rebuildRequested = true;
                preserveMaterialsOnRebuild = true;
            }
            ImGui::TextWrapped("B prime on transparency tiles. Resolve into temporary color, then copy tiles to "
                               "SceneColor. q stays in registers; no I/R/q caches.");
        }
        if (ImGui::CollapsingHeader("OIT submission validation"))
        {
            ImGui::Text("Interface vertices: %u / %u", unsigned(interfaceIndices.size()), transparentVertices);
            int order = int(submissionOrder);
            if (ImGui::Combo("Submission order", &order, "Original\0Reverse triangles\0Shuffle triangles\0"))
                submissionOrder = unsigned(order);
            ImGui::Checkbox("Animate shuffled order", &animateOrder);
            if (ImGui::Button("Next shuffle seed"))
                ++orderSeed;
            ImGui::InputScalar("Order seed", ImGuiDataType_U32, &orderSeed);
        }
        if (ImGui::CollapsingHeader("Scene fixtures", ImGuiTreeNodeFlags_DefaultOpen))
        {
            bool changed = false;
            ImGui::SliderFloat("Fixture time", &time, 0, 8, "%.2f s");
            ImGui::BeginDisabled(stressPreset != 0);
            changed |= ImGui::Checkbox("Scene glass panes", &sceneGlassPanes);
            uiGlassPanes = itemCenter();
            ImGui::EndDisabled();
            ImGui::BeginDisabled(stressPreset != 0);
            changed |= ImGui::Checkbox("Glass sphere", &glassSphere);
            ImGui::EndDisabled();
            ImGui::SliderFloat("Emissive gain", &emissiveGain, 0, 64, "%.2f", ImGuiSliderFlags_Logarithmic);
            changed |= ImGui::Checkbox("Inversion calibration board", &calibrationBoard);
            changed |= ImGui::Checkbox("Emissive balls", &emissiveBalls);
            ImGui::SameLine();
            ImGui::BeginDisabled(stressPreset != 0);
            changed |= ImGui::Checkbox("Front smoke", &showSmoke);
            uiSmoke = itemCenter();
            ImGui::EndDisabled();
            if (ImGui::Checkbox("Emissive balls in front", &emissiveFront))
            {
                emissiveBalls = true;
                changed = true;
            }
            changed |= ImGui::Checkbox("Place VFX behind glass", &vfxBehind);
            if (changed)
            {
                rebuildRequested = true;
                preserveMaterialsOnRebuild = false;
            }
        }
        if (ImGui::CollapsingHeader("Custom Sponza asset"))
        {
            ImGui::InputText("Sponza glTF path", scenePathUi.data(), scenePathUi.size());
            ImGui::TextUnformatted("Empty path uses the bundled Sponza asset.");
            if (ImGui::Button("Reload selected background"))
                sceneSourceRequested = true;
            ImGui::TextWrapped("Loading a scene resets fixture material edits; quality switches preserve them.");
        }
        if (!runtimeError.empty())
            ImGui::TextWrapped("%s", runtimeError.c_str());
        ImGui::End();
        ImGui::SetNextWindowPos(ImVec2(820, 12), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(450, 730), ImGuiCond_FirstUseEver);
        ImGui::Begin("GPU pass timings");
        if (ImGui::Checkbox("Enable profiling", &profilingEnabled))
            profilerDirty = true;
        uiProfile = itemCenter();
        ImGui::SameLine();
        if (ImGui::Checkbox("Detailed", &detailedProfiling))
            profilerDirty = true;
        ImGui::InputText("CSV path", csvPathUi.data(), csvPathUi.size());
        if (ImGui::Button("Export timing CSV") && profiler.ActiveStages() > 0)
        {
            try
            {
                profiler.ExportCsv(csvPathUi[0] ? csvPathUi.data() : "native-benchmark-ui.csv",
                                   "interactive; internal=" + std::to_string(Width) + "x" + std::to_string(Height));
            }
            catch (const std::exception &e)
            {
                donut::log::warning("%s", e.what());
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset stats"))
            profilerDirty = true;
        ImGui::InputInt("Benchmark frames", &interactiveBenchmarkFrames);
        if (ImGui::Button("Run fixed-camera benchmark"))
            benchmarkRequested = true;
        avboit::DrawGpuProfiler(profiler);
        ImGui::End();
        DrawActorGizmo();
    }
    void TestUiInteractions(avboit::ViewerUi &ui, nvrhi::IFramebuffer *framebuffer);
    void TestMaterialEdits(nvrhi::IFramebuffer *framebuffer);
    void TestTemporalAA(nvrhi::IFramebuffer *framebuffer);
    void TestActorEditing(nvrhi::IFramebuffer *framebuffer);
    void ApplyPendingUiChanges()
    {
        if (sceneSourceRequested)
        {
            sceneSourceRequested = false;
            runtimeError.clear();
            // Validate/load the candidate before discarding the active scene.
            try
            {
                const auto path = scenePathUi[0] ? std::filesystem::absolute(std::filesystem::path(scenePathUi.data()))
                                                 : app::GetDirectoryWithExecutable().parent_path() /
                                                       "assets/gltf-sample-assets/Models/Sponza/glTF/Sponza.gltf";
                std::unique_ptr<avboit::DonutSceneHost> candidate;
                if (pendingSceneMode != 0)
                {
                    if (!std::filesystem::is_regular_file(path))
                        throw std::runtime_error("Scene file not found: " + path.string());
                    if (pendingSceneMode == 1)
                    {
                        candidate = std::make_unique<avboit::DonutSceneHost>();
                        candidate->Load(GetDevice(), path,
                                        app::GetDirectoryWithExecutable() / "shaders/framework" /
                                            app::GetShaderTypeName(GetDevice()->getGraphicsAPI()));
                    }
                    else
                        (void)avboit::LoadSponza(path);
                }
                GetDevice()->waitForIdle();
                donutScene = std::move(candidate);
                scene = avboit::SceneData{};
                sceneVertices = nullptr;
                scenePasses.clear();
                shadowPasses.clear();
                sceneTextures.clear();
                shadowFb = nullptr;
                shadowDepth = nullptr;
                textureCache.reset();
                commonPasses.reset();
                useSponza = pendingSceneMode != 0;
                useDonutScene = pendingSceneMode == 1;
                hardwareDepth = useDonutScene;
                sponzaPath = scenePathUi.data();
                ApplyCameraPreset(cameraPreset);
                rebuildRequested = true;
                preserveMaterialsOnRebuild = false;
            }
            catch (const std::exception &e)
            {
                runtimeError = e.what();
                pendingSceneMode = useSponza ? (useDonutScene ? 1 : 2) : 0;
            }
        }
        if (!rebuildRequested && transparency.DistortionApply().Enabled() != HasVfxDraws())
        {
            rebuildRequested = true;
            preserveMaterialsOnRebuild = true;
        }
        if (!rebuildRequested && !profilerDirty)
            return;
        GetDevice()->waitForIdle();
        profiler = avboit::GpuProfiler{};
        // Completed command-list instances retain query handles until collection.
        // Reclaim them before allocating another profiler ring during a rebuild.
        GetDevice()->runGarbageCollection();
        if (rebuildRequested)
        {
            if (preserveMaterialsOnRebuild)
            {
                // Geometry, material edits, animation/order state and host scene stay live.
                profiler.Initialize(GetDevice(), detailedProfiling, profilingEnabled);
                CreateRenderResources();
            }
            else
            {
                vertices.clear();
                bounds.clear();
                objectFirstRecords.clear();
                objectMaterials.clear();
                uploadedMaterials.clear();
                fixtureMotions.clear();
                objectCount = opaqueVertices = transparentVertices = 0;
                scenePasses.clear();
                shadowPasses.clear();
                sceneTextures.clear();
                Init();
            }
            GetDevice()->runGarbageCollection();
            preserveMaterialsOnRebuild = false;
        }
        else
            profiler.Initialize(GetDevice(), detailedProfiling, profilingEnabled);
        rebuildRequested = profilerDirty = false;
    }
    bool Init()
    {
        opaqueFirstRecords.clear();
        opaqueDraws.clear();
        geometryDraws.clear();
        geometryIndices.clear();
        uploadedFixtureTime = 0;
        uploadedOrder = 0;
        uploadedOrderSeed = orderSeed;
        orderFrame = 0;
        shadowDirty = true;
        volumeWidth = (Width + volumeScale - 1) / volumeScale;
        volumeHeight = (Height + volumeScale - 1) / volumeScale;
        rays = volumeWidth * volumeHeight;
        occupancyTiles = ((volumeWidth + 7) / 8) * ((volumeHeight + 7) / 8);
        auto *device = GetDevice();
        auto fs = std::make_shared<vfs::NativeFileSystem>();
        shaders = std::make_unique<engine::ShaderFactory>(device, fs,
                                                          app::GetDirectoryWithExecutable() / "shaders/avboit" /
                                                              app::GetShaderTypeName(device->getGraphicsAPI()));
        commands = device->createCommandList();
        if (!frostNormal)
        {
            // Precomputed linear RG8 mips also work without Donut's scene passes.
            engine::TextureCache cache(device, fs, nullptr);
            auto path = app::GetDirectoryWithExecutable() / "textures/frost-normal.dds";
            commands->open();
            auto loaded = cache.LoadTextureFromFile(path, false, nullptr, commands);
            if (!loaded || !loaded->texture)
                throw std::runtime_error("Cannot load frost normal: " + path.string());
            frostNormal = loaded->texture;
            commands->close();
            device->executeCommandList(commands);
            device->waitForIdle();
            frostSampler = device->createSampler(
                nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Wrap));
        }
        if (stressPreset >= 24 && !volumeDensity)
        {
            engine::TextureCache cache(device, fs, nullptr);
            auto path = app::GetDirectoryWithExecutable() / "textures/smoke-volume.dds";
            commands->open();
            auto loaded = cache.LoadTextureFromFile(path, false, nullptr, commands);
            if (!loaded || !loaded->texture)
                throw std::runtime_error("Cannot load volume density: " + path.string());
            volumeDensity = loaded->texture;
            commands->close();
            device->executeCommandList(commands);
            device->waitForIdle();
        }
        if (stressPreset < 24)
            volumeDensity = nullptr;
        profiler.Initialize(device, detailedProfiling, profilingEnabled);
        cameraBuffer = Buffer(sizeof(camera), nvrhi::Format::RGBA32_FLOAT, false, "Camera and material controls");
        bounds = {{{float(Width), float(Height), float(volumeScale * 8), 64}},
                  {{1.f / (float(Width) / Height * .57735027f), 1.f / .57735027f, .05f, 80}},
                  {{1, 0, 0, 0}}};
        // Opaque calibration wall made of real quads, then transparent RGB glass and smoke meshes.
        if (!useSponza)
            for (int y = -4; y < 5; ++y)
                for (int x = -7; x < 8; ++x)
                    Quad({x * .65f, y * .65f, 8}, {.32f, 0, 0}, {0, .32f, 0},
                         (x + y) & 1 ? F4{.08f, .12f, .17f, 1} : F4{.65f, .58f, .42f, 1}, {0, 0, 0}, 0, false);
        const unsigned emissiveFirst = unsigned(vertices.size());
        if (emissiveBalls)
        {
            struct Sphere
            {
                F3 center;
                float radius;
                F3 radiance;
            };
            std::vector<Sphere> spheres = {{{-1.f, .63f, 5.f}, .12f, {18, 9, 2}},
                                           {{-.48f, .05f, 5.3f}, .09f, {12, 12, 12}},
                                           {{-.92f, -.5f, 5.7f}, .18f, {2, 8, 20}},
                                           {{.15f, .43f, 6.4f}, .15f, {15, 2, 6}},
                                           {{1.25f, -.25f, 5.6f}, .22f, {2, 12, 5}}};
            if (stressPreset == 18)
                spheres = {{{-1.42f, .18f, 6.f}, .19f, {5, 6, 6}}, {{1.42f, .18f, 6.f}, .19f, {5, 6, 6}}};
            for (auto sphere : spheres)
            {
                if (emissiveFront)
                {
                    sphere.center[0] *= .45f;
                    sphere.center[1] *= .45f;
                    sphere.center[2] = 2.8f;
                }
                if (useSponza)
                    sphere.center[1] += 1.75f;
                opaqueFirstRecords.push_back(unsigned(vertices.size()));
                avboit::AppendEmissiveSphere(vertices, sphere.center, sphere.radius, sphere.radiance);
            }
        }
        else
            Quad({-.9f, .55f, 7}, {.15f, 0, 0}, {0, .15f, 0}, {15, 8, 2, 1}, {0, 0, 0}, 0, false);
        const unsigned emissiveEnd = unsigned(vertices.size());
        if (calibrationBoard)
            for (const auto &q : avboit::BuildInversionBoard().opaque)
                Quad(q.center, q.x, q.y, q.color, q.transmission, q.kind, false);
        auto fixture = avboit::BuildStressFixture(stressPreset);
        viewVertexRanges.clear();
        viewObjects.clear();
        for (const auto &q : fixture.opaque)
            Quad(q.center, q.x, q.y, q.color, q.transmission, q.kind, false);
        opaqueVertices = unsigned(vertices.size() / 4);
        if ((stressPreset == 0 && glassSphere) || fixture.sphere)
        {
            F3 center = {0, useSponza ? 1.8f : .05f, 4.5f};
            const float radius = 1.05f;
            objectFirstRecords.push_back(unsigned(vertices.size()));
            avboit::AppendGlassSphere(vertices, center, radius,
                                      rgbGlass ? F3{.04f, .65f, .94f} : F3{.7728f, .8648f, .8832f}, sphereRoughness);
            bounds.push_back({center[0] - radius, center[1] - radius, center[2] - radius,
                              float(1u | (sphereRoughness > 0 ? 2u : 0u))});
            bounds.push_back({center[0] + radius, center[1] + radius, center[2] + radius, 8192});
            // Analytic screen-space refraction clamps to +/-8192 pixels; use
            // the same conservative bound if this sphere also has frost.
            ++objectCount;
        }
        if (stressPreset == 0 && sceneGlassPanes)
        {
            Quad({-.55f, 0, 4}, {.85f, 0, .25f}, {0, 1, 0}, {.04f, .06f, .07f, 1}, {.25f, .8f, .95f}, 2, true, .75f, 0);
            Quad({.6f, .1f, 4.6f}, {.8f, 0, -.25f}, {0, 1, 0}, {.06f, .04f, .02f, 1}, {.95f, .35f, .2f}, 2, true, 0,
                 12);
        }
        if (stressPreset == 0 && showSmoke)
            for (int i = 0; i < 8; ++i)
                Quad({-.8f + i * .22f, -.15f, (glassSphere ? 2.4f + i * .08f : 2.9f + i * .12f)}, {.55f, 0, 0},
                     {0, .6f, 0}, {.55f, .62f, .7f, .45f}, {0, 0, 0}, 1, true);
        for (const auto &q : fixture.transparent)
        {
            unsigned first = unsigned(vertices.size()), object = objectCount;
            Quad(q.center, q.x, q.y, q.color, q.transmission, q.kind, true, q.roughness, q.distortion, q.shape);
            for (unsigned vertex = 0; vertex < 6; ++vertex)
            {
                auto &local = vertices[first + vertex * 4 + 3];
                local[0] = q.localX[0] + local[0] * q.localX[1];
            }
            if (q.motion != F3{})
                fixtureMotions.push_back({first, object, q.motion, q.frequency});
        }
        localVolumes.clear();
        localVolumeRecords.clear();
        for (auto volume : fixture.volumes)
        {
            if (useSponza)
                volume.center[1] += 1.75f;
            localVolumes.push_back(volume);
            localVolumeRecords.push_back(unsigned(vertices.size()));
            objectFirstRecords.push_back(unsigned(vertices.size()));
            for (unsigned corner = 0; corner < 6; ++corner)
            {
                vertices.push_back({volume.center[0], volume.center[1], volume.center[2], volume.textured ? 6.f : 5.f});
                vertices.push_back({volume.source[0], volume.source[1], volume.source[2], 1});
                vertices.push_back({volume.extinction[0], volume.extinction[1], volume.extinction[2], 0});
                vertices.push_back({volume.extent[0], volume.extent[1], volume.extent[2], float(corner)});
            }
            bounds.push_back({volume.center[0] - volume.extent[0], volume.center[1] - volume.extent[1],
                              volume.center[2] - volume.extent[2], 1});
            bounds.push_back({volume.center[0] + volume.extent[0], volume.center[1] + volume.extent[1],
                              volume.center[2] + volume.extent[2], 0});
            ++objectCount;
        }
        transparentVertices = unsigned(vertices.size() / 4) - opaqueVertices;
        bounds[2][1] = float(objectCount);
        float pattern = float(vfxMode == 3 ? 2 : vfxMode);
        Quad({0, 0, (vfxBehind || stressPreset == 8) ? 6.4f : 2.7f}, {1.2f, 0, 0}, {0, 1.2f, 0}, {0, 0, 0, 0},
             {1, 1, 1}, 3, false, 0, 0, pattern);
        vfxVertices = 6;
        if (vfxMode == 3)
        {
            Quad({.5f, .15f, 5.f}, {1.25f, 0, 0}, {0, 1.25f, 0}, {0, 0, 0, 0}, {1, 1, 1}, 3, false, 0, .35f, 2);
            vfxVertices = 12;
        }
        materials.Build(vertices, MaterialDefaults());
        emissiveMaterials.clear();
        for (unsigned v = emissiveFirst; v < emissiveEnd; v += 4)
        {
            const unsigned material = unsigned(vertices[v + 1][3]);
            if (std::find(emissiveMaterials.begin(), emissiveMaterials.end(), material) == emissiveMaterials.end())
                emissiveMaterials.push_back(material);
        }
        // Material construction consumes the original kind. Afterwards position.w
        // is free to mark view-space geometry; the shader reads kind from materials.
        for (const auto &range : viewVertexRanges)
            for (unsigned v = range[0]; v < range[1]; v += 4)
                vertices[v][3] = -1;
        for (unsigned actor = 0; actor < opaqueFirstRecords.size(); ++actor)
        {
            const unsigned end =
                actor + 1 < opaqueFirstRecords.size() ? opaqueFirstRecords[actor + 1] : opaqueVertices * 4;
            for (unsigned v = opaqueFirstRecords[actor]; v < end; v += 4)
                vertices[v + 2][3] = float(actor);
        }
        for (unsigned object = 0; object < objectFirstRecords.size(); ++object)
        {
            unsigned first = objectFirstRecords[object];
            unsigned end = object + 1 < objectFirstRecords.size() ? objectFirstRecords[object + 1]
                                                                  : (opaqueVertices + transparentVertices) * 4;
            objectMaterials.push_back(unsigned(vertices[first + 1][3]));
            // Material import has consumed record 2.w (roughness). Reuse the
            // otherwise unread vertex lane for actor identity, including shuffles.
            for (unsigned vertex = first; vertex < end; vertex += 4)
                vertices[vertex + 2][3] = float(object);
        }
        materialBuffer = Buffer(unsigned(materials.entries.size() * 64), nvrhi::Format::RGBA32_FLOAT, false,
                                "Transparency material table");
        actorHomeCenters.clear();
        for (unsigned a = 0; a < objectCount; ++a)
            actorHomeCenters.push_back(ActorCenter(a));
        actorGeometryDirty = false;
        actorGizmo.Reset();
        selectedActor = -1;
        selectedMaterial = 0;
        for (unsigned i = 0; i < materials.entries.size(); ++i)
            if (materials.entries[i].kind != 0)
            {
                selectedMaterial = int(i);
                break;
            }
        if (!objectMaterials.empty())
        {
            unsigned initialActor = 0;
            for (unsigned actor = 0; actor < objectMaterials.size(); ++actor)
                if (materials.entries[objectMaterials[actor]].kind >= 5)
                {
                    initialActor = actor;
                    break;
                }
            SelectActor(initialActor);
        }
        vertexBuffer = Buffer(unsigned(vertices.size() * 16), nvrhi::Format::RGBA32_FLOAT, false, "Scene vertices");
        boundsBuffer = Buffer(unsigned(bounds.size() * 16), nvrhi::Format::RGBA32_FLOAT, false, "Scene bounds");
        interfaceIndexBuffer = device->createBuffer(nvrhi::BufferDesc()
                                                        .setByteSize(std::max(4u, transparentVertices * 4))
                                                        .setIsIndexBuffer(true)
                                                        .setInitialState(nvrhi::ResourceStates::CopyDest)
                                                        .setKeepInitialState(true)
                                                        .setDebugName("Special interface indices"));
        geometryIndexBuffer = device->createBuffer(nvrhi::BufferDesc()
                                                       .setByteSize(std::max(4u, transparentVertices * 4))
                                                       .setIsIndexBuffer(true)
                                                       .setInitialState(nvrhi::ResourceStates::CopyDest)
                                                       .setKeepInitialState(true)
                                                       .setDebugName("Actor/material transparency indices"));
        opaqueIndexBuffer = device->createBuffer(nvrhi::BufferDesc()
                                                     .setByteSize(std::max(4u, opaqueVertices * 4))
                                                     .setIsIndexBuffer(true)
                                                     .setInitialState(nvrhi::ResourceStates::CopyDest)
                                                     .setKeepInitialState(true)
                                                     .setDebugName("Actor/material opaque indices"));
        auto opaqueSubmission =
            avboit::BuildGeometrySubmission(vertices, 0, opaqueVertices, materials, MaterialDefaults(), false);
        opaqueDraws = std::move(opaqueSubmission.draws);
        submittedVertices = vertices;
        interfaceIndices.clear();
        interfaceSelectionDirty = true;
        if (useSponza)
        {
            if (useDonutScene && !donutScene)
            {
                donutScene = std::make_unique<avboit::DonutSceneHost>();
                donutScene->Load(device,
                                 sponzaPath.empty() ? app::GetDirectoryWithExecutable().parent_path() /
                                                          "assets/gltf-sample-assets/Models/Sponza/glTF/Sponza.gltf"
                                                    : std::filesystem::absolute(sponzaPath),
                                 app::GetDirectoryWithExecutable() / "shaders/framework" /
                                     app::GetShaderTypeName(device->getGraphicsAPI()));
            }
        }
        CreateRenderResources();
        commands->open();
        commands->writeBuffer(vertexBuffer, vertices.data(), vertices.size() * 16);
        commands->writeBuffer(boundsBuffer, bounds.data(), bounds.size() * 16);
        if (!opaqueSubmission.indices.empty())
            commands->writeBuffer(opaqueIndexBuffer, opaqueSubmission.indices.data(),
                                  opaqueSubmission.indices.size() * 4);
        commands->close();
        device->executeCommandList(commands);
        if (useSponza)
        {
            if (!useDonutScene)
                LoadScene();
        }
        return true;
    }
    // Reconnect size/quality-dependent targets without rebuilding scene content.
    void CreateRenderResources()
    {
        auto *device = GetDevice();
        volumeWidth = (Width + volumeScale - 1) / volumeScale;
        volumeHeight = (Height + volumeScale - 1) / volumeScale;
        rays = volumeWidth * volumeHeight;
        occupancyTiles = ((volumeWidth + 7) / 8) * ((volumeHeight + 7) / 8);
        bounds[0] = {float(Width), float(Height), float(volumeScale * 8), 64};
        bounds[1][0] = 1.f / (float(Width) / Height * .57735027f);
        auto sceneFormat =
            (!hardwareDepth || halfComposition) ? nvrhi::Format::RGBA16_FLOAT : nvrhi::Format::R11G11B10_FLOAT;
        if (!opaque || opaque->getDesc().width != Width || opaque->getDesc().height != Height ||
            opaque->getDesc().format != sceneFormat)
        {
            opaque = Texture(Width, Height, sceneFormat, "SceneColor: opaque then OM transparency");
            depth = Texture(Width, Height, nvrhi::Format::D32, "Opaque depth");
        }
        if (!linear)
            linear = device->createSampler(
                nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
        opaqueFb =
            device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(opaque).setDepthAttachment(depth));
        using B = nvrhi::BindingSetItem;
        using D = nvrhi::BindingSetDesc;
        volumeLighting.Create(device, Shader("main", nvrhi::ShaderType::Compute, "shaders/VolumeShadow.hlsl"),
                              volumeDensity, linear, unsigned(localVolumes.size()), donutScene != nullptr);
        if (donutScene)
            donutScene->SetVolumeLighting(volumeLighting);
        avboit::TransparencyInputs inputs;
        inputs.applyVfx = HasVfxDraws();
        inputs.hardwareDepth = hardwareDepth;
        inputs.fixedDepth = fixedDepth;
        inputs.depthBudget = depthBudget;
        inputs.width = Width;
        inputs.height = Height;
        inputs.volumeWidth = volumeWidth;
        inputs.volumeHeight = volumeHeight;
        inputs.objectCount = objectCount;
        inputs.halfAccumulation = halfAccumulation;
        inputs.lowResolutionVolumes = lowResolutionVolumes;
        inputs.frostMipCount = frostMipCount;
        inputs.fullVfx = fullVfx;
        inputs.interfaceIndices = interfaceIndexBuffer;
        inputs.geometryIndices = geometryIndexBuffer;
        inputs.vertices = vertexBuffer;
        inputs.bounds = boundsBuffer;
        inputs.camera = cameraBuffer;
        inputs.materials = materialBuffer;
        inputs.transparentRange = nvrhi::BufferRange(opaqueVertices * 64, transparentVertices * 64);
        inputs.opaqueColorDepth = opaque;
        inputs.opaqueHardwareDepth = depth;
        inputs.linear = linear;
        inputs.frostNormal = frostNormal;
        inputs.volumeDensity = volumeDensity;
        inputs.volumeLights = volumeLighting.lights;
        inputs.regularLightDepth = donutScene ? donutScene->ShadowTexture() : volumeLighting.fallbackDepth.Get();
        inputs.lightMoments = volumeLighting.moments;
        inputs.lightOpticalDepth = volumeLighting.opticalDepth;
        inputs.frostSampler = frostSampler;
        // The sample currently shares compiled shader programs. The renderer
        // receives a separate shader set per material, so an actor can replace
        // any pass shader without changing draw grouping or other actors.
        avboit::MaterialShaders commonShaders;
        commonShaders.vertex = Shader("meshVertex", nvrhi::ShaderType::Vertex);
        commonShaders.opaquePixel = Shader("opaquePixel", nvrhi::ShaderType::Pixel);
        commonShaders.extinctionPixel = Shader("splatPixel", nvrhi::ShaderType::Pixel);
        commonShaders.interfacePixel = Shader("interfacePixel", nvrhi::ShaderType::Pixel);
        commonShaders.accumulationPixel = Shader("accumulationPixel", nvrhi::ShaderType::Pixel);
        commonShaders.distortionPixel = Shader("vfxPixel", nvrhi::ShaderType::Pixel);
        for (const auto &material : materials.entries)
        {
            auto shaders = commonShaders;
            shaders.cullMode = material.kind == 4 ? nvrhi::RasterCullMode::Back : nvrhi::RasterCullMode::None;
            if (material.kind == 5 || material.kind == 6)
            {
                shaders.extinctionVertex =
                    Shader("volumeVertex", nvrhi::ShaderType::Vertex, "shaders/LocalVolumeExtinction.hlsl");
                shaders.accumulationVertex = shaders.extinctionVertex;
                shaders.halfResolutionPixel =
                    Shader(material.kind == 6 ? "texturedAccumulationHalf" : "volumeAccumulationHalf",
                           nvrhi::ShaderType::Pixel, "shaders/LocalVolumeAccumulation.hlsl");
                shaders.extinctionPixel = Shader(material.kind == 6 ? "texturedExtinction" : "volumeExtinction",
                                                 nvrhi::ShaderType::Pixel, "shaders/LocalVolumeExtinction.hlsl");
                shaders.accumulationPixel = Shader(material.kind == 6 ? "texturedAccumulation" : "volumeAccumulation",
                                                   nvrhi::ShaderType::Pixel, "shaders/LocalVolumeAccumulation.hlsl");
            }
            inputs.materialShaders.push_back(std::move(shaders));
        }
        const unsigned vfxByteOffset = (opaqueVertices + transparentVertices) * 64;
        for (unsigned first = 0; first < vfxVertices; first += 6)
        {
            unsigned material = unsigned(vertices[(opaqueVertices + transparentVertices + first) * 4 + 1][3]);
            inputs.distortionBatches.push_back({nvrhi::BufferRange(vfxByteOffset + first * 64, 6 * 64), 6, material});
        }
        transparency.Create(device, inputs, [&](const char *entry, nvrhi::ShaderType type, const char *file)
                            { return Shader(entry, type, file); });
        masks = transparency.Volume().Occupancy();
        warp = transparency.Volume().Warp();
        physicalMasks = transparency.Volume().PhysicalMask();
        extinction = transparency.Volume().PackedExtinction();
        lut = transparency.Volume().Transmittance();
        numerator = transparency.Resolve().Numerator();
        denominator = transparency.Resolve().Denominator();
        totalTau = transparency.Resolve().TotalTau();
        backNumerator = transparency.Resolve().BackNumerator();
        blurPyramid = transparency.Gaussian().Output();
        blurRectangles = transparency.Gaussian().Rectangles();
        composedHdr = transparency.Composition().Output();
        opaquePass = Bind(D().addItem(B::TypedBuffer_SRV(0, vertexBuffer)), nvrhi::ShaderType::AllGraphics);
        opaqueMaterialPipelines.clear();
        for (const auto &material : inputs.materialShaders)
        {
            nvrhi::GraphicsPipelineDesc desc;
            desc.VS = material.vertex;
            desc.PS = material.opaquePixel;
            desc.bindingLayouts = {opaquePass.layout};
            desc.renderState.depthStencilState.depthTestEnable = true;
            desc.renderState.depthStencilState.depthWriteEnable = true;
            desc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Greater;
            desc.renderState.rasterState.cullMode = material.cullMode;
            opaqueMaterialPipelines.push_back(device->createGraphicsPipeline(desc, opaqueFb->getFramebufferInfo()));
        }
        if (!occupancySettings)
            occupancySettings = Buffer(16, nvrhi::Format::RGBA32_FLOAT, false, "Occupancy overlay settings");
        temporalAA.reset();
        if (taaEnabled)
            temporalAA = std::make_unique<avboit::TemporalAA>(device, transparency.DistortionApply().Output(), depth);
        displayPass = Bind(
            D().addItem(B::Texture_SRV(21, temporalAA ? temporalAA->Output() : transparency.DistortionApply().Output()))
                .addItem(B::TypedBuffer_SRV(22, physicalMasks))
                .addItem(B::Texture_SRV(30, transparency.Volume().ScreenTiles()))
                .addItem(B::TypedBuffer_SRV(32, occupancySettings))
                .addItem(B::Sampler(0, linear)),
            nvrhi::ShaderType::AllGraphics);
    }
    void LoadScene()
    {
        auto *device = GetDevice();
        auto fs = std::make_shared<vfs::NativeFileSystem>();
        auto assetPath = sponzaPath.empty() ? app::GetDirectoryWithExecutable().parent_path() /
                                                  "assets/gltf-sample-assets/Models/Sponza/glTF/Sponza.gltf"
                                            : std::filesystem::absolute(sponzaPath);
        if (!std::filesystem::is_regular_file(assetPath))
            throw std::runtime_error("Sponza not found: " + assetPath.string() +
                                     ". Run python tools/fetch_sponza.py, or pass --sponza-path <Sponza.gltf>.");
        scene = avboit::LoadSponza(assetPath);
        shadowDepth = Texture(2048, 2048, nvrhi::Format::D32, "Scene shadow depth");
        shadowFb = device->createFramebuffer(nvrhi::FramebufferDesc().setDepthAttachment(shadowDepth));
        sceneVertices =
            Buffer(unsigned(scene.vertices.size() * 16), nvrhi::Format::RGBA32_FLOAT, false, "Sponza vertex records");
        auto shaderFS = std::make_shared<vfs::RootFileSystem>();
        shaderFS->mount("/shaders/donut", app::GetDirectoryWithExecutable() / "shaders/framework" /
                                              app::GetShaderTypeName(device->getGraphicsAPI()));
        auto frameworkShaders = std::make_shared<engine::ShaderFactory>(device, shaderFS, "/shaders");
        commonPasses = std::make_shared<engine::CommonRenderPasses>(device, frameworkShaders);
        textureCache = std::make_unique<engine::TextureCache>(device, fs, nullptr);
        commands->open();
        commands->writeBuffer(sceneVertices, scene.vertices.data(), scene.vertices.size() * 16);
        auto fallback = [&](std::array<unsigned char, 4> pixel)
        {
            auto texture = device->createTexture(nvrhi::TextureDesc()
                                                     .setWidth(1)
                                                     .setHeight(1)
                                                     .setFormat(nvrhi::Format::RGBA8_UNORM)
                                                     .setInitialState(nvrhi::ResourceStates::ShaderResource)
                                                     .setKeepInitialState(true));
            commands->writeTexture(texture, 0, 0, pixel.data(), 4);
            sceneTextures.push_back(texture);
            return texture;
        };
        auto white = fallback({255, 255, 255, 255}), normal = fallback({128, 128, 255, 255}),
             mr = fallback({255, 255, 0, 255});
        auto materialSampler = device->createSampler(
            nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Wrap));
        auto load = [&](const std::filesystem::path &path, bool srgb, nvrhi::TextureHandle fallbackTexture)
        {
            if (path.empty())
                return fallbackTexture;
            auto loaded = textureCache->LoadTextureFromFile(path, srgb, commonPasses.get(), commands);
            if (!loaded || !loaded->texture)
                throw std::runtime_error("Cannot load scene texture: " + path.string());
            sceneTextures.push_back(loaded->texture);
            return loaded->texture;
        };
        using B = nvrhi::BindingSetItem;
        using D = nvrhi::BindingSetDesc;
        for (const auto &mesh : scene.primitives)
        {
            auto pass =
                Bind(D().addItem(B::TypedBuffer_SRV(0, sceneVertices, nvrhi::Format::UNKNOWN,
                                                    nvrhi::BufferRange(mesh.firstVertex * 80, mesh.vertexCount * 80)))
                         .addItem(B::Texture_SRV(16, load(mesh.albedo, true, white)))
                         .addItem(B::Texture_SRV(17, load(mesh.normal, false, normal)))
                         .addItem(B::Texture_SRV(18, load(mesh.metallicRoughness, false, mr)))
                         .addItem(B::Texture_SRV(19, shadowDepth))
                         .addItem(B::Sampler(0, materialSampler)),
                     nvrhi::ShaderType::AllGraphics);
            Graphics(pass, "sceneVertex", "scenePixel", opaqueFb, true, false, "shaders/Scene.hlsl");
            scenePasses.push_back(pass);
            auto shadowPass =
                Bind(D().addItem(B::TypedBuffer_SRV(0, sceneVertices, nvrhi::Format::UNKNOWN,
                                                    nvrhi::BufferRange(mesh.firstVertex * 80, mesh.vertexCount * 80)))
                         .addItem(B::Texture_SRV(16, load(mesh.albedo, true, white)))
                         .addItem(B::Sampler(0, materialSampler)),
                     nvrhi::ShaderType::AllGraphics);
            Graphics(shadowPass, "shadowVertex", "shadowPixel", shadowFb, true, false, "shaders/Scene.hlsl");
            shadowPasses.push_back(shadowPass);
        }
        commands->close();
        device->executeCommandList(commands);
        device->waitForIdle();
        printf("Sponza loaded: %zu triangles, %zu primitives\n", scene.vertices.size() / 15, scene.primitives.size());
    }
    bool KeyboardUpdate(int key, int scancode, int action, int mods) override
    {
        navigation.Key(key, scancode, action, mods);
        if (action == GLFW_PRESS)
        {
            if (key == GLFW_KEY_B)
                cubic = !cubic;
            if (key == GLFW_KEY_G)
                frost = !frost;
            if (key == GLFW_KEY_V)
                vfx = !vfx;
            if (key == GLFW_KEY_SPACE)
                animate = !animate;
            if (key == GLFW_KEY_HOME)
                navigation.SetPose(useSponza ? F3{-.6f, 1.75f, 0} : F3{0, 0, 0});
        }
        return false;
    }
    bool MouseButtonUpdate(int button, int action, int mods) override
    {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
            PickActor();
        navigation.Button(button, action, mods);
        return false;
    }
    bool MousePosUpdate(double x, double y) override
    {
        pointerX = x;
        pointerY = y;
        navigation.Mouse(x, y);
        return false;
    }
    void Animate(float elapsed) override
    {
        bool keyboardCaptured = false, mouseCaptured = false;
        if (ImGui::GetCurrentContext())
        {
            keyboardCaptured = ImGui::GetIO().WantCaptureKeyboard;
            mouseCaptured = ImGui::GetIO().WantCaptureMouse;
        }
        if (animate)
            time += std::min(elapsed, .1f);
        navigation.Tick(elapsed, keyboardCaptured || actorGizmo.Active(), mouseCaptured || actorGizmo.Active());
    }
    void BackBufferResizing() override { displayPass.graphics = nullptr; }
    void BackBufferResized(uint32_t width, uint32_t height, uint32_t) override
    {
        // Defer allocation until Render; zero-size minimize events retain resources.
        if (!width || !height)
            return;
        windowWidth = width;
        windowHeight = height;
        SyncWindowSize();
    }
    void Render(nvrhi::IFramebuffer *framebuffer) override
    {
        // CPU pacing is outside the measured GPU interval; never busy-spin.
        if (fpsLimit > 0 && lastFrameStart.time_since_epoch().count())
        {
            auto interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(1.0 / fpsLimit));
            std::this_thread::sleep_until(lastFrameStart + interval);
        }
        lastFrameStart = std::chrono::steady_clock::now();
        ApplyPendingUiChanges();
        if (benchmarkRequested)
        {
            benchmarkRequested = false;
            const bool oldAnimate = animate, oldOrder = animateOrder;
            animate = false;
            animateOrder = false;
            profilingEnabled = true;
            profilerDirty = true;
            ApplyPendingUiChanges();
            try
            {
                Benchmark(framebuffer, unsigned(std::clamp(interactiveBenchmarkFrames, 1, 600)),
                          csvPathUi[0] ? csvPathUi.data() : "native-benchmark-ui.csv",
                          "interactive fixed-camera; internal=" + std::to_string(Width) + "x" + std::to_string(Height));
                runtimeError.clear();
            }
            catch (const std::exception &e)
            {
                runtimeError = e.what();
            }
            animate = oldAnimate;
            animateOrder = oldOrder;
        }
        if (!displayPass.graphics)
            Graphics(displayPass, "fullscreenVertex", "displayPixel", framebuffer);
        commands->open();
        {
            using Stage = avboit::GpuProfiler;
            AVBOIT_GPU_SCOPE(commands, "AVBOIT Frame", profiler.Route(commands, Stage::Frame));
            {
                AVBOIT_GPU_SCOPE(commands, "Frame uploads");
                UpdateCamera();
            }
            {
                AVBOIT_GPU_SCOPE(commands, "Shadow", profiler.Route(commands, Stage::Shadow));
                if (donutScene)
                {
                    donutScene->UpdateLights(time, navigation.Eye(), sceneLighting);
                    donutScene->RecordShadow(commands, redrawShadows);
                }
                for (unsigned actor = 0; actor < localVolumes.size(); ++actor)
                {
                    const auto &material = materials.entries[unsigned(vertices[localVolumeRecords[actor] + 1][3])];
                    for (unsigned c = 0; c < 3; ++c)
                        localVolumes[actor].extinction[c] = material.transmission[c];
                }
                volumeLighting.Record(commands, donutScene ? donutScene->LightingData() : avboit::VolumeLightData{},
                                      localVolumes);
                // The current shadow casters, alpha masks and directional-light matrix
                // are static. Camera motion and transparent fixtures do not invalidate
                // this map. Init/scene rebuilding always resets shadowDirty.
                if (useSponza && !useDonutScene && (shadowDirty || redrawShadows))
                {
                    commands->clearDepthStencilTexture(shadowDepth, nvrhi::AllSubresources, true, 0, false, 0);
                    for (unsigned mesh = 0; mesh < shadowPasses.size(); ++mesh)
                        Draw(shadowPasses[mesh], shadowFb, scene.primitives[mesh].vertexCount);
                    shadowDirty = false;
                }
            }
            {
                AVBOIT_GPU_SCOPE(commands, "Opaque", profiler.Route(commands, Stage::Opaque));
                commands->clearTextureFloat(opaque, nvrhi::AllSubresources, nvrhi::Color(.025f, .04f, .06f, 80));
                commands->clearDepthStencilTexture(depth, nvrhi::AllSubresources, true, 0, false, 0);
                if (donutScene)
                    donutScene->Record(commands, opaqueFb, camera);
                for (unsigned mesh = 0; mesh < scenePasses.size(); ++mesh)
                    Draw(scenePasses[mesh], opaqueFb, scene.primitives[mesh].vertexCount);
                nvrhi::GraphicsState state;
                state.framebuffer = opaqueFb;
                state.bindings = {opaquePass.bindings};
                state.viewport.addViewportAndScissorRect(opaqueFb->getFramebufferInfo().getViewport());
                state.indexBuffer =
                    nvrhi::IndexBufferBinding().setBuffer(opaqueIndexBuffer).setFormat(nvrhi::Format::R32_UINT);
                for (const auto &draw : opaqueDraws)
                {
                    char label[96];
                    snprintf(label, sizeof(label), "Opaque actor %u / material %u", draw.actor, draw.material);
                    AVBOIT_GPU_SCOPE(commands, label);
                    state.pipeline = opaqueMaterialPipelines.at(draw.material);
                    commands->setGraphicsState(state);
                    commands->drawIndexed(
                        nvrhi::DrawArguments().setStartIndexLocation(draw.firstIndex).setVertexCount(draw.indexCount));
                }
            }
            avboit::TransparencyFrame frame;
            frame.sparseBlur = sparseBlur;
            frame.cubic = cubic;
            frame.denseExtinction = denseExtinction;
            frame.poisonExtinction = poisonExtinction;
            frame.interfaceDraws = interfaceDraws;
            frame.geometryDraws = geometryDraws;
            frame.poisonResolve = poisonResolve;
            frame.zeroDepth = zeroDepth;
            frame.lastBlurMip = maxBlurMip;
            if (vfx)
            {
                if (vfxLayers & 1)
                    frame.distortionBatches.push_back(0);
                if ((vfxLayers & 2) && vfxVertices > 6)
                    frame.distortionBatches.push_back(1);
            }
            transparency.Record(commands, frame,
                                [&](avboit::TransparencyPipeline::Stage stage, bool begin)
                                {
                                    static constexpr avboit::GpuProfiler::Stage profileStages[] = {
                                        Stage::Interface,       Stage::Bounds,
                                        Stage::AdaptiveZ,       Stage::PhysicalMask,
                                        Stage::ExtinctionClear, Stage::ExtinctionSplat,
                                        Stage::Integration,     Stage::ZeroDepth,
                                        Stage::Accumulation,    Stage::Resolve,
                                        Stage::BlurBase,        Stage::Blur,
                                        Stage::Composition,     Stage::Vfx,
                                        Stage::VfxApply};
                                    profiler.Event(commands, profileStages[unsigned(stage)], begin);
                                });
            {
                AVBOIT_GPU_SCOPE(commands, "Temporal AA", profiler.Route(commands, Stage::TemporalAA));
                if (temporalAA)
                    temporalAA->Record(commands, camera);
            }
            {
                AVBOIT_GPU_SCOPE(commands, "Display / tone map", profiler.Route(commands, Stage::Display));
                const F4 overlayData = {float(occupancyOverlay), occupancyOpacity,
                                        float(std::min(occupancySlice, int(depthBudget) - 1)),
                                        occupancyGrid ? 1.f : 0.f};
                commands->writeBuffer(occupancySettings, overlayData.data(), sizeof(overlayData));
                Draw(displayPass, framebuffer, 3);
            }
        } // End every GPU scope while the command list is still open.
        commands->close();
        GetDevice()->executeCommandList(commands);
    }
    void Benchmark(nvrhi::IFramebuffer *framebuffer, unsigned frames, const std::string &path,
                   const std::string &metadata)
    {
        GetDevice()->waitForIdle();
        avboit::StablePowerState stablePower(GetDevice());
        auto drain = [&]()
        {
            GetDevice()->waitForIdle();
            profiler.Collect();
            GetDevice()->runGarbageCollection();
        };
        for (unsigned frame = 0; frame < 30; ++frame)
        {
            Render(framebuffer);
            if ((frame + 1) % avboit::GpuProfiler::RingSize == 0)
                drain();
        }
        drain();
        profiler.ResetSamples();
        for (unsigned frame = 0; frame < frames; ++frame)
        {
            Render(framebuffer);
            if ((frame + 1) % avboit::GpuProfiler::RingSize == 0)
                drain();
        }
        drain();
        profiler.ExportCsv(path, metadata + "; stable_power_state=1");
    }
    void Capture(nvrhi::ITexture *texture, const char *path)
    {
        auto desc = texture->getDesc();
        auto staging = GetDevice()->createStagingTexture(desc, nvrhi::CpuAccessMode::Read);
        auto lutReadback = GetDevice()->createStagingTexture(lut->getDesc(), nvrhi::CpuAccessMode::Read);
        auto scheduleReadback = GetDevice()->createBuffer(nvrhi::BufferDesc()
                                                              .setByteSize(8)
                                                              .setCpuAccess(nvrhi::CpuAccessMode::Read)
                                                              .setInitialState(nvrhi::ResourceStates::CopyDest)
                                                              .setKeepInitialState(true));
        commands->open();
        commands->copyBuffer(scheduleReadback, 0, warp, 0, 8);
        commands->copyTexture(lutReadback, {}, lut, {});
        commands->copyTexture(staging, {}, texture, {});
        commands->close();
        GetDevice()->executeCommandList(commands);
        GetDevice()->waitForIdle();
        size_t pitch = 0;
        auto pixels = static_cast<const unsigned char *>(
            GetDevice()->mapStagingTexture(staging, {}, nvrhi::CpuAccessMode::Read, &pitch));
        if (!pixels)
            throw std::runtime_error("Readback failed");
        printf("Capture to %s, cwd %s\n", path, std::filesystem::current_path().string().c_str());
        printf("Interface submission: %u / %u vertices\n",
               allInterface ? transparentVertices : unsigned(interfaceIndices.size()), transparentVertices);
        printf("Interface actor draws: %u\n", unsigned(interfaceDraws.size()));
        std::ofstream file(path, std::ios::binary);
        if (!file)
            throw std::runtime_error("Capture file open failed");
        file << "P6\n" << desc.width << " " << desc.height << "\n255\n";
        for (unsigned y = 0; y < desc.height; ++y)
            for (unsigned x = 0; x < desc.width; ++x)
                file.write(reinterpret_cast<const char *>(pixels + y * pitch + x * 4), 3);
        GetDevice()->unmapStagingTexture(staging);
        auto schedule =
            static_cast<const unsigned *>(GetDevice()->mapBuffer(scheduleReadback, nvrhi::CpuAccessMode::Read));
        if (!schedule)
            throw std::runtime_error("Blur schedule readback failed");
        printf("Gaussian: masked base, full-screen reductions; maximum active mip %u\n", maxBlurMip);
        printf("B prime: transparency tiles (64x64), zero-cleared; opaque fallback outside cache\n");
        unsigned validSlices = std::min(depthBudget, schedule[1] + 2);
        GetDevice()->unmapBuffer(scheduleReadback);
        size_t lutPitch = 0;
        auto lutBytes = static_cast<const char *>(
            GetDevice()->mapStagingTexture(lutReadback, {}, nvrhi::CpuAccessMode::Read, &lutPitch));
        if (!lutBytes)
            throw std::runtime_error("LUT readback failed");
        std::ofstream lutFile("native-lut.rgba8", std::ios::binary);
        if (!lutFile)
            throw std::runtime_error("LUT capture file open failed");
        unsigned header[3] = {volumeWidth, volumeHeight, validSlices};
        lutFile.write(reinterpret_cast<const char *>(header), sizeof(header));
        for (unsigned z = 0; z < validSlices; ++z)
            for (unsigned y = 0; y < volumeHeight; ++y)
                lutFile.write(lutBytes + (z * volumeHeight + y) * lutPitch, volumeWidth * 4);
        GetDevice()->unmapStagingTexture(lutReadback);
        printf("Captured %u valid transmittance LUT slices\n", validSlices);
        // Diagnostic capture only, outside GPU timing. Preserve signed half values
        // so additive VFX and depth rejection can be tested without tone mapping.
        auto vfxReadback = GetDevice()->createStagingTexture(transparency.Distortion().Output()->getDesc(),
                                                             nvrhi::CpuAccessMode::Read);
        commands->open();
        commands->copyTexture(vfxReadback, {}, transparency.Distortion().Output(), {});
        commands->close();
        GetDevice()->executeCommandList(commands);
        GetDevice()->waitForIdle();
        size_t vfxPitch = 0;
        auto vfxBytes = static_cast<const char *>(
            GetDevice()->mapStagingTexture(vfxReadback, {}, nvrhi::CpuAccessMode::Read, &vfxPitch));
        if (!vfxBytes)
            throw std::runtime_error("VFX readback failed");
        std::ofstream vfxFile("native-vfx.rg16f", std::ios::binary);
        if (!vfxFile)
            throw std::runtime_error("VFX capture file open failed");
        unsigned vfxSize[2] = {transparency.Distortion().Output()->getDesc().width,
                               transparency.Distortion().Output()->getDesc().height};
        vfxFile.write(reinterpret_cast<const char *>(vfxSize), sizeof(vfxSize));
        for (unsigned y = 0; y < vfxSize[1]; ++y)
            vfxFile.write(vfxBytes + y * vfxPitch, vfxSize[0] * 4);
        GetDevice()->unmapStagingTexture(vfxReadback);
        {
            // Diagnostic captures are outside rendering/timing.
            auto captureRaw =
                [&](nvrhi::ITexture *source, const char *filename, unsigned bytesPerPixel, unsigned slice = 0)
            {
                if (!source)
                    return;
                auto readback = GetDevice()->createStagingTexture(source->getDesc(), nvrhi::CpuAccessMode::Read);
                commands->open();
                nvrhi::TextureSlice subresource;
                subresource.arraySlice = slice;
                commands->copyTexture(readback, subresource, source, subresource);
                commands->close();
                GetDevice()->executeCommandList(commands);
                GetDevice()->waitForIdle();
                size_t rowPitch = 0;
                auto bytes = static_cast<const char *>(
                    GetDevice()->mapStagingTexture(readback, subresource, nvrhi::CpuAccessMode::Read, &rowPitch));
                if (!bytes)
                    throw std::runtime_error("Depth ABI readback failed");
                std::ofstream file(filename, std::ios::binary);
                if (!file)
                    throw std::runtime_error("Depth ABI capture open failed");
                unsigned size[2] = {source->getDesc().width, source->getDesc().height};
                file.write(reinterpret_cast<const char *>(size), sizeof(size));
                for (unsigned y = 0; y < size[1]; ++y)
                {
                    if (source->getDesc().format == nvrhi::Format::D32 && bytesPerPixel == 16)
                    {
                        const auto row = reinterpret_cast<const float *>(bytes + y * rowPitch);
                        for (unsigned x = 0; x < size[0]; ++x)
                        {
                            const float d = row[x];
                            F4 hit = d > 0.f ? F4{camera[5][1] / (d - camera[5][0]), 0, 0, 1} : F4{};
                            file.write(reinterpret_cast<const char *>(hit.data()), 16);
                        }
                    }
                    else
                        file.write(bytes + y * rowPitch, size[0] * bytesPerPixel);
                }
                GetDevice()->unmapStagingTexture(readback);
            };
            // Capture the source/mip producer rectangles for domain-aware checks.
            auto regionReadback = GetDevice()->createBuffer(nvrhi::BufferDesc()
                                                                .setByteSize(11 * 16)
                                                                .setCpuAccess(nvrhi::CpuAccessMode::Read)
                                                                .setInitialState(nvrhi::ResourceStates::CopyDest)
                                                                .setKeepInitialState(true));
            commands->open();
            commands->copyBuffer(regionReadback, 0, blurRectangles, 0, 11 * 16);
            commands->close();
            GetDevice()->executeCommandList(commands);
            GetDevice()->waitForIdle();
            const void *regionBytes = GetDevice()->mapBuffer(regionReadback, nvrhi::CpuAccessMode::Read);
            if (!regionBytes)
                throw std::runtime_error("Region capture mapping failed");
            {
                std::array<std::array<unsigned, 4>, 11> regions{};
                std::memcpy(regions.data(), regionBytes, sizeof(regions));
                for (auto &r : regions)
                    if (r[2] <= r[0] || r[3] <= r[1])
                        r = {};
                std::ofstream file("native-regions.u32", std::ios::binary);
                file.write(reinterpret_cast<const char *>(regions.data()), sizeof(regions));
            }
            GetDevice()->unmapBuffer(regionReadback);
            auto workMask = transparency.Gaussian().WorkMask();
            auto workReadback = GetDevice()->createBuffer(nvrhi::BufferDesc()
                                                              .setByteSize(workMask->getDesc().byteSize)
                                                              .setCpuAccess(nvrhi::CpuAccessMode::Read)
                                                              .setInitialState(nvrhi::ResourceStates::CopyDest)
                                                              .setKeepInitialState(true));
            commands->open();
            commands->copyBuffer(workReadback, 0, workMask, 0, workMask->getDesc().byteSize);
            commands->close();
            GetDevice()->executeCommandList(commands);
            GetDevice()->waitForIdle();
            const void *workBytes = GetDevice()->mapBuffer(workReadback, nvrhi::CpuAccessMode::Read);
            if (!workBytes)
                throw std::runtime_error("Tile mask capture mapping failed");
            {
                std::ofstream file("native-work-masks.u32", std::ios::binary);
                file.write(static_cast<const char *>(workBytes), workMask->getDesc().byteSize);
            }
            GetDevice()->unmapBuffer(workReadback);
            {
                std::ofstream file("native-work-layout.u32", std::ios::binary);
                const auto &layout = transparency.Gaussian().ScheduleLayout();
                for (unsigned stage = 0; stage < 11; ++stage)
                {
                    unsigned info[] = {layout.width[stage], layout.height[stage], layout.offset[stage],
                                       layout.capacity[stage]};
                    file.write(reinterpret_cast<char *>(info), sizeof(info));
                }
            }
            if (temporalAA)
                captureRaw(temporalAA->Output(), "native-taa.rgba16f", 8);
            captureRaw(transparency.Volume().ScreenTiles(), "native-screen-tiles.r32u", 4);
            captureRaw(composedHdr,
                       composedHdr->getDesc().format == nvrhi::Format::RGBA16_FLOAT ? "native-composition.rgba16f"
                                                                                    : "native-composition.r11g11b10",
                       composedHdr->getDesc().format == nvrhi::Format::RGBA16_FLOAT ? 8 : 4);
            auto captureBuffer = [&](nvrhi::IBuffer *source, const char *path)
            {
                auto readback = GetDevice()->createBuffer(nvrhi::BufferDesc()
                                                              .setByteSize(source->getDesc().byteSize)
                                                              .setCpuAccess(nvrhi::CpuAccessMode::Read)
                                                              .setInitialState(nvrhi::ResourceStates::CopyDest)
                                                              .setKeepInitialState(true));
                commands->open();
                commands->copyBuffer(readback, 0, source, 0, source->getDesc().byteSize);
                commands->close();
                GetDevice()->executeCommandList(commands);
                GetDevice()->waitForIdle();
                auto bytes = GetDevice()->mapBuffer(readback, nvrhi::CpuAccessMode::Read);
                if (!bytes)
                    throw std::runtime_error("Buffer capture mapping failed");
                std::ofstream file(path, std::ios::binary);
                file.write(static_cast<const char *>(bytes), source->getDesc().byteSize);
                GetDevice()->unmapBuffer(readback);
            };
            captureBuffer(volumeLighting.lights, "native-volume-lights.f32");
            if (donutScene && !localVolumes.empty() && sceneLighting.volumeShadows)
                for (unsigned slice = 0; slice < 3; ++slice)
                {
                    const std::string suffix = std::to_string(slice);
                    captureRaw(volumeLighting.moments, ("native-light-moments-" + suffix + ".unorm16").c_str(), 8,
                               slice);
                    captureRaw(volumeLighting.opticalDepth, ("native-light-tau-" + suffix + ".f16").c_str(), 2, slice);
                }
            captureBuffer(warp, "native-warp.u32");
            captureBuffer(transparency.Volume().ZeroDrawArgs(), "native-zero-draw-args.u32");
            captureBuffer(transparency.Volume().ZeroCandidates(), "native-zero-candidates.u32");
            // Existing integration metadata, also consumed directly by the depth VS.
            captureRaw(transparency.Volume().ZeroSlice(), "native-zero-slice.r32u", 4);
            captureRaw(transparency.Interface().HardwareDepth(), "native-interface.rgba32f", 16);
            captureRaw(transparency.Interface().HardwareDepth(), "native-interface-depth.r32f", 4);
            captureRaw(transparency.Interface().TransmissionSigma(), "native-interface-surface.rgba16f", 8);
            captureRaw(transparency.Interface().Offset(), "native-interface-offset.rg16f", 4);
            captureRaw(transparency.Resolve().Background(), "native-background.rgba16f", 8);
            captureRaw(transparency.Gaussian().Output(), "native-frost-base.rgba16f", 8);
            captureRaw(transparency.Resolve().Numerator(), "native-numerator.raw", halfAccumulation ? 8 : 4);
            captureRaw(transparency.Resolve().Denominator(), "native-denominator.raw", halfAccumulation ? 8 : 4);
            captureRaw(transparency.Resolve().TotalTau(), "native-total-tau.raw", halfAccumulation ? 8 : 4);
            captureRaw(transparency.Resolve().BackNumerator(), "native-back-numerator.raw", halfAccumulation ? 8 : 4);
            if (hardwareDepth)
            {
                captureRaw(depth, "native-depth.r32f", 4);
                captureRaw(opaque,
                           opaque->getDesc().format == nvrhi::Format::RGBA16_FLOAT ? "native-scene-color.rgba16f"
                                                                                   : "native-scene-color.r11g11b10",
                           opaque->getDesc().format == nvrhi::Format::RGBA16_FLOAT ? 8 : 4);
            }
        }
    }
};

// Test-only driver compiled in this translation unit to access the sample host.
#include "tests/ViewerSelfTests.inl"

int main(int argc, const char **argv)
{
    avboit::ViewerOptions options;
    try
    {
        options = avboit::ParseViewerOptions(argc, argv);
    }
    catch (const std::exception &error)
    {
        fprintf(stderr, "%s\nUse --help for available options.\n", error.what());
        return 2;
    }
    if (options.help)
    {
        printf("%s", avboit::ViewerHelp());
        return 0;
    }
    const auto &o = options;

    log::ConsoleApplicationMode();
    std::atomic<unsigned> validationErrors{0};
    auto previousLogger = log::GetCallback();
    log::SetCallback(
        [&](log::Severity severity, const char *message)
        {
            if (severity >= log::Severity::Error)
                ++validationErrors;
            previousLogger(severity, message);
        });
    glfwSetErrorCallback([](int code, const char *message) { fprintf(stderr, "GLFW %d: %s\n", code, message); });

    auto manager = std::unique_ptr<app::DeviceManager>(app::DeviceManager::Create(nvrhi::GraphicsAPI::D3D12));
    app::DeviceCreationParameters params;
    params.backBufferWidth = 1280;
    params.backBufferHeight = 768;
    // displayPixel already applies the transfer curve; avoid a second sRGB encode.
    params.swapChainFormat = nvrhi::Format::RGBA8_UNORM;
    params.enableNvrhiValidationLayer = true;
    bool created = o.headless ? manager->CreateHeadlessDevice(params)
                              : manager->CreateWindowDeviceAndSwapChain(params, "AVBOIT experiment - DX12");
    if (!created)
    {
        log::ResetCallback();
        return 1;
    }

    int result = 0;
    try
    {
        printf("Device ready, headless=%d\n", o.headless);
        NativeViewer viewer(manager.get());
        viewer.SetFpsLimit(o.fpsLimit);
        viewer.SetTileResolve(o.tileResolve);
        viewer.SetRefraction(o.planarIor, o.sphereIor, o.refractionStrength, o.normalSphere);
        viewer.SetStress(o.stressPreset, o.fixtureTime);
        viewer.SetSponzaPath(o.sponzaAssetPath);
        viewer.SetCsvPath(o.benchmarkPath);
        viewer.SetFrostChain(o.frostMipCount);
        viewer.SetFrostDetailRadius(o.frostDetailRadius);
        viewer.SetFrostNormalTiling(o.frostNormalTiling);
        viewer.SetSparseBlur(o.sparseBlur);
        viewer.SetCubicTrilinear(o.cubicTrilinear);
        viewer.SetPoisonResolve(o.poisonResolve);
        viewer.SetVfxMaterial(o.vfxMode, o.vfxStrength, o.vfxLayers);
        viewer.SetVolumeScale(o.volumeScale);
        viewer.SetDepthBudget(o.depthBudget, o.fixedDepth);
        viewer.SetCompactOptions(o.halfComposition, o.fullVfx, o.allInterface);
        viewer.SetZeroOptions(o.zeroDepth, o.mirrorBlur);
        viewer.SetBackgroundOptions(o.qBound, o.protectBackground, o.backgroundThreshold, o.zeroTShortcut);
        viewer.SetPrecisionAndOrder(o.halfAccumulation, o.submissionOrder, o.orderSeed);
        viewer.SetRedrawShadows(o.redrawShadows);
        viewer.SetFixtureOptions(o.emissiveBalls, o.showSmoke, o.glassSphere, o.rgbGlass, o.sphereRoughness);
        viewer.SetProfilingOptions(o.detailedProfiling, o.benchmarkFrames > 0 || !o.headless || o.uiCapture);
        viewer.SetExtinctionOptions(o.denseExtinction, o.poisonExtinction, o.slopeSplat);
        viewer.SetRenderSize(o.renderWidth, o.renderHeight);
        viewer.SetFollowWindow(o.followWindow);
        viewer.SetHardwareDepth(o.hardwareDepth);
        viewer.SetDonutScene(o.donutScene);
        viewer.SetSceneLighting(o.sunIntensity, o.spotIntensity, o.volumeShadows);
        viewer.SetVolumeResolution(o.lowResolutionVolumes, o.stochasticVolumeUpsample);
        viewer.SetTaa(o.taa);
        viewer.SetVolumeMarch(o.volumeStepSize, o.volumeLowT, o.volumeStepScale, o.volumeMaxSteps);
        viewer.SetCalibration(o.calibrationBoard, o.emissiveGain);
        viewer.SetEmissiveFront(o.emissiveFront);
        viewer.SetOccupancyOverlay(o.occupancyOverlay, o.occupancySlice);
        auto configureCamera = [&](unsigned preset)
        { viewer.Configure(o.cubic, o.frost, o.sponza, o.vfx, o.behind, o.fullBlur, preset, o.sigmaLimit); };
        configureCamera(o.cameraPreset);
        viewer.Init();
        printf("Renderer initialized\n");

        std::unique_ptr<avboit::ViewerUi> gui;
        if (!o.headless || o.uiCapture)
            gui = std::make_unique<avboit::ViewerUi>(manager.get(), [&viewer]() { viewer.DrawUi(); });

        if (o.headless)
        {
            auto device = manager->GetDevice();
            auto texture = device->createTexture(nvrhi::TextureDesc()
                                                     .setWidth(o.uiCapture ? 1280 : o.renderWidth)
                                                     .setHeight(o.uiCapture ? 768 : o.renderHeight)
                                                     .setFormat(nvrhi::Format::RGBA8_UNORM)
                                                     .setIsRenderTarget(true)
                                                     .setInitialState(nvrhi::ResourceStates::RenderTarget)
                                                     .setKeepInitialState(true));
            auto framebuffer = device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(texture));
            viewer.BackBufferResized(texture->getDesc().width, texture->getDesc().height, 1);
            if (o.actorEditorTest)
                viewer.TestActorEditing(framebuffer);
            if (o.taa)
                viewer.TestTemporalAA(framebuffer);
            // Warm a different view first, exposing stale sparse resources after camera changes.
            configureCamera(o.cameraPreset == 6 ? 0 : (o.cameraPreset + 2) % 6);
            viewer.Render(framebuffer);
            configureCamera(o.cameraPreset);
            for (int frame = 0; frame < (o.taa ? 32 : 3); ++frame)
            {
                viewer.Render(framebuffer);
                printf("Rendered %d\n", frame);
            }

            if (o.benchmarkFrames)
            {
                std::string metadata = "adapter=" + std::string(manager->GetRendererString()) +
                                       "; internal=" + std::to_string(o.renderWidth) + "x" +
                                       std::to_string(o.renderHeight) + "; command=";
                for (int arg = 1; arg < argc; ++arg)
                    metadata += std::string(argv[arg]) + " ";
                viewer.Benchmark(framebuffer, o.benchmarkFrames, o.benchmarkPath, metadata);
            }
            if (o.uiCapture)
            {
                for (unsigned frame = 0; frame < 60; ++frame)
                {
                    viewer.Render(framebuffer);
                    device->waitForIdle();
                    gui->Animate(1.f / 60.f);
                    gui->Render(framebuffer);
                    if (gui->ErrorCount())
                        throw std::runtime_error("ImGui reported errors during UI capture");
                }
            }
            if (o.uiSelfTest)
                viewer.TestUiInteractions(*gui, framebuffer);
            viewer.Capture(texture, "native-raster.ppm");
        }
        else
        {
            manager->AddRenderPassToBack(&viewer);
            manager->AddRenderPassToBack(gui.get());
            manager->RunMessageLoop();
            manager->RemoveRenderPass(gui.get());
            manager->RemoveRenderPass(&viewer);
        }
        manager->GetDevice()->waitForIdle();
    }
    catch (const std::exception &error)
    {
        fprintf(stderr, "%s\n", error.what());
        result = 1;
    }
    manager->Shutdown();
    if (validationErrors)
        result = 1;
    log::ResetCallback();
    return result;
}
