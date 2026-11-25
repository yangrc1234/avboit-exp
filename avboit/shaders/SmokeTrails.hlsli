// SPDX-License-Identifier: MIT
#ifndef AVBOIT_SMOKE_TRAILS
#define AVBOIT_SMOKE_TRAILS
Buffer<float4> SmokeTrails : register(t35);
Texture3D<uint> SmokeTrailCandidates : register(t36);
float SmokeCapsuleClear(uint i, float3 world)
{
    float4 a = SmokeTrails[1 + 2 * i], b = SmokeTrails[2 + 2 * i];
    float3 ab = b.xyz - a.xyz, ap = world - a.xyz;
    float t = saturate(dot(ap, ab) / max(dot(ab, ab), 1e-8));
    float distanceToAxis = length(ap - t * ab);
    return b.w * (1. - smoothstep(a.w * .65, a.w, distanceToAxis));
}
float SmokeTrailVisibility(float3 world)
{
    float clear = 0.;
    uint count = min(uint(SmokeTrails[0].x), 15u);
    if (count == 0)
        return 1.;
    // Preserve the original counter loop for an honest exhaustive-reference timing.
    if (SmokeTrails[0].y == 0.)
    {
        [loop] for (uint i = 0; i < count; ++i) clear = max(clear, SmokeCapsuleClear(i, world));
        return 1. - clear;
    }
    uint mask = (1u << count) - 1u;
    float3 cell = (world - SmokeTrails[31].xyz) * SmokeTrails[32].xyz;
    // Out-of-grid positions fall back to analytic evaluation, never a clamped cell.
    if (all(cell >= 0.) && all(cell < 16.))
        mask = SmokeTrailCandidates.Load(int4(int3(cell), 0));
    [loop] while (mask != 0)
    {
        uint i = firstbitlow(mask);
        mask &= mask - 1u;
        clear = max(clear, SmokeCapsuleClear(i, world)); // Union, not repeated multiplication at overlaps.
    }
    return 1. - clear;
}
#endif
