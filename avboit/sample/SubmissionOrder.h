// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <vector>
namespace avboit
{
// Reorder complete triangles in the transparent range only. Each vertex has
// four float4 records; no change to material indices, winding or draw ranges.
void ReorderTransparentTriangles(std::vector<std::array<float, 4>> &vertices, unsigned firstVertex,
                                 unsigned vertexCount, unsigned mode, unsigned seed);
} // namespace avboit
