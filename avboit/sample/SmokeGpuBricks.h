// SPDX-License-Identifier: MIT
#pragma once
#include <vector>
#include <array>
#include <cstdint>
#include <algorithm>
#include <stdexcept>
namespace avboit
{
// Grid of 8^3 interiors with a positive one-texel apron. Choosing the brick
// from floor(uv*N-.5) makes 9^3 sufficient for every trilinear footprint.
// Entries: 0 empty, 1 full, >=2 mixed record index + 2.
struct SmokeGpuBricks
{
    std::vector<uint32_t> words;
    std::vector<uint8_t> texture;
    unsigned textureSize = 1, mixed = 0;
    SmokeGpuBricks(const std::vector<uint8_t> &density, unsigned n, unsigned mode, bool skip, float radiusCells = 0)
    {
        if (n % 8 || density.size() != size_t(n) * n * n || mode > 2)
            throw std::invalid_argument("Smoke GPU grid");
        unsigned grid = n / 8, count = grid * grid * grid;
        words.assign(9 + count, 0);
        words[0] = 256 * (mode + 1);
        words[1] = words[2] = words[3] = n;
        words[4] = 9 + count;
        // Fixed-point radius in grid cells, shared by all GPU storage layouts.
        words[8] = unsigned((radiusCells > 0 ? radiusCells : n * .5f) * 1024.f);
        words[7] = skip ? 1u : 0u;
        std::vector<std::array<uint8_t, 729>> records;
        for (unsigned z = 0; z < grid; ++z)
            for (unsigned y = 0; y < grid; ++y)
                for (unsigned x = 0; x < grid; ++x)
                {
                    std::array<uint8_t, 729> record{};
                    bool empty = true, full = true;
                    for (unsigned k = 0; k < 9; ++k)
                        for (unsigned j = 0; j < 9; ++j)
                            for (unsigned i = 0; i < 9; ++i)
                            {
                                auto d = density[std::min(x * 8 + i, n - 1) +
                                                 n * (std::min(y * 8 + j, n - 1) + n * std::min(z * 8 + k, n - 1))];
                                record[i + 9 * (j + 9 * k)] = d;
                                empty &= d == 0;
                                full &= d == 255;
                            }
                    auto &entry = words[9 + x + grid * (y + grid * z)];
                    if (full)
                        entry = 1;
                    else if (!empty)
                    {
                        entry = unsigned(records.size()) + 2;
                        records.push_back(record);
                    }
                }
        mixed = unsigned(records.size());
        if (mode == 0)
        {
            texture = density;
            textureSize = n;
        }
        else if (mode == 1)
        {
            texture = {0};
            words.resize(words.size() + mixed * 183, 0);
            for (unsigned r = 0; r < mixed; ++r)
                for (unsigned i = 0; i < 729; ++i)
                    words[words[4] + r * 183 + i / 4] |= uint32_t(records[r][i]) << ((i % 4) * 8);
        }
        else
        {
            unsigned axis = 1;
            while (axis * axis * axis < mixed)
                ++axis;
            words[5] = axis;
            textureSize = axis * 9;
            texture.assign(size_t(textureSize) * textureSize * textureSize, 0);
            for (unsigned r = 0; r < mixed; ++r)
                for (unsigned k = 0; k < 9; ++k)
                    for (unsigned j = 0; j < 9; ++j)
                        for (unsigned i = 0; i < 9; ++i)
                        {
                            unsigned x = (r % axis) * 9 + i, y = ((r / axis) % axis) * 9 + j,
                                     z = (r / (axis * axis)) * 9 + k;
                            texture[x + textureSize * (y + textureSize * z)] = records[r][i + 9 * (j + 9 * k)];
                        }
        }
        words[6] = textureSize;
    }
};
} // namespace avboit
