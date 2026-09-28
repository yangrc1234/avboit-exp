// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace avboit
{
constexpr uint32_t VirtualSlices = 2048;
constexpr uint32_t PhysicalSlices = 128;
constexpr int GuardSlices = 3;

// CPU oracle for tests only. The renderer will consume GPU-generated mappings.
inline std::vector<uint32_t> BuildDepthWarp(std::vector<uint32_t> raw, uint32_t physicalBudget = PhysicalSlices)
{
    std::vector<uint32_t> prefix;
    for (;;)
    {
        prefix.assign(raw.size() + 1, 0);
        for (int i = 0; i < int(raw.size()); ++i)
        {
            uint32_t occupied = 0;
            for (int j = std::max(0, i - GuardSlices); j <= std::min(int(raw.size()) - 1, i + GuardSlices); ++j)
                occupied |= raw[j] != 0;
            prefix[i + 1] = prefix[i] + occupied;
        }
        if (prefix.back() < physicalBudget)
            break;
        std::vector<uint32_t> merged(raw.size() / 2);
        for (size_t i = 0; i < merged.size(); ++i)
            merged[i] = raw[2 * i] | raw[2 * i + 1];
        raw = std::move(merged);
    }
    std::vector<uint32_t> result(2 + 2 * (VirtualSlices + 1));
    result[0] = uint32_t(raw.size());
    result[1] = prefix.back();
    for (size_t i = 0; i <= VirtualSlices; ++i)
    {
        const bool active = i < raw.size() && prefix[i + 1] != prefix[i];
        const bool previous = i > 0 && i < raw.size() && prefix[i] != prefix[i - 1];
        const bool next = i + 1 < raw.size() && prefix[i + 2] != prefix[i + 1];
        result[2 + 2 * i] = prefix[std::min(i, raw.size())];
        result[3 + 2 * i] = uint32_t(active) | (active && !previous ? 2u : 0u) | (active && !next ? 4u : 0u);
    }
    return result;
}
} // namespace avboit
