// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>

namespace avboit
{
// Screen-space reference units: one unit is 1/1440 of the viewport height.
// Quality adds fine levels; the common coarse levels never change scale.
struct FrostChain
{
    static constexpr unsigned MinMips = 3, MaxMips = 5, ReferenceHeight = 1440;
    unsigned width, height, mipCount;
    static FrostChain Make(unsigned renderWidth, unsigned renderHeight, unsigned count)
    {
        count = std::clamp(count, MinMips, MaxMips);
        unsigned coarseWidth = std::max(1u, unsigned(std::lround(45.0 * renderWidth / renderHeight)));
        return {coarseWidth << (count - 1), 45u << (count - 1), count};
    }
    // Matches the old sparse chain's variance at reference mip 2 and beyond.
    static float Variance(unsigned referenceMip)
    {
        return std::max(0.f, 9.f + 2.12297680594f * (std::pow(4.f, float(referenceMip)) - 16.f) / 3.f);
    }
    float VarianceAt(unsigned mip) const { return Variance(6 - mipCount + mip); }
};
} // namespace avboit
