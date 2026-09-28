// SPDX-License-Identifier: MIT
#include "sample/SubmissionOrder.h"
#include <algorithm>
#include <numeric>
#include <random>
#include <stdexcept>
namespace avboit
{
void ReorderTransparentTriangles(std::vector<std::array<float, 4>> &vertices, unsigned firstVertex,
                                 unsigned vertexCount, unsigned mode, unsigned seed)
{
    if (mode > 2 || vertexCount % 3 || (size_t(firstVertex) + vertexCount) * 4 > vertices.size())
        throw std::runtime_error("Invalid transparent triangle range/order");
    if (mode == 0)
        return;
    std::vector<unsigned> order(vertexCount / 3);
    std::iota(order.begin(), order.end(), 0);
    if (mode == 1)
        std::reverse(order.begin(), order.end());
    else
    {
        std::mt19937 random(seed);
        std::shuffle(order.begin(), order.end(), random);
    }
    auto source = vertices;
    for (unsigned triangle = 0; triangle < order.size(); ++triangle)
        std::copy_n(source.begin() + firstVertex * 4 + order[triangle] * 12, 12,
                    vertices.begin() + firstVertex * 4 + triangle * 12);
}
} // namespace avboit
