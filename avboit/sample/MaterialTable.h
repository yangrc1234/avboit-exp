// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <vector>

namespace avboit
{
using MaterialRecord = std::array<float, 4>;
struct MaterialDefaults
{
    float planarIor, sphereIor, gain;
    bool normalSphere;
};
struct TransparencyMaterial
{
    MaterialRecord color;        // Radiance RGB, coverage A.
    MaterialRecord transmission; // RGB transmittance, roughness.
    float ior = 1.3f, gain = 1, displacement = 0;
    unsigned kind = 0; // Fixture shading model, not an instance ID.
    bool normalSphere = false, inheritOptics = true;
    unsigned vfxPattern = 0; // 0 oscillating ring, 1 heat wave, 2 expanding shock.
    float vfxPhase = 0;      // Phase offset in cycles for expanding shock.
    bool ScalarExtinction() const;
};
struct EffectiveMaterial
{
    TransparencyMaterial value;
    float MaximumDisplacement() const;
    float FrostTheta() const;
};
class MaterialTable
{
  public:
    // Import fixture vertex attributes, deduplicate identical materials, and put
    // a table index in vertex record 1.w. Geometry and local coordinates stay intact.
    void Build(std::vector<MaterialRecord> &vertices, const MaterialDefaults &defaults);
    EffectiveMaterial Resolve(unsigned index, const MaterialDefaults &defaults) const;
    std::vector<MaterialRecord> GpuRecords(const MaterialDefaults &defaults) const;
    std::vector<TransparencyMaterial> entries;
};
} // namespace avboit
