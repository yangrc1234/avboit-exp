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
    unsigned vfxPattern = 0;     // 0 oscillating ring, 1 heat wave, 2 expanding shock.
    float vfxPhase = 0;          // Phase offset in cycles for expanding shock.
    unsigned surfacePattern = 0; // Normal-mesh fixture: 0 glass, 1 noise shell, 2 striped inner layer.
    float cameraOffsetBins = 0;  // Physical Z bins; positive pulls OIT ordering toward the camera.
    float patternScale = 6;      // Material-local noise frequency.
    bool ScalarExtinction() const;
};
struct EffectiveMaterial
{
    TransparencyMaterial value;
    float MaximumDisplacement() const;
    float FrostTheta() const;
    bool SupportsCameraOffset() const;
    float OitCameraOffsetBins() const;
};
class MaterialTable
{
  public:
    static constexpr unsigned GpuRecordCount = 5;
    // Import fixture vertex attributes, deduplicate identical materials, and put
    // a table index in vertex record 1.w. Geometry and local coordinates stay intact.
    void Build(std::vector<MaterialRecord> &vertices, const MaterialDefaults &defaults);
    EffectiveMaterial Resolve(unsigned index, const MaterialDefaults &defaults) const;
    std::vector<MaterialRecord> GpuRecords(const MaterialDefaults &defaults) const;
    std::vector<TransparencyMaterial> entries;
};
} // namespace avboit
