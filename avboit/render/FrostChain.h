// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <array>

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
    // Each row is indexed by the material's maximum mip; columns 0..4 are
    // Gaussian output halos. The base samples B prime or opaque at any UV,
    // so no full-resolution source halo is needed.
    // Precompute on quality/size changes. Work-group rounding is deliberately
    // included so independently marked producer rectangles cover all consumers.
    std::array<std::array<float, 4>, 25> Footprints(unsigned w, unsigned h, bool cubic) const
    {
        std::array<std::array<float, 4>, 25> out{};
        for (unsigned top = 0; top < mipCount; ++top)
            for (unsigned axis = 0; axis < 2; ++axis)
            {
                const float full = float(axis ? h : w), base = float(axis ? height : width);
                const float pitch = full / base;
                for (int level = int(top); level >= 0; --level)
                {
                    float padding = (cubic ? 2.f : 1.f) * pitch * float(1u << level) + 1.f;
                    if (unsigned(level) < top)
                        padding =
                            std::max(padding, out[top * 5 + level + 1][axis] + 4.f * pitch * float(1u << (level + 1)) +
                                                  3.708608528f * pitch * float(1u << level));
                    out[top * 5 + level][axis] = padding;
                }
            }
        return out;
    }
};
} // namespace avboit
