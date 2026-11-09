// SPDX-License-Identifier: MIT
#ifndef AVBOIT_BAKED_SMOKE
#define AVBOIT_BAKED_SMOKE
Texture3D<float2> VolumeDensity : register(t26);
Buffer<uint> SmokeBricks : register(t33);
Texture3D<float> GameplaySmokeMask : register(t34);

float DecodeSmokeVoxel(uint record, uint voxel, float2 range, bool halfDensity)
{
    if (halfDensity)
        return f16tof32((SmokeBricks[record + voxel / 2] >> ((voxel & 1) * 16)) & 65535);
    uint q = (SmokeBricks[record + 1 + voxel / 8] >> (4 * (voxel % 8))) & 15;
    return lerp(range.x, range.y, q / 15.);
}
float SampleSmokeFrame(float3 uv, uint base)
{
    uint3 size = uint3(SmokeBricks[1], SmokeBricks[2], SmokeBricks[3]);
    float3 p = clamp(uv * size - .5, 0., float3(size - 1));
    uint3 cell = uint3(p), brick = cell / 4, local = cell % 4, grid = size / 4;
    uint index = brick.x + grid.x * (brick.y + grid.y * brick.z);
    uint entry = (SmokeBricks[base + SmokeBricks[base + 4] + index / 2] >> ((index & 1) * 16)) & 65535;
    if (entry == 0)
        return 0.;
    bool halfDensity = SmokeBricks[base] >= 3;
    uint record = base + SmokeBricks[base + 5] + (entry - 1) * (halfDensity ? 64 : 17);
    uint ranges = SmokeBricks[record];
    float2 range = float2(f16tof32(ranges & 65535), f16tof32(ranges >> 16));
    float3 f = frac(p);
    float value = 0.;
    [unroll] for (uint z = 0; z < 2; ++z)[unroll] for (uint y = 0; y < 2; ++y)[unroll] for (uint x = 0; x < 2; ++x)
    {
        uint3 v = local + uint3(x, y, z);
        float3 w = lerp(1. - f, f, float3(x, y, z));
        value += w.x * w.y * w.z * DecodeSmokeVoxel(record, v.x + 5 * (v.y + 5 * v.z), range, halfDensity);
    }
    // No precomputed lighting integral: the masked density uses dynamic MSM.
    return value;
}
float2 SampleSmokeDensity(float3 uv)
{
    if (SmokeBricks[0] == 0)
        return VolumeDensity.SampleLevel(LinearClamp, uv, 0);
    float mask = GameplaySmokeMask.SampleLevel(LinearClamp, uv, 0);
    if (mask <= 0. || any(uv < 0.) || any(uv > 1.))
        return 0.;
    float blend = asfloat(SmokeBricks[7]);
    float value = SampleSmokeFrame(uv, 0);
    if (blend > 0.)
        value = lerp(value, SampleSmokeFrame(uv, SmokeBricks[6]), blend);
    return float2(value * mask, 0.);
}
bool SmokeBrickPresent(uint3 brick)
{
    uint3 grid = uint3(SmokeBricks[1], SmokeBricks[2], SmokeBricks[3]) / 4;
    uint index = brick.x + grid.x * (brick.y + grid.y * brick.z);
    uint a = (SmokeBricks[SmokeBricks[4] + index / 2] >> ((index & 1) * 16)) & 65535;
    if (a != 0 || asfloat(SmokeBricks[7]) <= 0.)
        return a != 0;
    uint base = SmokeBricks[6];
    uint b = (SmokeBricks[base + SmokeBricks[base + 4] + index / 2] >> ((index & 1) * 16)) & 65535;
    return b != 0;
}
// UV direction is per caller's distance unit (view depth or world metres).
// Both animation frames must be empty. Brick support includes the positive
// interpolation border, so no nonzero filtering taps are discarded.
float SmokeEmptyRun(float3 uv, float3 direction)
{
    if ((SmokeBricks[0] & 1u) == 0)
        return 0.;
    float3 size = float3(SmokeBricks[1], SmokeBricks[2], SmokeBricks[3]);
    float3 p = clamp(uv * size - .5, 0., size - 1.), d = direction * size;
    int3 cell = int3(floor(p / 4.)), grid = int3(size) / 4;
    int3 step = int3(d.x >= 0 ? 1 : -1, d.y >= 0 ? 1 : -1, d.z >= 0 ? 1 : -1);
    float distance = 0.;
    [loop] for (uint n = 0; n < 64; ++n)
    {
        if (any(cell < 0) || any(cell >= grid))
            return distance;
        if (SmokeBrickPresent(uint3(cell)))
            return distance;
        // Account for clamped half-texel fringe at the volume boundary.
        float3 boundary = float3(cell * 4) + float3(step.x > 0 ? 4 : 0, step.y > 0 ? 4 : 0, step.z > 0 ? 4 : 0);
        float3 origin = uv * size - .5;
        float3 t = float3(abs(d.x) > 1e-10 ? (boundary.x - origin.x) / d.x : 1e30,
                          abs(d.y) > 1e-10 ? (boundary.y - origin.y) / d.y : 1e30,
                          abs(d.z) > 1e-10 ? (boundary.z - origin.z) / d.z : 1e30);
        distance = max(distance, min(t.x, min(t.y, t.z)));
        if (t.x <= distance)
            cell.x += step.x;
        if (t.y <= distance)
            cell.y += step.y;
        if (t.z <= distance)
            cell.z += step.z;
    }
    return distance; // Only the traversed, proven-empty prefix is skipped.
}
#endif
