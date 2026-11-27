// SPDX-License-Identifier: MIT
#pragma once
#include <string>

namespace avboit
{
// One size policy for CLI, custom UI and window-follow rendering.
constexpr unsigned AlignRenderDimension(unsigned value)
{
    return (value + 3u) & ~3u;
}
enum class BrickLod { Balanced, Performance, Low };

struct ViewerOptions
{
    bool gpuValidation = false;
    bool help = false, headless = false, uiCapture = false, uiSelfTest = false, actorEditorTest = false;
    bool cubic = true, frost = true, sparseBlur = true, fullBlur = false;
    bool cubicTrilinear = false;
    bool lutMinFilter = true;
    float lutMinRetain = .5f;
    bool lutIgn = false;
    bool lutPlane = false;
    float lutJitterStrength = 1.f;
    bool sponza = false, emissiveBalls = false, emissiveFront = false, showSmoke = true, glassSphere = false,
         rgbGlass = false;
    bool vfx = false, behind = false, normalSphere = false;
    bool denseExtinction = false, poisonExtinction = false, detailedProfiling = true;
    bool slopeSplat = true;
    bool followWindow = false, taa = true;
    bool redrawShadows = false, hardwareDepth = false, donutScene = false, legacyScene = false;
    float sphereRoughness = 0, planarIor = 1.3f, sphereIor = 1.5f, refractionStrength = 1;
    float sunIntensity = 2.f, spotIntensity = 80.f;
    bool volumeShadows = true;
    bool gameplayGrenade = false, grenadeSkip = false;
    int grenadeStorage = 0;
    unsigned smokeShots = 0; // Deterministic capsule preview, 0..15.
    float smokeShotRadius = .45f;
    bool smokeTrailReference = false, smokeTrailRebuild = false; // Offline comparison only.
    bool smokeAo = true;
    bool bakedSmoke = false, smokeMask = true, smokeDoorOpen = true;
    bool smokeBrickSkip = true, playSmoke = true;
    float smokeFrame = 15.f;
    bool lowResolutionVolumes = true, temporalVolumeDithering = false;
    bool volumeUpsampleTest = false;
    int volumeScatteringOctaves = 2;
    float initialVolumeExtinction = -1.f, shellOffsetBins = 0;
    bool flattenShell = true;
    unsigned magazineOrdering = 2; // Scene 34: geometric, uniform flat group, depth-weighted flat group.
    bool magazineNeighbours = false;
    float groupWeightStrength = 4.f, groupDepthRange = 2.f;
    bool stochasticSmokeFilter = false, quadVolumeT = true, eightVolumeT = false;
    unsigned accumulationAudit = 0; // Offline shader ablation, not a quality option.
    bool vdbRayHull = false;
    bool vdbGroupMarch = false;
    bool vdbBrickMarch = false;
    BrickLod brickLod = BrickLod::Balanced;
    bool coarseBrickSteps = false;
    bool vdbBrickCoarseLight = false;
    bool vdbBrickVertexLight = false;
    bool indexedBrickQuads = true;
    bool classifyBricks = false, classifyCubes = false;
    bool vdbBrickCache = false;
    bool vdbProxyStencil = false, vdbCompute = false, vdbNearRayLod = false, vdbQuarterReference = false, vdbSharedLighting = false, marchHeatmap = false;
    bool showSkippedSteps = false, volumeEarlyOut = true;
    bool volumeLutSkip = false, volumeMarchStats = false, volumeReferenceSampling = false;
    float volumeStepSize = .5f, volumeLowT = .1f, volumeStepScale = 1.f;
    int volumeMaxSteps = 256;
    float fixtureTime = 0, sigmaLimit = 24;
    float frostDetailRadius = 24;
    float frostNormalTiling = 1;
    unsigned cameraPreset = 0, stressPreset = 0, benchmarkFrames = 0;
    unsigned volumeScale = 8, depthBudget = 128, fpsLimit = 120;
    unsigned occupancyOverlay = 0, occupancySlice = 0;
    bool fixedDepth = false, calibrationBoard = false;
    float emissiveGain = 1;
    bool qBound = true, protectBackground = false, zeroTShortcut = false, zeroDepth = true, mirrorBlur = true;
    float backgroundThreshold = .001f;
    unsigned renderWidth = 2560, renderHeight = 1440;
    bool halfAccumulation = false, halfComposition = false, fullVfx = true, allInterface = false;
    unsigned frostMipCount = 3;
    bool poisonResolve = false, tileResolve = true;
    unsigned submissionOrder = 0, orderSeed = 1;
    unsigned vfxMode = 0, vfxLayers = 3;
    float vfxStrength = 20;
    std::string sponzaAssetPath, smokeSequencePath, benchmarkPath = "native-benchmark.csv";
};
// Parse before device creation: help and invalid arguments never require a GPU.
ViewerOptions ParseViewerOptions(int argc, const char *const *argv);
const char *ViewerHelp();
} // namespace avboit
