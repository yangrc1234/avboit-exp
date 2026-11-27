// SPDX-License-Identifier: MIT
#include "sample/ViewerOptions.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace avboit
{
namespace
{
struct Flag
{
    const char *name;
    bool ViewerOptions::*member;
    bool value;
};
constexpr Flag Flags[] = {
    {"--no-taa", &ViewerOptions::taa, false},
    {"--volume-full-res", &ViewerOptions::lowResolutionVolumes, false},
    {"--no-lut-min-filter", &ViewerOptions::lutMinFilter, false},
    {"--lut-ign", &ViewerOptions::lutIgn, true},
    {"--lut-plane", &ViewerOptions::lutPlane, true},
    {"--magazine-neighbours", &ViewerOptions::magazineNeighbours, true},
    {"--no-smoke-brick-skip", &ViewerOptions::smokeBrickSkip, false},
    {"--baked-smoke", &ViewerOptions::bakedSmoke, true},
    {"--smoke-paused", &ViewerOptions::playSmoke, false},
    {"--grenade-skip", &ViewerOptions::grenadeSkip, true},
    {"--gameplay-grenade", &ViewerOptions::gameplayGrenade, true},
    {"--smoke-trail-reference", &ViewerOptions::smokeTrailReference, true},
    {"--smoke-trail-rebuild", &ViewerOptions::smokeTrailRebuild, true},
    {"--no-smoke-ao", &ViewerOptions::smokeAo, false},
    {"--no-smoke-mask", &ViewerOptions::smokeMask, false},
    {"--smoke-door-closed", &ViewerOptions::smokeDoorOpen, false},
    {"--smoke-stochastic-filter", &ViewerOptions::stochasticSmokeFilter, true},
    {"--volume-reference-sampling", &ViewerOptions::volumeReferenceSampling, true},
    {"--volume-quad-t", &ViewerOptions::quadVolumeT, true},
    {"--no-volume-quad-t", &ViewerOptions::quadVolumeT, false},
    {"--volume-eight-t", &ViewerOptions::eightVolumeT, true},
    {"--volume-lut-skip", &ViewerOptions::volumeLutSkip, true},
    {"--vdb-shared-lighting", &ViewerOptions::vdbSharedLighting, true},
    {"--vdb-quarter-ray-reference", &ViewerOptions::vdbQuarterReference, true},
    {"--vdb-near-ray-lod", &ViewerOptions::vdbNearRayLod, true},
    {"--vdb-proxy-stencil", &ViewerOptions::vdbProxyStencil, true},
    {"--vdb-ray-hull", &ViewerOptions::vdbRayHull, true},
    {"--vdb-group-march", &ViewerOptions::vdbGroupMarch, true},
    {"--vdb-brick-double-step", &ViewerOptions::coarseBrickSteps, true},
    {"--vdb-brick-march", &ViewerOptions::vdbBrickMarch, true},
    {"--vdb-brick-vertex-light", &ViewerOptions::vdbBrickVertexLight, true},
    {"--vdb-brick-classify", &ViewerOptions::classifyBricks, true},
    {"--vdb-brick-classify-cube", &ViewerOptions::classifyCubes, true},
    {"--vdb-brick-cache", &ViewerOptions::vdbBrickCache, true},
    {"--no-vdb-brick-indices", &ViewerOptions::indexedBrickQuads, false},
    {"--vdb-brick-coarse-light", &ViewerOptions::vdbBrickCoarseLight, true},
    {"--vdb-compute", &ViewerOptions::vdbCompute, true},
    {"--no-volume-early-out", &ViewerOptions::volumeEarlyOut, false},
    {"--volume-skip-view", &ViewerOptions::showSkippedSteps, true},
    {"--volume-step-view", &ViewerOptions::marchHeatmap, true},
    {"--volume-march-stats", &ViewerOptions::volumeMarchStats, true},
    {"--dense-blur", &ViewerOptions::sparseBlur, false},
    {"--no-tile-resolve", &ViewerOptions::tileResolve, false},
    {"--poison-resolve", &ViewerOptions::poisonResolve, true},
    {"--help", &ViewerOptions::help, true},
    {"-h", &ViewerOptions::help, true},
    {"--headless", &ViewerOptions::headless, true},
    {"--ui-capture", &ViewerOptions::uiCapture, true},
    {"--ui-self-test", &ViewerOptions::uiSelfTest, true},
    {"--linear", &ViewerOptions::cubic, false},
    {"--cubic-trilinear", &ViewerOptions::cubicTrilinear, true},
    {"--lut-min-filter", &ViewerOptions::lutMinFilter, true},
    {"--no-frost", &ViewerOptions::frost, false},
    {"--sparse-blur", &ViewerOptions::sparseBlur, true},
    {"--full-blur", &ViewerOptions::fullBlur, true},
    {"--sponza", &ViewerOptions::sponza, true},
    {"--emissive-balls", &ViewerOptions::emissiveBalls, true},
    {"--emissive-front", &ViewerOptions::emissiveFront, true},
    {"--no-smoke", &ViewerOptions::showSmoke, false},
    {"--glass-sphere", &ViewerOptions::glassSphere, true},
    {"--rgb-glass", &ViewerOptions::rgbGlass, true},
    {"--normal-sphere", &ViewerOptions::normalSphere, true},
    {"--vfx", &ViewerOptions::vfx, true},
    {"--vfx-behind", &ViewerOptions::behind, true},
    {"--dense-extinction", &ViewerOptions::denseExtinction, true},
    {"--poison-extinction", &ViewerOptions::poisonExtinction, true},
    {"--no-slope-splat", &ViewerOptions::slopeSplat, false},
    {"--profile-frame-only", &ViewerOptions::detailedProfiling, false},
    {"--redraw-shadows", &ViewerOptions::redrawShadows, true},
    {"--hardware-depth", &ViewerOptions::hardwareDepth, true},
    {"--legacy-scene", &ViewerOptions::legacyScene, true},
    {"--donut-scene", &ViewerOptions::donutScene, true},
    {"--composition-half", &ViewerOptions::halfComposition, true},
    {"--vfx-full", &ViewerOptions::fullVfx, true},
    {"--vfx-half", &ViewerOptions::fullVfx, false},
    {"--all-interface", &ViewerOptions::allInterface, true},
    {"--zero-depth", &ViewerOptions::zeroDepth, true},
    {"--no-zero-depth", &ViewerOptions::zeroDepth, false},
    {"--mirror-blur", &ViewerOptions::mirrorBlur, true},
    {"--no-mirror-blur", &ViewerOptions::mirrorBlur, false},
    {"--zero-t-shortcut", &ViewerOptions::zeroTShortcut, true},
    {"--no-zero-t-shortcut", &ViewerOptions::zeroTShortcut, false},
    {"--no-q-bound", &ViewerOptions::qBound, false},
    {"--protect-background", &ViewerOptions::protectBackground, true},
    {"--calibration-board", &ViewerOptions::calibrationBoard, true},
    {"--fixed-z", &ViewerOptions::fixedDepth, true},
    {"--follow-window", &ViewerOptions::followWindow, true},
    {"--accum-half", &ViewerOptions::halfAccumulation, true},
};

float ReadFloat(const std::string &flag, const std::string &value, float minimum, float maximum)
{
    size_t consumed = 0;
    float parsed;
    try
    {
        parsed = std::stof(value, &consumed);
    }
    catch (const std::exception &)
    {
        throw std::invalid_argument(flag + ": invalid number '" + value + "'");
    }
    if (consumed != value.size() || !std::isfinite(parsed) || parsed < minimum || parsed > maximum)
        throw std::invalid_argument(flag + ": expected finite value in [" + std::to_string(minimum) + ", " +
                                    std::to_string(maximum) + "]");
    return parsed;
}

unsigned ReadInteger(const std::string &flag, const std::string &value, unsigned minimum, unsigned maximum)
{
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument(flag + ": expected an unsigned integer");
    unsigned long parsed;
    try
    {
        parsed = std::stoul(value);
    }
    catch (const std::exception &)
    {
        throw std::invalid_argument(flag + ": integer out of range");
    }
    if (parsed < minimum || parsed > maximum)
        throw std::invalid_argument(flag + ": expected value in [" + std::to_string(minimum) + ", " +
                                    std::to_string(maximum) + "]");
    return unsigned(parsed);
}
} // namespace

ViewerOptions ParseViewerOptions(int argc, const char *const *argv)
{
    ViewerOptions options;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        bool isFlag = false;
        for (const auto &flag : Flags)
            if (arg == flag.name)
            {
                options.*(flag.member) = flag.value;
                isFlag = true;
                break;
            }
        if (isFlag || arg == "--smoke-single-frame") // Accepted by older launch scripts.
            continue;
        auto value = [&]()
        {
            if (i + 1 >= argc || std::string_view(argv[i + 1]).substr(0, 2) == "--" ||
                std::string_view(argv[i + 1]).empty())
                throw std::invalid_argument(arg + ": missing value");
            return std::string(argv[++i]);
        };
        if (arg == "--render-width")
            options.renderWidth = ReadInteger(arg, value(), 256, 4096);
        else if (arg == "--render-height")
            options.renderHeight = ReadInteger(arg, value(), 256, 4096);
        else if (arg == "--fps-limit")
            options.fpsLimit = ReadInteger(arg, value(), 0, 1000);
        else if (arg == "--occupancy-overlay")
            options.occupancyOverlay = ReadInteger(arg, value(), 0, 7);
        else if (arg == "--occupancy-slice")
            options.occupancySlice = ReadInteger(arg, value(), 0, 127);
        else if (arg == "--frost-mips")
            options.frostMipCount = ReadInteger(arg, value(), 3, 5);
        else if (arg == "--planar-ior")
            options.planarIor = ReadFloat(arg, value(), 1, 2.5f);
        else if (arg == "--sphere-ior")
            options.sphereIor = ReadFloat(arg, value(), 1, 2.5f);
        else if (arg == "--refraction-gain")
            options.refractionStrength = ReadFloat(arg, value(), 0, 4);
        else if (arg == "--sphere-roughness")
            options.sphereRoughness = ReadFloat(arg, value(), 0, 1);
        else if (arg == "--max-frost-sigma")
            options.sigmaLimit = ReadFloat(arg, value(), 0, 24);
        else if (arg == "--frost-detail-radius")
            options.frostDetailRadius = ReadFloat(arg, value(), 0, 64);
        else if (arg == "--frost-normal-tiling")
            options.frostNormalTiling = ReadFloat(arg, value(), .25f, 64);
        else if (arg == "--sun-intensity")
            options.sunIntensity = ReadFloat(arg, value(), 0, 12);
        else if (arg == "--spot-intensity")
            options.spotIntensity = ReadFloat(arg, value(), 0, 500);
        else if (arg == "--volume-temporal-dither" || arg == "--volume-stochastic-upsample")
            options.temporalVolumeDithering = true;
        else if (arg == "--volume-upsample-test")
        {
            options.volumeUpsampleTest = options.headless = options.taa = options.lowResolutionVolumes = true;
        }
        else if (arg == "--volume-ms-octaves")
            options.volumeScatteringOctaves = ReadInteger(arg, value(), 0, 3);
        else if (arg == "--volume-extinction")
            options.initialVolumeExtinction = ReadFloat(arg, value(), 0.f, 128.f);
        else if (arg == "--smoke-frame")
            options.smokeFrame = ReadFloat(arg, value(), 0.f, 1023.f);
        else if (arg == "--smoke-sequence")
        {
            options.smokeSequencePath = value();
            options.bakedSmoke = true;
        }
        else if (arg == "--accumulation-audit")
        {
            options.accumulationAudit = ReadInteger(arg, value(), 0, 256);
            const auto mode = options.accumulationAudit;
            if (mode != 0 && mode != 1 && mode != 2 && mode != 4 && mode != 6 && mode != 8 && mode != 16 &&
                mode != 33 && mode != 65 && mode != 128 && mode != 256)
                throw std::invalid_argument("Unsupported accumulation audit mode");
            options.headless = true;
        }
        else if (arg == "--grenade-storage")
            options.grenadeStorage = ReadInteger(arg, value(), 0, 2);
        else if (arg == "--smoke-shots")
            options.smokeShots = ReadInteger(arg, value(), 0, 15);
        else if (arg == "--smoke-shot-radius")
            options.smokeShotRadius = ReadFloat(arg, value(), .005f, 1.f);
        else if (arg == "--vdb-brick-lod")
        {
            const std::string preset = value();
            if (preset != "balanced" && preset != "performance" && preset != "low")
                throw std::invalid_argument("--vdb-brick-lod expects balanced, performance or low");
            options.brickLod = preset == "low" ? BrickLod::Low : preset == "performance" ? BrickLod::Performance : BrickLod::Balanced;
            options.classifyBricks = true;
        }
        else if (arg == "--volume-step")
            options.volumeStepSize = ReadFloat(arg, value(), .005f, 2.f);
        else if (arg == "--volume-low-t")
            options.volumeLowT = ReadFloat(arg, value(), .001f, .5f);
        else if (arg == "--volume-step-scale")
            options.volumeStepScale = ReadFloat(arg, value(), 1.f, 8.f);
        else if (arg == "--volume-max-steps")
            options.volumeMaxSteps = ReadInteger(arg, value(), 16, 512);
        else if (arg == "--actor-editor-test")
            options.actorEditorTest = options.headless = true;
        else if (arg == "--taa")
            options.taa = true;
        else if (arg == "--volume-half-res")
            options.lowResolutionVolumes = true;
        else if (arg == "--no-volume-shadows")
            options.volumeShadows = false;
        else if (arg == "--time")
            options.fixtureTime =
                ReadFloat(arg, value(), -std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        else if (arg == "--lut-min-retain")
            options.lutMinRetain = ReadFloat(arg, value(), 0.f, 1.f);
        else if (arg == "--lut-jitter-strength")
            options.lutJitterStrength = ReadFloat(arg, value(), 0.f, 2.f);
        else if (arg == "--geometric-shell")
            options.flattenShell = false;
        else if (arg == "--magazine-order")
            options.magazineOrdering = ReadInteger(arg, value(), 0, 2);
        else if (arg == "--group-weight")
            options.groupWeightStrength = ReadFloat(arg, value(), 0.f, 8.f);
        else if (arg == "--group-depth-range")
            options.groupDepthRange = ReadFloat(arg, value(), .05f, 10.f);
        else if (arg == "--shell-offset-bins")
            options.shellOffsetBins = ReadFloat(arg, value(), -4.f, 4.f);
        else if (arg == "--stress")
            options.stressPreset = ReadInteger(arg, value(), 0, 34);
        else if (arg == "--camera-preset")
            options.cameraPreset = ReadInteger(arg, value(), 0, 11);
        else if (arg == "--benchmark")
            options.benchmarkFrames = ReadInteger(arg, value(), 1, 600);
        else if (arg == "--emissive-gain")
            options.emissiveGain = ReadFloat(arg, value(), 0, 64);
        else if (arg == "--background-threshold")
            options.backgroundThreshold = ReadFloat(arg, value(), 0, 1);
        else if (arg == "--depth-budget")
            options.depthBudget = ReadInteger(arg, value(), 16, 128);
        else if (arg == "--volume-scale")
            options.volumeScale = ReadInteger(arg, value(), 2, 8);
        else if (arg == "--vfx-mode")
        {
            options.vfxMode = ReadInteger(arg, value(), 0, 3);
            options.vfx = true;
        }
        else if (arg == "--vfx-strength")
            options.vfxStrength = ReadFloat(arg, value(), 0, 64);
        else if (arg == "--vfx-layers")
            options.vfxLayers = ReadInteger(arg, value(), 0, 3);
        else if (arg == "--submission-order")
            options.submissionOrder = ReadInteger(arg, value(), 0, 2);
        else if (arg == "--order-seed")
            options.orderSeed = ReadInteger(arg, value(), 0, 0xffffffffu);
        else if (arg == "--sponza-path")
            options.sponzaAssetPath = value();
        else if (arg == "--csv")
            options.benchmarkPath = value();
        else
            throw std::invalid_argument("Unknown option: " + arg);
    }
    if (options.depthBudget != 16 && options.depthBudget != 32 && options.depthBudget != 64 &&
        options.depthBudget != 128)
        throw std::invalid_argument("--depth-budget: expected 16, 32, 64 or 128");
    if (options.accumulationAudit != 0 && options.stochasticSmokeFilter)
        throw std::invalid_argument("Accumulation audits require the deterministic density filter");
    if (options.accumulationAudit == 65 && (options.volumeStepScale != 1.f || options.volumeLutSkip))
        throw std::invalid_argument("T-LUT audit requires --volume-step-scale 1 and disabled LUT skipping");
    if (options.accumulationAudit == 128 && options.spotIntensity != 0.f)
        throw std::invalid_argument("Spotlight audit requires --spot-intensity 0");
    options.quadVolumeT |= options.eightVolumeT;
    options.uiCapture |= options.uiSelfTest;
    options.emissiveBalls |= options.emissiveFront;
    if (options.volumeScale != 2 && options.volumeScale != 8)
        throw std::invalid_argument("--volume-scale: expected 2 or 8");
    options.headless |= options.uiCapture || options.benchmarkFrames > 0;
    options.vfx |= options.behind;
    if (options.legacyScene && options.donutScene)
        throw std::invalid_argument("--legacy-scene and --donut-scene select different scene backends");
    options.sponza |= !options.sponzaAssetPath.empty() || options.legacyScene || options.donutScene;
    options.donutScene = options.sponza && !options.legacyScene;
    if (options.donutScene)
        options.hardwareDepth = true;
    options.renderWidth = AlignRenderDimension(options.renderWidth);
    options.renderHeight = AlignRenderDimension(options.renderHeight);
    return options;
}

const char *ViewerHelp()
{
    return R"(AVBOIT + frost/refraction DX12 prototype
Usage: avboit_viewer [options]

Run and debug (benchmarks require Windows Developer Mode for StablePowerState):
  --accum-half               RGBA16F N/A/totalTau/BackN precision reference
  --submission-order 0..2    Original / reversed / shuffled transparent triangles
  --order-seed UINT          Deterministic shuffled order (default 1)
  --help, -h                 Print help without creating a GPU device
  --headless                 Render native-raster.ppm and native-lut.rgba8
  --ui-capture               Headless capture including ImGui
  --ui-self-test             Exercise live ImGui controls and resource rebuilding
  --occupancy-overlay 0..7  Off / requests RGB / transparent / frost / sharp B / B work / volume XY / volume Z
  --occupancy-slice 0..127  Physical Z slice for overlay 7 (clamped to current Z budget)
  --camera-preset 0..11       Camera (6: sky; 7/8/9: crossing glass; 10/11: near/inside smoke)

Scene and materials:
  --shell-offset-bins N  Initial noise-shell OIT offset, -4..4 bins (positive toward camera)
  --vfx-mode 0..3            Oscillating ring / heat / expanding shock / dual shock; enables VFX
  --vfx-strength 0..64       Global screen-space displacement multiplier in pixels (default 20)
  --vfx-layers 0..3          Diagnostic layer bitmask: 1 primary, 2 secondary, 3 both
  --sponza                   Load Sponza through Donut (default scene backend)
  --legacy-scene             Old Sponza loader for calibration comparisons
  --sponza-path PATH          Explicit Sponza.gltf path; enables Sponza
  --geometric-shell         Scene 32: use ordinary camera offset instead of flattened ordering
  --magazine-order 0..2     Scene 34: geometric / uniform flat group / depth-weighted flat group (default 2)
  --magazine-neighbours     Scene 34: add ordinary RGB panes before and behind the groups
  --group-weight 0..8       Scene 34: depth-weight falloff in stops (default 4)
  --group-depth-range M     Scene 34: shared depth span, .05..10 metres (default 2)
  --stress 0..34             Stress preset (26: noise shell; 27/28: silhouette; 29/30: eight layers; 31: zero spread; 32: flattened cubes; 33: third-layer halo; 34: folded magazine); also in ImGui Test case
  --sun-intensity N          Sponza sun irradiance (0..12), default 2
  --spot-intensity N         Sponza dynamic spotlight intensity (0..500), default 80
  --volume-temporal-dither  Animate raymarch sample phases (requires final TAA)
  --volume-upsample-test   Headless static/moving bilateral captures (enables half-res + TAA)
  --volume-ms-octaves 0..3 Additional scattering octaves (default 2; 0: single)
  --volume-extinction N    Initial volume material extinction, 0..128 per world unit
  --grenade-storage N  GPU coverage: 0 dense R8, 1 sparse byte buffer, 2 sparse hardware-filtered atlas
  --grenade-skip       Enable conservative brick empty interval skipping
  --gameplay-grenade  Gameplay binary coverage smoke, unobstructed sphere (scenes 24/25)
  --smoke-shots N     Preview 0..15 world-space bullet capsules through gameplay smoke
  --smoke-shot-radius M  Capsule radius in metres (0.005..1, default 0.45)
  --smoke-trail-reference  Offline comparison: evaluate every capsule without the index
  --smoke-trail-rebuild  Offline timing: rebuild the capsule index every frame
  --baked-smoke       Use compressed animated density in scenes 24/25
  --smoke-frame N     Initial zero-based frame, clamped to sequence (default 15)
  --smoke-sequence PATH  Load a local sequence directory instead of the bundled probe
  --smoke-paused      Start paused; use Actors > VDB animation > Play / Restart
  --no-smoke-brick-skip Disable brick traversal for comparison
  --no-smoke-ao             Disable baked volume ambient occlusion
  --no-smoke-mask     Disable gameplay coverage for baked smoke
  --smoke-door-closed Close the VDB example coverage doorway
  --vdb-proxy-stencil    Mask half-resolution VDB pixel marching with occupied brick groups
  --vdb-ray-hull         Clip half-resolution Q4 trilinear pixel marching against the frame's 14-DOP
  --vdb-group-march      Integrate occupied 2x2x2 brick groups independently (half-resolution Q4 PS)
  --vdb-brick-march      Draw individual 4x4x4 Q4 bricks with a dedicated fixed-step PS at the selected volume resolution
  --vdb-brick-lod balanced|performance|low  Use classified bricks with the selected quality preset (default: balanced)
  --vdb-brick-vertex-light  Shade shared brick corners and interpolate lighting along the ray
  --vdb-brick-double-step   Double the single-brick ray step and its one-voxel cap
  --vdb-brick-classify      Classify bricks into per-step and center-lit rectangle draws
  --vdb-brick-classify-cube  Use indexed, backface-culled cubes for both classified draws
  --vdb-brick-cache        Precompute shared brick-corner lighting per volume draw
  --no-vdb-brick-indices     Draw six independent vertices per brick for GPU comparisons
  --vdb-brick-coarse-light  Draw Q4 bricks and shade each side of the glass once per intersected brick
  --vdb-compute        Q4 trilinear compute raymarch (quad T mode)
  --vdb-quarter-ray-reference  Benchmark one independent ray per 2x2 accumulation texels (compute)
  --vdb-near-ray-lod     Switch wave-wide from quad to diagonal-pair to pixel rays as depth increases (compute)
  --vdb-shared-lighting  Compare low-transmission light sharing with --vdb-compute
  --no-volume-early-out    March through low-transmission tails for comparison
  --volume-skip-view   Show skipped march intervals per source ray
  --volume-step-view   Show executed march intervals per source ray
  --smoke-stochastic-filter  Experimental single-voxel filtering in smoke accumulation only
  --accumulation-audit N  Offline shader ablation; see docs/ACCUMULATION_PROFILE.zh-CN.md
  --volume-reference-sampling  Validation: four density taps and full tail lighting
  --volume-quad-t      Quad-shared T-LUT, four-step interpolation (default on)
  --no-volume-quad-t   Disable quad-shared T-LUT and quad empty-brick jumps
  --volume-eight-t     Experimental eight uniform T anchors (implies quad sharing)
  --volume-lut-skip    Experimental exact-T empty interval skipping
  --volume-march-stats       Diagnostic numerator = intervals, shading evaluations, skipped intervals
  --volume-step N           World-space accumulation step (default .5)
  --volume-low-t N          Grow steps below this max-RGB T (default .1)
  --volume-step-scale N     Maximum low-T step multiplier, 1..8 (default 1)
  --volume-max-steps N      Per-ray step budget, 16..512 (default 256)
  --taa                     Enable HDR TAA and temporal volume dithering (default)
  --no-taa                  Disable TAA for non-temporal comparisons
  --volume-half-res         Half-resolution local volume accumulation (default)
  --volume-full-res         Full-resolution local volume accumulation reference
  --no-volume-shadows        Disable local volume moment shadows for comparison
  --time SECONDS             Finite initial fixture time, default 0
  --glass-sphere             Replace default panes with a sphere
  --rgb-glass                Strong RGB absorption for sphere fixtures
  --sphere-roughness 0..1    Default 0
  --emissive-balls           HDR sphere fixture
  --no-smoke                 Disable smoke in the manual fixture
  --planar-ior 1..2.5        Planar frost IOR, default 1.3
  --sphere-ior 1..2.5        Sphere IOR, default 1.5
  --refraction-gain 0..4     Self-refraction offset gain, default 1
  --normal-sphere            Artistic projected-normal sphere offset
  --vfx                     Enable shock-wave distortion
  --vfx-behind               Place VFX behind glass; enables VFX

Quality and references:
  --poison-resolve         Fill auxiliary outputs with poison before resolve (validation)
  --volume-scale 2|8         Extinction XY divisor, default 8
  --linear                  Bilinear reconstruction (default: cubic)
  --cubic-trilinear         Approximate cubic: 4 trilinear taps instead of 8 bilinear taps
  --no-frost                Disable frost, preserve self-refraction
  --max-frost-sigma 0..24   Material sigma cap in 1440p reference pixels
  --frost-detail-radius 0..64  Offset reserved for post-blur sampling, 1440p pixels (default 24)
  --frost-normal-tiling .25..64  Normal-map repeats across a pane (default 1)
  --frost-mips 3..5          Fixed-screen chain; extra levels are finer (default 3)
  --dense-blur               64-tap first filter reference
  --no-tile-resolve          Draw every resolve tile (culling reference)
  --fps-limit N             CPU frame pacing, default 120; 0 disables
  --sparse-blur              16-tap first filter (default), same fixed screen grid
  --full-blur                Full-screen/full-chain blur reference
  --dense-extinction         Dense clear/integration reference
  --poison-extinction        Fill unused extinction with garbage for tests
  --no-slope-splat           Disable slope-aware extinction depth push (comparison)
  --donut-scene             Use Donut Sponza loading/forward shading and D32
  --composition-half       RGBA16F composition precision reference
  --vfx-full               Full-resolution VFX (default; --vfx-half experimental)
  --all-interface          Submit every OIT triangle to interface prepass
  --no-zero-depth          Disable conservative zero-T depth quads (default on)
  --no-mirror-blur         Disable invalid-tap mirror replacement (default on)
  --zero-t-shortcut         Experimental zero-weight shading skip (default off)
  --no-zero-t-shortcut      Evaluate zero-weight shading too (reference)
  --no-q-bound              Disable total-transmission lower bound (reference)
  --protect-background      Reject B when any q channel is below threshold
  --background-threshold N  B rejection threshold, 0..1 (default .001)
  --calibration-board       Asymmetric four-color inversion target
  --emissive-gain N          Emissive sphere radiance multiplier, 0..64
  --emissive-front           Put emissive spheres before the frost pane
  --lut-min-filter          Bounded 3x3 XY min after integration (default), no new RT
  --no-lut-min-filter       Disable XY min-filter for comparison
  --lut-min-retain 0..1     Minimum fraction of centre T retained (default .5; 0: old min, 1: no XY change)
  --lut-ign                 IGN XY jitter for surface accumulation LUT reads; suspends Zero-T depth culling
  --lut-plane               Per-column receiver-plane depth for surface LUT reads; overrides IGN, suspends Zero-T
  --lut-jitter-strength 0..2 Full jitter width in LUT texels (default 1); temporal with TAA, static without
  --fixed-z                 Uniform log-Z reference (adaptive is default)
  --depth-budget N          Adaptive Z budget: 16, 32, 64, 128 (default 128)
  --follow-window           Follow window size (longest side capped at 2048)
  --render-width N          Internal width, 256..4096, rounded up to 4 (default 2560)
  --render-height N         Internal height, 256..4096, rounded up to 4 (default 1440)
  --hardware-depth          Read opaque D32 instead of color alpha (host migration)
  --redraw-shadows           Re-render static Sponza shadows every frame

Profiling:
  --benchmark 1..600        Headless fixed-camera measured frames
  --profile-frame-only       Disable per-pass queries, retain frame timer
  --csv PATH                 Timing export, default native-benchmark.csv

Default internal resolution: 2560x1440 (configurable). WASD/QE move; RMB look; Home resets camera.
Captures/CSVs use the working directory; shaders/default assets use executable paths.
)";
}
} // namespace avboit
