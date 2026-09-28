// SPDX-License-Identifier: MIT
#include "sample/GeometrySubmission.h"
#include "sample/SubmissionOrder.h"
#include <set>
#include <iostream>

int main()
{
    using namespace avboit;
    auto check = [](bool condition)
    {
        if (!condition)
            throw std::runtime_error("Interface draw grouping failed");
    };
    MaterialDefaults defaults{1.3f, 1.5f, 1.f, false};
    MaterialTable materials;
    TransparencyMaterial frost{};
    frost.kind = 2;
    frost.transmission = {.8f, .8f, .8f, .5f};
    TransparencyMaterial smoke{};
    smoke.kind = 1;
    TransparencyMaterial plain = frost;
    plain.transmission[3] = 0;
    materials.entries = {frost, smoke, plain};
    std::vector<MaterialRecord> vertices(3 * 4); // Opaque prefix, not selected.
    for (auto batch : std::array<std::array<unsigned, 2>, 6>{{{7, 0}, {9, 0}, {7, 0}, {11, 1}, {12, 2}, {9, 0}}})
    {
        for (unsigned corner = 0; corner < 3; ++corner)
        {
            vertices.push_back({0, 0, 4, 2});
            vertices.push_back({1, 1, 1, float(batch[1])});
            vertices.push_back({0, 0, 0, float(batch[0])});
            vertices.push_back({0, 0, 0, 0});
        }
    }
    for (unsigned order : {0u, 1u, 2u})
    {
        auto submitted = vertices;
        ReorderTransparentTriangles(submitted, 3, 18, order, 42);
        auto selection = BuildGeometrySubmission(submitted, 3, 18, materials, defaults, true, true);
        check(selection.draws.size() == 2 && selection.indices.size() == 12);
        std::set<unsigned> indices, actors;
        for (const auto &draw : selection.draws)
        {
            check(draw.material == 0 && draw.indexCount == 6);
            actors.insert(draw.actor);
            for (unsigned j = draw.firstIndex; j < draw.firstIndex + draw.indexCount; ++j)
            {
                unsigned index = selection.indices[j];
                check(indices.insert(index).second);
                check(unsigned(submitted[(index + 3) * 4 + 2][3]) == draw.actor);
                check(unsigned(submitted[(index + 3) * 4 + 1][3]) == draw.material);
            }
        }
        check(actors == std::set<unsigned>{7, 9});
        check(BuildGeometrySubmission(submitted, 3, 18, materials, defaults, false, true).draws.empty());
        auto all = BuildGeometrySubmission(submitted, 3, 18, materials, defaults, true);
        check(all.indices.size() == 18 && all.draws.size() == 4);
        for (const auto &draw : all.draws)
            for (unsigned j = draw.firstIndex; j < draw.firstIndex + draw.indexCount; ++j)
            {
                unsigned index = all.indices[j];
                check(unsigned(submitted[(index + 3) * 4 + 1][3]) == draw.material);
                check(unsigned(submitted[(index + 3) * 4 + 2][3]) == draw.actor);
            }
    }
    std::cout << "Actor/material batching, shared materials and shuffled triangles PASS\n";
}
