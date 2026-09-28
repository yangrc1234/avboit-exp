// SPDX-License-Identifier: MIT
#include "sample/StressFixtures.h"
#include <cmath>
#include <stdexcept>

namespace avboit
{
const char *StressPresetName(unsigned preset)
{
    static const char *names[] = {"Default panes / sphere controls",
                                  "RGB layered panes",
                                  "Low RGB transmission + HDR smoke",
                                  "Thin bars + crossing smoke",
                                  "Crossing curved frost surfaces",
                                  "Sphere + crossing RGB panes",
                                  "Sphere + dense front smoke",
                                  "Front refraction / rear frost limitation",
                                  "Ordinary glass with rear VFX",
                                  "Opaque weapon proxy",
                                  "Perforated frost / refractor overlap",
                                  "Zero-T occluded sphere",
                                  "Frost before zero-T stack",
                                  "RGB nonzero channel survives",
                                  "Moving dense smoke before frost",
                                  "Separated frost / refraction tiles"};
    return names[preset < StressPresetCount ? preset : 0];
}
StressFixture BuildStressFixture(unsigned preset)
{
    if (preset >= StressPresetCount)
        throw std::runtime_error("Unknown stress preset");
    StressFixture result;
    auto pane = [&](std::array<float, 3> center, std::array<float, 3> x, std::array<float, 3> y,
                    std::array<float, 3> transmission, float roughness = 0, float distortion = 0) -> StressQuad &
    {
        result.transparent.push_back({center, x, y, {.025f, .035f, .04f, 1}, transmission, 2, roughness, distortion});
        return result.transparent.back();
    };
    auto smoke = [&](std::array<float, 3> center, float radius, std::array<float, 4> color) -> StressQuad &
    {
        result.transparent.push_back({center, {radius, 0, 0}, {0, radius, 0}, color, {0, 0, 0}, 1});
        return result.transparent.back();
    };
    if (preset == 1)
    {
        for (unsigned i = 0; i < 3; ++i)
        {
            const std::array<float, 3> t[] = {{.08f, .8f, .95f}, {.95f, .12f, .8f}, {.8f, .95f, .06f}};
            pane({-.3f + i * .3f, 0, 3.7f + i * .4f}, {.8f, 0, 0}, {0, 1, 0}, t[i], i == 0 ? .5f : 0);
        }
        smoke({0, 0, 3.1f}, .6f, {.8f, .8f, .8f, .6f});
    }
    else if (preset == 2)
    {
        pane({0, 0, 4.5f}, {1.45f, 0, 0}, {0, 1.05f, 0}, {.018f, .32f, .78f}, .75f);
        for (unsigned i = 0; i < 8; ++i)
            smoke({-.6f + i * .09f, .05f, 2.2f + i * .14f}, .53f, {18, 12, 5, .76f});
        pane({.6f, 0, 3.5f}, {.35f, 0, 0}, {0, .9f, 0}, {.06f, .55f, .88f});
    }
    else if (preset == 3)
    {
        pane({0, 0, 4.5f}, {1.45f, 0, 0}, {0, 1.05f, 0}, {.7f, .9f, .95f}, .65f);
        for (unsigned i = 0; i < 15; ++i)
            pane({-1.25f + i * .18f, 0, 3.6f}, {.008f + (i % 3) * .006f, 0, 0}, {0, .85f, 0}, {.08f, .45f, .85f});
        auto &cloud = smoke({0, 0, 4.5f}, .6f, {1.8f, .9f, .25f, .88f});
        cloud.motion = {.35f, 0, .8f};
        cloud.frequency = .8f;
    }
    else if (preset == 4)
    {
        for (int side : {-1, 1})
            for (unsigned i = 0; i < 64; ++i)
            {
                float x0 = -1.475f + i * 2.95f / 64, x1 = x0 + 2.95f / 64;
                float z0 = 4.5f + side * .55f * std::sin(x0), z1 = 4.5f + side * .55f * std::sin(x1);
                auto &surface =
                    pane({(x0 + x1) * .5f, 0, (z0 + z1) * .5f}, {(x1 - x0) * .5f, 0, (z1 - z0) * .5f}, {0, 1.075f, 0},
                         side < 0 ? std::array<float, 3>{.3f, .8f, .95f} : std::array<float, 3>{.95f, .4f, .2f}, .6f,
                         side < 0 ? 0.f : 12.f);
                surface.motion = {0, 0, side * .18f};
                surface.frequency = .7f;
                surface.localX = {(x0 + x1) / (2 * 1.475f), (x1 - x0) / (2 * 1.475f)};
            }
        smoke({-.6f, .1f, 3.1f}, .25f, {.8f, .45f, .2f, .5f});
    }
    else if (preset == 5)
    {
        result.sphere = true;
        pane({-.2f, 0, 4.25f}, {1.5f, 0, .95f}, {0, 1.25f, 0}, {.92f, .12f, .3f});
        pane({.35f, 0, 4.45f}, {1.25f, 0, -.95f}, {0, 1.05f, 0}, {.15f, .75f, .94f});
        auto &cloud = smoke({-.4f, .1f, 3.6f}, .5f, {.7f, .74f, .8f, .78f});
        cloud.motion = {0, 0, 1.25f};
        cloud.frequency = .9f;
    }
    else if (preset == 6)
    {
        result.sphere = true;
        for (unsigned i = 0; i < 8; ++i)
        {
            auto &cloud = smoke({-.53f + .08f * std::sin(i * 1.7f), .07f + .1f * std::cos(i * 1.3f), 1.75f + i * .075f},
                                .36f + (i % 3) * .025f, {.4f, .43f, .47f, .86f});
            cloud.motion = {.12f, 0, 0};
            cloud.frequency = .6f;
        }
    }
    else if (preset == 7 || preset == 8)
    {
        pane({0, 0, 3.5f}, {.85f, 0, 0}, {0, 1, 0}, {.85f, .95f, .98f}, 0, preset == 7 ? 18.f : 0.f);
        if (preset == 7)
            pane({0, 0, 5.f}, {1.45f, 0, 0}, {0, 1.1f, 0}, {.8f, .9f, .95f}, .75f);
        // Preset 8 uses the rear VFX position z=6.4. The ordinary pane must
        // never become the selected special interface or suppress that VFX.
    }
    else if (preset == 9)
    {
        pane({0, 0, 4.5f}, {1.45f, 0, 0}, {0, 1.05f, 0}, {.8f, .9f, .95f}, .75f);
        auto box = [&](std::array<float, 3> center, std::array<float, 3> half)
        {
            for (unsigned axis = 0; axis < 3; ++axis)
                for (int sign : {-1, 1})
                {
                    auto c = center;
                    c[axis] += sign * half[axis];
                    std::array<float, 3> x = {}, y = {};
                    x[(axis + 1) % 3] = half[(axis + 1) % 3];
                    y[(axis + 2) % 3] = half[(axis + 2) % 3];
                    result.opaque.push_back({c,
                                             x,
                                             y,
                                             axis == 2 ? std::array<float, 4>{.07f, .12f, .16f, 1}
                                                       : std::array<float, 4>{.18f, .24f, .27f, 1},
                                             {0, 0, 0},
                                             0});
                }
        };
        box({.45f, -.4f, 2.9f}, {.7f, .15f, .15f});
        box({-.49f, -.35f, 2.9f}, {.35f, .045f, .045f});
        box({1.1f, -.41f, 2.9f}, {.19f, .12f, .12f});
        box({.68f, -.65f, 2.9f}, {.1f, .22f, .1f});
        box({.3f, -.2f, 2.9f}, {.4f, .025f, .04f});
        for (unsigned i = 0; i < 5; ++i)
            smoke({-.8f + i * .4f, .1f, 5.7f}, .23f, {.6f, .8f, 1, .55f});
    }
    else if (preset == 12 || preset == 13)
    {
        pane({0, 0, 1.5f}, {.85f, 0, 0}, {0, .7f, 0}, {.8f, .9f, .95f}, .55f, 8);
        for (unsigned i = 0; i < 18; ++i)
            pane({0, 0, 2.f + i * .055f}, {1.6f, 0, 0}, {0, 1.3f, 0},
                 preset == 12 ? std::array<float, 3>{.2f, .2f, .2f} : std::array<float, 3>{.02f, .9f, .98f});
    }
    else if (preset == 14)
    {
        pane({0, 0, 4.5f}, {1.6f, 0, 0}, {0, 1.2f, 0}, {.7f, .85f, .95f}, .65f, 12);
        for (unsigned i = 0; i < 28; ++i)
        {
            auto &cloud = smoke({-.25f, 0, 2.2f + i * .012f}, .95f, {.35f, .42f, .5f, 1});
            cloud.motion = {.45f, 0, 0};
            cloud.frequency = .8f;
        }
    }
    else if (preset == 11)
    {
        result.sphere = true;
        // Full RGB extinction before the shaded sphere; no channel may be
        // treated as opaque merely because one wavelength has saturated.
        for (unsigned i = 0; i < 18; ++i)
            pane({0, 0, 2.f + i * .055f}, {1.6f, 0, 0}, {0, 1.3f, 0}, {.2f, .2f, .2f});
    }
    else if (preset == 15)
    {
        pane({-2.6f, .15f, 4.5f}, {.42f, 0, 0}, {0, .7f, 0}, {.35f, .82f, .95f}, .5f, 10);
        pane({2.6f, -.1f, 4.5f}, {.42f, 0, 0}, {0, .7f, 0}, {.95f, .5f, .25f}, .7f, 14);
        smoke({-2.6f, .2f, 3.5f}, .25f, {.7f, .75f, .8f, .55f});
    }
    else if (preset == 10)
    {
        pane({-.48f, 0, 4}, {.9f, 0, .2f}, {0, .95f, 0}, {.25f, .8f, .95f}, .75f).shape = .3f;
        pane({.5f, 0, 4.3f}, {.85f, 0, -.3f}, {0, .9f, 0}, {.95f, .35f, .2f}, 0, 18);
        smoke({0, .05f, 3}, .25f, {.8f, .8f, .8f, .5f});
    }
    return result;
}
} // namespace avboit

namespace avboit
{
StressFixture BuildInversionBoard()
{
    StressFixture board;
    auto rect = [&](float x, float y, float hx, float hy, float z, std::array<float, 4> color)
    { board.opaque.push_back({{x, y, z}, {hx, 0, 0}, {0, hy, 0}, color, {0, 0, 0}, 0}); };
    // Four quadrants and an asymmetric dark F identify both inversion axes.
    rect(-1.25f, .95f, 1.24f, .94f, 7, {2, .08f, .03f, 1});
    rect(1.25f, .95f, 1.24f, .94f, 7, {.03f, 1.5f, .08f, 1});
    rect(-1.25f, -.95f, 1.24f, .94f, 7, {.03f, .12f, 2, 1});
    rect(1.25f, -.95f, 1.24f, .94f, 7, {2, 1.3f, .03f, 1});
    const std::array<float, 4> ink = {.006f, .006f, .006f, 1};
    rect(-1.4f, .9f, .065f, .58f, 6.99f, ink);
    rect(-1.08f, 1.42f, .38f, .065f, 6.99f, ink);
    rect(-1.14f, 1.00f, .32f, .065f, 6.99f, ink);
    // Distinct bright dots make blur radius and reconstruction easy to compare.
    rect(.75f, -.7f, .06f, .06f, 6.98f, {12, 12, 12, 1});
    rect(1.15f, -.7f, .035f, .035f, 6.98f, {12, 12, 12, 1});
    return board;
}
} // namespace avboit
