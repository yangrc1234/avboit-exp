// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <filesystem>
#include <vector>
namespace avboit
{
// Five float4 records: world position, normal/cutoff, UV/roughness/metallic,
// tangent/handedness, base color factor. GPU vertex pulling uses explicit ranges.
struct ScenePrimitive
{
    unsigned firstVertex = 0, vertexCount = 0;
    std::filesystem::path albedo, normal, metallicRoughness;
};
struct SceneData
{
    std::vector<std::array<float, 4>> vertices;
    std::vector<ScenePrimitive> primitives;
};
SceneData LoadSponza(const std::filesystem::path &path);
} // namespace avboit
