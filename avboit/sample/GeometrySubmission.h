// SPDX-License-Identifier: MIT
#pragma once
#include "sample/MaterialTable.h"
#include "render/GeometryDraw.h"
#include <map>
#include <stdexcept>
namespace avboit
{
// Keep vertex pulling, but submit every actor/material section independently.
// The order of first appearance controls draw order; triangle shuffles are
// retained inside each section. Indices are relative to the supplied SRV range.
struct GeometrySubmission
{
    std::vector<unsigned> indices;
    std::vector<GeometryDraw> draws;
};
inline GeometrySubmission BuildGeometrySubmission(const std::vector<MaterialRecord> &vertices, unsigned first,
                                                  unsigned count, const MaterialTable &materials,
                                                  const MaterialDefaults &defaults, bool frost,
                                                  bool interfacesOnly = false)
{
    if (count % 3 || size_t(first + count) * 4 > vertices.size())
        throw std::invalid_argument("Invalid geometry triangle range");
    GeometrySubmission result;
    std::map<std::pair<unsigned, unsigned>, unsigned> lookup;
    std::vector<std::vector<unsigned>> actorIndices;
    for (unsigned triangle = 0; triangle < count; triangle += 3)
    {
        unsigned id = unsigned(vertices[(first + triangle) * 4 + 1][3]);
        const auto material = materials.Resolve(id, defaults);
        if (interfacesOnly)
        {
            if (material.value.kind != 2 && material.value.kind != 4)
                continue;
            if (!(frost && material.FrostTheta() > 0) && material.MaximumDisplacement() == 0)
                continue;
        }
        // Actor identity travels with each vertex through triangle shuffling.
        const unsigned actor = unsigned(vertices[(first + triangle) * 4 + 2][3]);
        for (unsigned corner = 1; corner < 3; ++corner)
            if (unsigned(vertices[(first + triangle + corner) * 4 + 1][3]) != id ||
                unsigned(vertices[(first + triangle + corner) * 4 + 2][3]) != actor)
                throw std::invalid_argument("Triangle spans multiple actors or materials");
        auto inserted = lookup.emplace(std::make_pair(actor, id), unsigned(result.draws.size()));
        if (inserted.second)
        {
            result.draws.push_back({actor, id, 0, 0});
            actorIndices.emplace_back();
        }
        auto &indices = actorIndices[inserted.first->second];
        for (unsigned corner = 0; corner < 3; ++corner)
            indices.push_back(triangle + corner);
    }
    result.indices.reserve(count);
    for (unsigned batch = 0; batch < result.draws.size(); ++batch)
    {
        result.draws[batch].firstIndex = unsigned(result.indices.size());
        result.draws[batch].indexCount = unsigned(actorIndices[batch].size());
        result.indices.insert(result.indices.end(), actorIndices[batch].begin(), actorIndices[batch].end());
    }
    return result;
}
} // namespace avboit
