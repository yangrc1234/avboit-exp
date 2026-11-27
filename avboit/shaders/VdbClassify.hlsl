// SPDX-License-Identifier: MIT
#define SMOKE_MODE 4
#include "LocalVolume.hlsli"
#include "VdbProxyProjection.hlsli"
#include "ResolveData.hlsli"
#include "VolumeTransmittance.hlsli"
#include "VolumeLighting.hlsli"

cbuffer BrickDraw : register(b4) { uint ActorVertex; };
RWBuffer<uint> DrawArgs : register(u0);
RWBuffer<uint4> BrickList : register(u1);

[numthreads(64, 1, 1)]
void main(uint brick : SV_DispatchThreadID)
{
    uint count = VdbGrid.x * VdbGrid.y * VdbGrid.z;
    if (brick >= count)
        return;
    uint entry = VdbEntry(brick);
    if (!entry)
        return;
    VolumeVaryings actor = LoadVolumeActor(ActorVertex);
    uint3 cell = 4u * uint3(brick % VdbGrid.x, (brick / VdbGrid.x) % VdbGrid.y, brick / (VdbGrid.x * VdbGrid.y));
    float3 lo = select(cell == 0u, 0., float3(cell) + .5);
    float3 hi = min(float3(cell) + 4.5, float3(VdbSize));
    float3 view, radius;
    VdbViewBounds(actor, lo, hi, view, radius);
    float nearZ = view.z - radius.z, farZ = view.z + radius.z;
    if (farZ < Camera[5].z || nearZ > Camera[5].w)
        return;
    float2 tangent = .57735027 * float2(float(WIDTH) / HEIGHT, 1.);
    float2 margin = 1. + (2. * abs(Camera[11].xy) + 1.) / Camera[6].xy;
    if (any(abs(view.xy) - radius.xy > farZ * tangent * margin))
        return;

    bool nearPlane = nearZ <= Camera[5].z;
    // Keep near-plane bricks at full quality. Else, one front-depth query chooses the brick's LOD.
    float maxT = 1.;
    if (!nearPlane)
    {
        float2 uv = .5 + float2(.5, -.5) * view.xy / (view.z * tangent) + Camera[11].xy / Camera[6].xy;
        float3 T = SampleVolumeTransmittance(saturate(uv), nearZ);
        maxT = max(T.r, max(T.g, T.b));
        if (maxT == 0.)
            return;
    }
    bool singleStep = maxT <= Camera[14].x; // Host-selected boundary between the two draws.
    float3 light = 1.;
    if (singleStep && Camera[14].y == 0. && VolumeLights[4].y > 0.)
    {
        float3 uvw = (lo + hi) * .5 / VdbSize;
        float3 world = Camera[0].xyz + view.x * Camera[1].xyz + view.y * Camera[2].xyz + view.z * Camera[3].xyz;
        light = EvaluateVolumeLighting(world, SampleVdbAmbientVisibility(uvw, VdbExtinctionLength(actor.sigma.r, actor.extent)));
    }
    uint slot;
    InterlockedAdd(DrawArgs[singleStep ? 6 : 1], 1, slot);
    BrickList[singleStep ? count - 1 - slot : slot] = uint4(brick | (uint(nearPlane) << 31), entry,
        f32tof16(light.r) | (f32tof16(light.g) << 16), f32tof16(light.b));
}
