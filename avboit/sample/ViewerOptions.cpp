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
    {"--dense-blur", &ViewerOptions::sparseBlur, false},
    {"--no-tile-resolve", &ViewerOptions::tileResolve, false},
    {"--poison-resolve", &ViewerOptions::poisonResolve, true},
    {"--help", &ViewerOptions::help, true},
    {"-h", &ViewerOptions::help, true},
    {"--headless", &ViewerOptions::headless, true},
    {"--ui-capture", &ViewerOptions::uiCapture, true},
    {"--ui-self-test", &ViewerOptions::uiSelfTest, true},
    {"--linear", &ViewerOptions::cubic, false},
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
        if (isFlag)
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
        else if (arg == "--time")
            options.fixtureTime =
                ReadFloat(arg, value(), -std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        else if (arg == "--stress")
            options.stressPreset = ReadInteger(arg, value(), 0, 15);
        else if (arg == "--camera-preset")
            options.cameraPreset = ReadInteger(arg, value(), 0, 6);
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
  --camera-preset 0..6        Deterministic camera (default 0; 6 looks at sky)

Scene and materials:
  --vfx-mode 0..3            Oscillating ring / heat / expanding shock / dual shock; enables VFX
  --vfx-strength 0..64       Global screen-space displacement multiplier in pixels (default 20)
  --vfx-layers 0..3          Diagnostic layer bitmask: 1 primary, 2 secondary, 3 both
  --sponza                   Load Sponza through Donut (default scene backend)
  --legacy-scene             Old Sponza loader for calibration comparisons
  --sponza-path PATH          Explicit Sponza.gltf path; enables Sponza
  --stress 0..15             Stress preset (0: manual, 15: separated glass); also in ImGui Test case
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
  --no-frost                Disable frost, preserve self-refraction
  --max-frost-sigma 0..24   Material sigma cap in 1440p reference pixels
  --frost-mips 3..5          Fixed-screen chain; extra levels are finer (default 3)
  --dense-blur               64-tap first filter reference
  --no-tile-resolve          Draw every resolve tile (culling reference)
  --fps-limit N             CPU frame pacing, default 120; 0 disables
  --sparse-blur              16-tap first filter (default), same fixed screen grid
  --full-blur                Full-screen/full-chain blur reference
  --dense-extinction         Dense clear/integration reference
  --poison-extinction        Fill unused extinction with garbage for tests
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
