// SPDX-License-Identifier: MIT
#pragma once
#include "FrostChain.h"
#include <array>

namespace avboit
{
// Disjoint per-mip uint masks, one word per 4x4 output group. Odd stage indices
// and a reserved header word retain the diagnostic capture ABI. Even stages
// and B (stage 10) have no mask storage; B reuses ScreenTiles transparency bit 4.
struct TileSchedule
{
    static constexpr unsigned Stages = 11, GroupSize = 4, SourceGroupSize = 8, RequestSize = 64;
    std::array<unsigned, Stages> width{}, height{}, offset{}, capacity{};
    unsigned entries = 0;
    static TileSchedule Make(unsigned w, unsigned h, unsigned mipCount)
    {
        TileSchedule s;
        const auto chain = FrostChain::Make(w, h, mipCount);
        for (unsigned stage = 0; stage < Stages; ++stage)
        {
            if (stage == 10)
            {
                s.width[stage] = w;
                s.height[stage] = h;
            }
            else if (stage && stage / 2 < mipCount)
            {
                s.width[stage] = chain.width >> (stage / 2);
                s.height[stage] = chain.height >> ((stage & 1) ? stage / 2 : stage / 2 - 1);
            }
            s.offset[stage] = s.entries;
            unsigned block = stage == 10 ? SourceGroupSize : GroupSize;
            if (stage < 10 && (stage & 1))
                s.capacity[stage] = ((s.width[stage] + block - 1) / block) * ((s.height[stage] + block - 1) / block);
            s.entries += 1 + s.capacity[stage];
        }
        return s;
    }
};
} // namespace avboit
