// SPDX-License-Identifier: MIT
#include "sample/MaterialTable.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace avboit
{
bool TransparencyMaterial::ScalarExtinction() const
{
    // Colored smoke must use the RGB lane, even though its opacity mask remains scalar.
    return kind == 1 && transmission[0] == transmission[1] && transmission[1] == transmission[2];
}
float EffectiveMaterial::MaximumDisplacement() const
{
    if (value.kind == 4)
        return value.normalSphere ? 28.8f * value.gain
                                  : (value.gain > 0 && value.ior > 1 ? std::numeric_limits<float>::infinity() : 0.f);
    return std::abs(value.displacement) * value.gain;
}
float EffectiveMaterial::FrostTheta() const
{
    if (value.kind != 2 && value.kind != 4)
        return 0;
    float rough = value.transmission[3];
    return std::min(.35f, rough * rough * (1.f - 1.f / value.ior)) / 1.177410f;
}
void MaterialTable::Build(std::vector<MaterialRecord> &vertices, const MaterialDefaults &defaults)
{
    if (vertices.size() % 4)
        throw std::runtime_error("Incomplete fixture vertex records");
    entries.clear();
    for (size_t v = 0; v < vertices.size(); v += 4)
    {
        TransparencyMaterial material;
        material.kind = unsigned(vertices[v][3]);
        if (material.kind == 3)
        {
            material.vfxPattern = unsigned(vertices[v + 3][3]);
            material.vfxPhase = vertices[v + 3][2];
        }
        material.color = vertices[v + 1];
        material.transmission = vertices[v + 2];
        // Imported normal meshes store normal.z here, not a refraction offset.
        material.displacement =
            material.kind == 4 ? 18.f : (material.kind == 2 && vertices[v + 3][3] >= -1.5f ? vertices[v + 3][2] : 0.f);
        material.ior = material.kind == 4 ? defaults.sphereIor : defaults.planarIor;
        material.gain = defaults.gain;
        material.normalSphere = defaults.normalSphere;
        auto match = std::find_if(entries.begin(), entries.end(),
                                  [&](const auto &entry)
                                  {
                                      return entry.kind == material.kind && entry.color == material.color &&
                                             entry.transmission == material.transmission &&
                                             entry.displacement == material.displacement &&
                                             entry.vfxPattern == material.vfxPattern &&
                                             entry.vfxPhase == material.vfxPhase;
                                  });
        unsigned index = unsigned(match - entries.begin());
        if (match == entries.end())
            entries.push_back(material);
        vertices[v + 1][3] = float(index);
    }
}
EffectiveMaterial MaterialTable::Resolve(unsigned index, const MaterialDefaults &defaults) const
{
    auto value = entries.at(index);
    if (value.inheritOptics)
    {
        value.ior = value.kind == 4 ? defaults.sphereIor : defaults.planarIor;
        value.gain = value.kind == 3 ? 1.f : defaults.gain;
        value.normalSphere = defaults.normalSphere;
    }
    return {value};
}
std::vector<MaterialRecord> MaterialTable::GpuRecords(const MaterialDefaults &defaults) const
{
    std::vector<MaterialRecord> records;
    records.reserve(entries.size() * 4);
    for (unsigned i = 0; i < entries.size(); ++i)
    {
        auto m = Resolve(i, defaults).value;
        records.push_back(m.color);
        records.push_back(m.transmission);
        records.push_back({m.ior, m.gain, m.displacement, float(m.normalSphere)});
        records.push_back({float(m.kind), float(m.ScalarExtinction()), float(m.vfxPattern), m.vfxPhase});
    }
    return records;
}
} // namespace avboit
