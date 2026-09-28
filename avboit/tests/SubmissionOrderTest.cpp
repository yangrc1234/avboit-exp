// SPDX-License-Identifier: MIT
#include "sample/SubmissionOrder.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
int main()
{
    std::vector<std::array<float, 4>> source(12 * 7);
    for (unsigned i = 0; i < source.size(); ++i)
        source[i] = {float(i), float(i + 1), float(i + 2), float(i + 3)};
    for (unsigned mode = 0; mode < 3; ++mode)
    {
        auto reordered = source;
        avboit::ReorderTransparentTriangles(reordered, 3, 15, mode, 42);
        auto require = [](bool ok)
        {
            if (!ok)
                throw std::runtime_error("Triangle permutation contract failed");
        };
        require(std::equal(source.begin(), source.begin() + 12, reordered.begin()));
        require(std::equal(source.begin() + 72, source.end(), reordered.begin() + 72));
        std::vector<unsigned> seen;
        for (unsigned t = 0; t < 5; ++t)
        {
            unsigned record = unsigned(reordered[12 + t * 12][0]);
            require(record >= 12 && record < 72 && record % 12 == 0);
            require(
                std::equal(reordered.begin() + 12 + t * 12, reordered.begin() + 24 + t * 12, source.begin() + record));
            seen.push_back(record);
        }
        std::sort(seen.begin(), seen.end());
        require(seen == std::vector<unsigned>({12, 24, 36, 48, 60}));
        if (mode == 1)
            require(reordered[12] == source[60]);
        auto repeated = source;
        avboit::ReorderTransparentTriangles(repeated, 3, 15, mode, 42);
        require(repeated == reordered);
    }
    std::cout << "Opaque/VFX range preservation, complete triangles and deterministic permutation PASS\n";
}
