// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <vector>

namespace avboit
{
struct StressQuad
{
    std::array<float, 3> center, x, y;
    std::array<float, 4> color;
    std::array<float, 3> transmission;
    unsigned kind = 2;
    float roughness = 0, distortion = 0, shape = -1;
    std::array<float, 3> motion = {};
    float frequency = 1;
    std::array<float, 2> localX = {0, 1}; // material-coordinate center and half-span
};
struct StressFixture
{
    std::vector<StressQuad> opaque, transparent;
    bool sphere = false;
};
constexpr unsigned StressPresetCount = 16;
const char *StressPresetName(unsigned preset);
StressFixture BuildStressFixture(unsigned preset);
// View-space prototype receiver: opaque frame and three optical skin panels.
StressFixture BuildInversionBoard();
} // namespace avboit
