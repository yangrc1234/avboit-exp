// SPDX-License-Identifier: MIT
// Both VDB formats use the same packed brick index and sampling constants.
#define SMOKE_MODE 4
#include "LocalVolume.hlsli"
#include "VdbGroupGeometry.hlsli"
#include "VdbProxyProjection.hlsli"

cbuffer ProxyDraw : register(b4) { uint ProxyActorVertex; };

float4 ProjectGroup(uint vertex, uint instance, out VolumeVaryings actor)
{
    actor = (VolumeVaryings)0;
    uint3 groups = (VdbGrid + 1u) / 2u;
    uint3 brick = 2u * uint3(instance % groups.x, (instance / groups.x) % groups.y,
                             instance / (groups.x * groups.y));
    bool occupied = false;
    [unroll] for (uint k = 0; k < 8; ++k)
    {
        uint3 b = brick + uint3(k & 1u, (k >> 1u) & 1u, k >> 2u);
        if (all(b < VdbGrid))
            occupied |= VdbEntry(b.x + VdbGrid.x * (b.y + VdbGrid.y * b.z)) != 0;
    }
    if (!occupied)
        return float4(0, 0, 0, 1);

    actor = LoadVolumeActor(ProxyActorVertex);
#if VDB_GROUP_MARCH
    actor.groupIndex = brick / 2u;
#endif
    float3 lo, hi;
    VdbGroupBounds(brick / 2u, lo, hi);
    bool crossesNearPlane;
    return ProjectVdbBox(vertex, actor, lo, hi, true, crossesNearPlane);
}

float4 main(uint vertex : SV_VertexID, uint instance : SV_InstanceID) : SV_Position
{
    VolumeVaryings actor;
    return ProjectGroup(vertex, instance, actor);
}
