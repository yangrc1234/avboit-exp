// SPDX-License-Identifier: MIT
// Homogeneous world-space AABB prototype. Geometry remains an actor-owned draw;
// its six proxy vertices carry center.xyz and extent.xyz, not a surface mesh.
#ifndef AVBOIT_LOCAL_VOLUME
#define AVBOIT_LOCAL_VOLUME
#include "TransparencyCommon.hlsli"
#include "DepthMapping.hlsli"
struct VolumeVaryings
{
    float4 position : SV_Position;
    nointerpolation float3 center : CENTER;
    nointerpolation float3 extent : EXTENT;
    nointerpolation float3 sigma : SIGMA;
    nointerpolation float3 source : SOURCE;
};
VolumeVaryings volumeVertex(uint id : SV_VertexID)
{
    VolumeVaryings o;
    o.center = Vertices[id * 4].xyz;
    o.extent = Vertices[id * 4 + 3].xyz;
    uint material = uint(Vertices[id * 4 + 1].w) * 4;
    o.sigma = max(Materials[material + 1].rgb, 0.);
    o.source = max(Materials[material].rgb, 0.);
    float2 low = 1e30, high = -1e30;
    bool nearCrossing = false;
    for (uint c = 0; c < 8; ++c)
    {
        float3 d = o.center + o.extent * float3((c & 1) ? 1 : -1, (c & 2) ? 1 : -1, (c & 4) ? 1 : -1) - Camera[0].xyz;
        float3 v = float3(dot(d, Camera[1].xyz), dot(d, Camera[2].xyz), dot(d, Camera[3].xyz));
        nearCrossing |= v.z <= Camera[5].z;
        float2 ndc = v.xy / max(v.z, Camera[5].z) / float2(float(WIDTH) / HEIGHT * .57735027, .57735027);
        ndc += Camera[11].xy * float2(2., -2.) / Camera[6].xy;
        low = min(low, ndc);
        high = max(high, ndc);
    }
    low = nearCrossing ? -1. : clamp(low, -1., 1.);
    high = nearCrossing ? 1. : clamp(high, -1., 1.);
    const float2 corners[6] = {float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1)};
    o.position = float4(lerp(low, high, corners[uint(Vertices[id * 4 + 3].w)]), 1, 1);
    return o;
}
bool VolumeInterval(VolumeVaryings i, float2 uv, float opaqueZ, out float2 interval, out float rayZ)
{
    float3 direction = WorldRay(uv);
    rayZ = max(dot(direction, Camera[3].xyz), 1e-6);
    float3 origin = Camera[0].xyz;
    float entry = Camera[5].z / rayZ, exit = min(opaqueZ, Camera[5].w) / rayZ;
    for (uint axis = 0; axis < 3; ++axis)
    {
        if (abs(direction[axis]) < 1e-7)
        {
            if (abs(origin[axis] - i.center[axis]) > i.extent[axis])
                exit = -1;
        }
        else
        {
            float2 hit = (float2(i.center[axis] - i.extent[axis], i.center[axis] + i.extent[axis]) - origin[axis]) /
                         direction[axis];
            entry = max(entry, min(hit.x, hit.y));
            exit = min(exit, max(hit.x, hit.y));
        }
    }
    interval = float2(entry, exit) * rayZ;
    return exit > entry;
}
uint2 VolumeVirtualRange(float2 interval, uint count)
{
    return min(
        uint2(NormalizedLogDepth(interval.x, 80., 1.) * count, ceil(NormalizedLogDepth(interval.y, 80., 1.) * count)),
        count);
}
float2 VolumeSegment(uint cell, uint count, float2 interval)
{
    float2 z = exp2(float2(cell, cell + 1) / count * log2(81.)) - 1.;
    return float2(max(z.x, interval.x), min(z.y, interval.y));
}
// Optional imported density (R) and vertical optical-depth integral in UV units (G).
Texture3D<float2> VolumeDensity : register(t26);
float2 SampleVolumeData(VolumeVaryings i, float2 uv, float viewZ, float rayZ)
{
    float3 world = Camera[0].xyz + WorldRay(uv) * (viewZ / rayZ);
    float3 local = (world - i.center) / (2. * i.extent) + .5;
    return VolumeDensity.SampleLevel(LinearClamp, local, 0);
}
#endif
