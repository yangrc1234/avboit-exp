// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace avboit
{
// The seven slab pairs in UV space are stored in four HLSL constant registers.
struct VdbRayHull
{
    using Planes = std::array<float, 4>;
    Planes axisLo{1, 1, 1}, axisHi{}, diagonalLo{4, 4, 4, 4}, diagonalHi{-4, -4, -4, -4};

    void IncludeBrick(uint64_t index, const uint32_t *size)
    {
        std::array<float, 3> low, high;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            const unsigned grid = (size[axis] + 3) / 4;
            const unsigned brick = unsigned(index % grid);
            index /= grid;
            // Brick ownership uses clamp(uv * size - 0.5) before floor.
            low[axis] = brick ? (4 * brick + .5f) / size[axis] : 0.f;
            high[axis] = std::min(1.f, (4 * brick + 4.5f) / size[axis]);
            axisLo[axis] = std::min(axisLo[axis], low[axis]);
            axisHi[axis] = std::max(axisHi[axis], high[axis]);
        }
        for (unsigned lane = 0; lane < 4; ++lane)
        {
            const float lo = low[0] + ((lane & 1) ? -high[1] : low[1]) + ((lane & 2) ? -high[2] : low[2]);
            const float hi = high[0] + ((lane & 1) ? -low[1] : high[1]) + ((lane & 2) ? -low[2] : high[2]);
            diagonalLo[lane] = std::min(diagonalLo[lane], lo);
            diagonalHi[lane] = std::max(diagonalHi[lane], hi);
        }
    }
};
} // namespace avboit
