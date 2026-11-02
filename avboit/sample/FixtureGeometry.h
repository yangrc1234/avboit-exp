// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <vector>

namespace avboit
{
using FixtureRecord = std::array<float, 4>;
// NativeTransparency vertex records: position/kind, radiance/coverage,
// transmission/roughness, local material coordinates.
void AppendEmissiveSphere(std::vector<FixtureRecord> &vertices, const std::array<float, 3> &center, float radius,
                          const std::array<float, 3> &radiance);
void AppendGlassSphere(std::vector<FixtureRecord> &vertices, const std::array<float, 3> &center, float radius,
                       const std::array<float, 3> &transmission, float roughness);
// Identical topology for both layers; the shell is displaced along mesh normals.
void AppendLayeredModel(std::vector<FixtureRecord> &vertices, const std::array<float, 3> &center, float shellGap,
                        bool outer);
} // namespace avboit
