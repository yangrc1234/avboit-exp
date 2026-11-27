// SPDX-License-Identifier: MIT
#ifndef AVBOIT_BAKED_SMOKE
#define AVBOIT_BAKED_SMOKE
#include "SmokeTrails.hlsli"
Texture3D<float2> VolumeDensity : register(t26);
Buffer<uint> SmokeBricks : register(t33);
Texture3D<float> GameplaySmokeMask : register(t34);

// The asset selects the same storage permutation for camera and light rays.
// Mode 0 keeps runtime dispatch for the offline accumulation audit.
#ifndef SMOKE_MODE
#define SMOKE_MODE 0
#endif
#define SPECIALIZED_VDB (SMOKE_MODE == 2 || SMOKE_MODE == 4)
#if SPECIALIZED_VDB
cbuffer VdbSamplingConstants : register(b3)
{
    uint3 VdbSize;
    uint VdbPayloadOffset;
    uint3 VdbGrid;
    uint VdbSkipEnabled;
    uint3 VdbFootprintSize;
    float VdbAmbientColumnScale;
#if VDB_RAY_HULL
    float4 VdbAxisLo, VdbAxisHi, VdbDiagonalLo, VdbDiagonalHi;
#endif
};
#endif

uint3 SmokeVoxelSize()
{
#if SPECIALIZED_VDB
    return VdbSize;
#else
    return uint3(SmokeBricks[1], SmokeBricks[2], SmokeBricks[3]);
#endif
}
uint3 VdbBrickGrid()
{
#if SPECIALIZED_VDB
    return VdbGrid;
#else
    return (SmokeVoxelSize() + 3) / 4;
#endif
}
bool VdbSkippingEnabled()
{
#if SPECIALIZED_VDB
    return VdbSkipEnabled != 0;
#else
    return (SmokeBricks[0] & 1u) != 0;
#endif
}
uint VdbEntry(uint index)
{
    // The validated VDB file header is eight words, followed by packed uint16 indices.
    return (SmokeBricks[8 + index / 2] >> ((index & 1) * 16)) & 65535;
}
uint SmokeStorageKind()
{
#if SMOKE_MODE == 1
    return 0; // Static DDS.
#elif SMOKE_MODE == 2
    return 3; // FP16 bricks.
#elif SMOKE_MODE == 4
    return 1; // Quantized bricks.
#elif SMOKE_MODE >= 6
    return (SMOKE_MODE - 5) * 256; // Gameplay dense / buffer / atlas.
#else
    return SmokeBricks[0];
#endif
}
bool UsesFp16Density()
{
#if SMOKE_MODE == 2
    return true;
#elif SMOKE_MODE == 4
    return false;
#else
    return SmokeBricks[0] >= 3;
#endif
}
float LoadSmokeVoxel(uint record, uint voxel, bool fp16Density)
{
    if (fp16Density)
        return f16tof32((SmokeBricks[record + voxel / 2] >> ((voxel & 1) * 16)) & 65535);
    return float((SmokeBricks[record + 1 + voxel / 8] >> ((voxel % 8) * 4)) & 15u);
}
float SampleSmokeFrame(uint index, uint localVoxel, float3 f, float3 filterRandom)
{
    uint entry = VdbEntry(index);
    if (entry == 0)
        return 0.;
    bool fp16Density = UsesFp16Density();
#if SPECIALIZED_VDB
    uint payload = VdbPayloadOffset;
#else
    uint payload = SmokeBricks[5];
#endif
    uint record = payload + (entry - 1) * (fp16Density ? 64 : 17);
    float value = 0.;
#if SMOKE_STOCHASTIC
    {
        uint3 corner = uint3(filterRandom < f);
        value = LoadSmokeVoxel(record, localVoxel + corner.x + 5 * corner.y + 25 * corner.z, fp16Density);
    }
#else
    {
        [unroll] for (uint z = 0; z < 2; ++z)[unroll] for (uint y = 0; y < 2; ++y)[unroll] for (uint x = 0; x < 2; ++x)
        {
            float3 w = lerp(1. - f, f, float3(x, y, z));
            value += w.x * w.y * w.z * LoadSmokeVoxel(record, localVoxel + x + 5 * y + 25 * z, fp16Density);
        }
    }
#endif
    if (fp16Density)
        return value;
    // The interpolation border shares this brick's affine quantization range.
    uint packedRange = SmokeBricks[record];
    return lerp(f16tof32(packedRange & 65535), f16tof32(packedRange >> 16), value * (1. / 15.));
}
// All providers reconstruct the identical R8 density, including its feather.
float SampleGameplayCoverage(float3 uv)
{
    if (SmokeStorageKind() == 256)
        return GameplaySmokeMask.SampleLevel(LinearClamp, uv, 0);
    uint n = SmokeBricks[1], grid = n / 8;
    float3 p = clamp(uv * n - .5, 0., float(n - 1));
    uint3 cell = uint3(p), brick = cell / 8, local = cell % 8;
    uint entry = SmokeBricks[9 + brick.x + grid * (brick.y + grid * brick.z)];
    if (entry < 2)
        return float(entry);
    uint record = entry - 2;
    float3 f = frac(p);
    if (SmokeStorageKind() == 768)
    {
        uint axis = SmokeBricks[5];
        uint3 tile = uint3(record % axis, (record / axis) % axis, record / (axis * axis));
        float3 atlasUV = (float3(tile * 9 + local) + f + .5) / float(SmokeBricks[6]);
        return GameplaySmokeMask.SampleLevel(LinearClamp, atlasUV, 0);
    }
    float result = 0;
    uint base = SmokeBricks[4] + record * 183;
    [unroll] for (uint z = 0; z < 2; ++z)[unroll] for (uint y = 0; y < 2; ++y)[unroll] for (uint x = 0; x < 2; ++x)
    {
        uint3 c = local + uint3(x, y, z);
        uint index = c.x + 9 * (c.y + 9 * c.z);
        float value = float((SmokeBricks[base + index / 4] >> ((index % 4) * 8)) & 255u) / 255.;
        float3 w = lerp(1. - f, f, float3(x, y, z));
        result += value * w.x * w.y * w.z;
    }
    return result;
}
float2 SampleSmokeDensity(float3 uv, float3 world, float3 filterRandom = 0)
{
    if (SmokeStorageKind() >= 256)
    {
        if (any(uv < 0.) || any(uv > 1.))
            return 0.;
        // Authority controls coverage. Client detail is bounded by the filtered authority coverage.
        float coverage = SampleGameplayCoverage(uv);
        // 64^3 periodic interior with a wrapped border: continuous through frac seams.
        // Visual advection only: the gameplay mask and radial envelope stay fixed.
        // Shared phase across splat, camera integration and light-space MSM.
        float phase = SMOKE_FLOW_TIME;
        float3 flow = uv * 2.3 - phase * float3(.018, .032, -.012);
        // Slowly evolving domain warp avoids rigid translation of the texture.
        flow += .10 * sin(uv.yzx * 6.2831853 + phase * float3(.31, -.23, .27));
        float3 detailUV = (frac(flow) * 64. + 1.) / 66.;
        float detail = VolumeDensity.SampleLevel(LinearClamp, detailUV, 0).r;
        // Subtractive erosion: preserve the gameplay mask, but allow visual holes.
        // Radius follows the gameplay radius slider, independent of GPU layout.
        float radiusCells = max(float(SmokeBricks[8]) / 1024., 1.);
        float r = length((uv - .5) * float(SmokeBricks[1])) / radiusCells;
        float baseDensity = saturate(1. - r * r);
        float erosion = .45 * (1. - saturate(detail));
        return float2(coverage * max(baseDensity - erosion, 0.) * 2.5 * SmokeTrailVisibility(world), 0.);
    }
    if (SmokeStorageKind() == 0)
        return VolumeDensity.SampleLevel(LinearClamp, uv, 0);
#if defined(ACCUMULATION_AUDIT) && (ACCUMULATION_AUDIT & 16)
    float mask = 1.; // Only valid for the all-one gameplay mask benchmark.
#else
    float mask = GameplaySmokeMask.SampleLevel(LinearClamp, uv, 0);
#endif
    if (mask <= 0. || any(uv < 0.) || any(uv > 1.))
        return 0.;
    uint3 size = SmokeVoxelSize();
    float3 p = clamp(uv * size - .5, 0., float3(size - 1));
    uint3 cell = uint3(p), brick = cell / 4, local = cell % 4, grid = VdbBrickGrid();
    uint index = brick.x + grid.x * (brick.y + grid.y * brick.z);
    uint localVoxel = local.x + 5 * local.y + 25 * local.z;
    float3 f = frac(p);
    float value = SampleSmokeFrame(index, localVoxel, f, filterRandom);
    return float2(value * mask, 0.);
}
bool SmokeBrickPresent(uint3 brick)
{
    if (SmokeStorageKind() >= 256)
    {
        uint grid = SmokeBricks[1] / 8;
        return SmokeBricks[9 + brick.x + grid * (brick.y + grid * brick.z)] != 0;
    }
    uint3 grid = VdbBrickGrid();
    uint index = brick.x + grid.x * (brick.y + grid.y * brick.z);
    return VdbEntry(index) != 0;
}
// UV direction is per caller's distance unit (view depth or world metres).
// Brick support includes the positive
// interpolation border, so no nonzero filtering taps are discarded.
float SmokeEmptyRun(float3 uv, float3 direction, out float recheckDistance)
{
    recheckDistance = 0.;
    bool gameplay = SmokeStorageKind() >= 256;
    if (gameplay ? SmokeBricks[7] == 0 : !VdbSkippingEnabled())
        return 0.;
    float3 size = SmokeVoxelSize();
    float3 p = clamp(uv * size - .5, 0., size - 1.), d = direction * size;
    int brickSize = gameplay ? 8 : 4;
    int3 cell = int3(floor(p / float(brickSize))), grid = gameplay ? (int3(size) + 7) / 8 : int3(VdbBrickGrid());
    int3 step = int3(d.x >= 0 ? 1 : -1, d.y >= 0 ? 1 : -1, d.z >= 0 ? 1 : -1);
    float distance = 0.;
    [loop] for (uint n = 0; n < 64; ++n)
    {
        if (any(cell < 0) || any(cell >= grid))
            return distance;
        // Account for clamped half-texel fringe at the volume boundary.
        float3 boundary = float3(cell * brickSize) +
                          float3(step.x > 0 ? brickSize : 0, step.y > 0 ? brickSize : 0, step.z > 0 ? brickSize : 0);
        float3 origin = uv * size - .5;
        float3 t = float3(abs(d.x) > 1e-10 ? (boundary.x - origin.x) / d.x : 1e30,
                          abs(d.y) > 1e-10 ? (boundary.y - origin.y) / d.y : 1e30,
                          abs(d.z) > 1e-10 ? (boundary.z - origin.z) / d.z : 1e30);
        float exitDistance = max(distance, min(t.x, min(t.y, t.z)));
        if (SmokeBrickPresent(uint3(cell)))
        {
            // A nonempty brick stays nonempty until its exit. Cache only the
            // classification, never density or lighting. After an empty run,
            // recheck at the first occupied brick so no boundary is skipped.
            recheckDistance = distance > 0. ? distance : exitDistance;
            return distance;
        }
        distance = exitDistance;
        if (t.x <= distance)
            cell.x += step.x;
        if (t.y <= distance)
            cell.y += step.y;
        if (t.z <= distance)
            cell.z += step.z;
    }
    return distance; // Only the traversed, proven-empty prefix is skipped.
}
float SmokeEmptyRun(float3 uv, float3 direction)
{
    float unused;
    return SmokeEmptyRun(uv, direction, unused);
}
#endif
