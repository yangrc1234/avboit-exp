// SPDX-License-Identifier: MIT
#include "sample/ViewerOptions.h"
#include "render/FrostChain.h"
#include <cstdio>
#include <stdexcept>
#include <vector>

static avboit::ViewerOptions Parse(std::initializer_list<const char *> arguments)
{
    std::vector<const char *> argv = {"viewer"};
    argv.insert(argv.end(), arguments.begin(), arguments.end());
    return avboit::ParseViewerOptions(int(argv.size()), argv.data());
}
static void Require(bool value)
{
    if (!value)
        throw std::runtime_error("Viewer options contract failed");
}
int main()
{
    try
    {
        auto defaults = Parse({});
        Require(defaults.cubic && defaults.frost && defaults.showSmoke && !defaults.headless);
        Require(defaults.frostMipCount == 3 && defaults.sparseBlur);
        Require(defaults.volumeScale == 8 && Parse({"--camera-preset", "6"}).cameraPreset == 6);
        Require(defaults.occupancyOverlay == 0);
        Require(Parse({"--occupancy-overlay", "7", "--occupancy-slice", "127"}).occupancySlice == 127);
        Require(Parse({"--emissive-front"}).emissiveFront && Parse({"--emissive-front"}).emissiveBalls);
        Require(defaults.fpsLimit == 120 && Parse({"--fps-limit", "0"}).fpsLimit == 0 &&
                Parse({"--fps-limit", "60"}).fpsLimit == 60);
        Require(defaults.tileResolve && !Parse({"--no-tile-resolve"}).tileResolve);
        for (unsigned count : {3u, 4u, 5u})
        {
            const auto a = avboit::FrostChain::Make(1920, 1080, count), b = avboit::FrostChain::Make(2560, 1440, count),
                       c = avboit::FrostChain::Make(3840, 2160, count);
            Require(a.width == b.width && b.width == c.width && a.height == b.height && b.height == c.height);
            Require((a.height >> (count - 1)) == 45 && (a.width >> (count - 1)) == 80);
            Require(a.VarianceAt(count - 1) == avboit::FrostChain::Variance(5));
        }
        Require(Parse({"--frost-mips", "5", "--dense-blur"}).frostMipCount == 5);
        Require(Parse({"--poison-resolve"}).poisonResolve);
        auto precision = Parse({"--accum-half", "--composition-half", "--submission-order", "2", "--order-seed", "42"});
        Require(precision.halfAccumulation && precision.halfComposition && precision.submissionOrder == 2 &&
                precision.orderSeed == 42);
        auto size = Parse({"--render-width", "1280", "--render-height", "768"});
        Require(size.renderWidth == 1280 && size.renderHeight == 768);
        auto twoK = Parse({"--render-width", "2560", "--render-height", "1440"});
        Require(twoK.renderWidth == 2560 && twoK.renderHeight == 1440);
        auto aligned = Parse({"--render-width", "641", "--render-height", "385"});
        Require(aligned.renderWidth == 644 && aligned.renderHeight == 388);
        auto effect = Parse({"--vfx-mode", "3", "--vfx-layers", "2", "--vfx-strength", "32"});
        Require(effect.vfx && effect.vfxMode == 3 && effect.vfxLayers == 2 && effect.vfxStrength == 32);
        auto capture = Parse({"--ui-self-test", "--vfx-behind", "--sponza-path", "scene with spaces/Sponza.gltf"});
        Require(capture.uiSelfTest && capture.uiCapture && capture.headless && capture.vfx && capture.sponza &&
                capture.donutScene && capture.hardwareDepth);
        auto scene = Parse({"--sponza"});
        Require(scene.donutScene && scene.hardwareDepth);
        auto legacy = Parse({"--legacy-scene"});
        Require(legacy.sponza && !legacy.donutScene && !legacy.hardwareDepth);
        Require(!defaults.sponza && !defaults.donutScene);
        auto controls = Parse({"--sphere-ior", "2.5", "--planar-ior", "1", "--refraction-gain", "0",
                               "--sphere-roughness", ".6", "--stress", "10", "--camera-preset", "5", "--time", "-2",
                               "--benchmark", "600", "--profile-frame-only", "--csv", "timings.csv"});
        Require(controls.sphereIor == 2.5f && controls.planarIor == 1 && controls.refractionStrength == 0 &&
                controls.sphereRoughness == .6f);
        Require(controls.stressPreset == 10 && controls.cameraPreset == 5 && controls.fixtureTime == -2 &&
                controls.benchmarkFrames == 600);
        Require(controls.headless && !controls.detailedProfiling && controls.benchmarkPath == "timings.csv");
        for (auto args : std::vector<std::vector<const char *>>{{"--resolve-mode", "0"},
                                                                {"--resolve-mode", "1"},
                                                                {"--resolve-mode", "2"},
                                                                {"--resolve-half"},
                                                                {"--q-half"},
                                                                {"--q-log8"},
                                                                {"--frost-mips", "2"},
                                                                {"--frost-mips", "6"},
                                                                {"--resolve-mode", "3"},
                                                                {"--legacy-scene", "--donut-scene"},
                                                                {"--emissive-gain", "65"},
                                                                {"--background-threshold", "-1"},
                                                                {"--background-threshold", "2"},
                                                                {"--depth-budget", "48"},
                                                                {"--render-height", "64"},
                                                                {"--render-width", "8192"},
                                                                {"--wat"},
                                                                {"--time"},
                                                                {"--time", "--headless"},
                                                                {"--time", "nan"},
                                                                {"--time", "inf"},
                                                                {"--sphere-ior", "1.5garbage"},
                                                                {"--sphere-ior", "0.9"},
                                                                {"--refraction-gain", "5"},
                                                                {"--stress", "-1"},
                                                                {"--stress", "16"},
                                                                {"--stress", "1.5"},
                                                                {"--camera-preset", "7"},
                                                                {"--volume-scale", "4"},
                                                                {"--occupancy-overlay", "8"},
                                                                {"--occupancy-slice", "128"},
                                                                {"--inspect"},
                                                                {"--separable-blur"},
                                                                {"--fused-blur"},
                                                                {"--serial-schedule"},
                                                                {"--max-frost-radius", "129"},
                                                                {"--max-glass-offset", "-1"},
                                                                {"--benchmark", "0"},
                                                                {"--benchmark", "601"},
                                                                {"--benchmark", "99999999999999999"},
                                                                {"--csv", ""}})
        {
            args.insert(args.begin(), "viewer");
            bool rejected = false;
            try
            {
                avboit::ParseViewerOptions(int(args.size()), args.data());
            }
            catch (const std::invalid_argument &)
            {
                rejected = true;
            }
            Require(rejected);
        }
        Require(Parse({"--follow-window"}).followWindow);
        Require(!defaults.zeroTShortcut && Parse({"--zero-t-shortcut"}).zeroTShortcut &&
                !Parse({"--no-zero-t-shortcut"}).zeroTShortcut);
        Require(defaults.zeroDepth && defaults.mirrorBlur && !Parse({"--no-zero-depth"}).zeroDepth &&
                !Parse({"--no-mirror-blur"}).mirrorBlur);
        Require(defaults.fullVfx && !defaults.halfComposition && !defaults.allInterface);
        Require(Parse({"--vfx-full"}).fullVfx && !Parse({"--vfx-half"}).fullVfx &&
                Parse({"--composition-half"}).halfComposition && Parse({"--all-interface"}).allInterface);
        auto calibration = Parse({"--calibration-board", "--emissive-gain", "8"});
        Require(calibration.calibrationBoard && calibration.emissiveGain == 8);
        Require(Parse({"--fixed-z"}).fixedDepth && !defaults.fixedDepth);
        auto protection = Parse({"--no-q-bound", "--protect-background", "--background-threshold", ".02"});
        Require(!protection.qBound && protection.protectBackground && protection.backgroundThreshold == .02f);
        for (unsigned budget : {16u, 32u, 64u, 128u})
        {
            const std::string value = std::to_string(budget);
            Require(Parse({"--depth-budget", value.c_str()}).depthBudget == budget);
        }
        Require(Parse({"--help"}).help && Parse({"-h"}).help);
        Require(Parse({"--volume-scale", "2"}).volumeScale == 2 && Parse({"--volume-scale", "8"}).volumeScale == 8);
        bool badScale = false;
        try
        {
            Parse({"--volume-scale", "3"});
        }
        catch (const std::invalid_argument &)
        {
            badScale = true;
        }
        Require(badScale);
        printf("Viewer options: defaults, implications, numeric values, help and 16 invalid cases PASS\n");
        return 0;
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
