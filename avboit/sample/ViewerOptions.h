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
struct ViewerOptions
{
    bool help = false, headless = false, uiCapture = false, uiSelfTest = false, actorEditorTest = false;
    bool cubic = true, frost = true, sparseBlur = true, fullBlur = false;
    bool cubicTrilinear = false;
    bool sponza = false, emissiveBalls = false, emissiveFront = false, showSmoke = true, glassSphere = false,
         rgbGlass = false;
    bool vfx = false, behind = false, normalSphere = false;
    bool denseExtinction = false, poisonExtinction = false, detailedProfiling = true;
    bool slopeSplat = true;
    bool followWindow = false, taa = false;
    bool redrawShadows = false, hardwareDepth = false, donutScene = false, legacyScene = false;
    float sphereRoughness = 0, planarIor = 1.3f, sphereIor = 1.5f, refractionStrength = 1;
    float sunIntensity = 2.f, spotIntensity = 80.f;
    bool volumeShadows = true;
    bool lowResolutionVolumes = false, temporalVolumeDithering = false;
    bool ditheredVolumeUpsample = true, volumeUpsampleTest = false;
    float initialVolumeExtinction = -1.f;
    float volumeStepSize = .5f, volumeLowT = .1f, volumeStepScale = 4.f;
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
    std::string sponzaAssetPath, benchmarkPath = "native-benchmark.csv";
};
// Parse before device creation: help and invalid arguments never require a GPU.
ViewerOptions ParseViewerOptions(int argc, const char *const *argv);
const char *ViewerHelp();
} // namespace avboit
