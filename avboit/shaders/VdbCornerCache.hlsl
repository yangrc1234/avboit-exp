// SPDX-License-Identifier: MIT
#define SMOKE_MODE 4
#include "LocalVolume.hlsli"
#include "VolumeLighting.hlsli"

RWTexture3D<float4> CornerLight : register(u0);
cbuffer BrickDraw : register(b4) { uint ActorVertex; };

[numthreads(4, 4, 4)]
void main(uint3 corner : SV_DispatchThreadID)
{
    if (any(corner > VdbGrid) || VolumeLights[4].y <= 0.)
        return;

    // A corner is required by up to eight incident bricks, including apron-only bricks.
    bool used = false;
    [unroll] for (uint i = 0; i < 8; ++i)
    {
        uint3 cell = corner - uint3(i & 1, (i >> 1) & 1, i >> 2);
        if (all(cell < VdbGrid))
            used |= VdbEntry(cell.x + VdbGrid.x * (cell.y + VdbGrid.y * cell.z)) != 0;
    }
    if (!used)
        return;

    VolumeVaryings actor = LoadVolumeActor(ActorVertex);
    float3 texel = select(corner == 0, 0., min(float3(corner * 4) + .5, float3(VdbSize)));
    float3 local = (texel / VdbSize * 2. - 1.) * actor.extent;
    float3 world = actor.center + local.x * actor.axisX + local.y * actor.axisY +
                   local.z * cross(actor.axisX, actor.axisY);
    CornerLight[corner] = float4(EvaluateVolumeLighting(world, SampleVdbAmbientVisibility(texel / VdbSize, VdbExtinctionLength(actor.sigma.r, actor.extent))), 0.);
}
