// SPDX-License-Identifier: MIT
#include "TransparencyCommon.hlsli"
#include "VolumeBlueNoise.hlsli"
Texture2D<float3> LowN : register(t4);
Texture2D<float3> LowA : register(t5);
Texture2D<float3> LowTau : register(t6);
Texture2D<float3> LowBackN : register(t9);
Texture2D<float> InterfaceDepth : register(t7);
Texture2D<float4> Opaque : register(t3);
#include "OpaqueDepth.hlsli"
float4 upsampleVertex(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}
struct Accumulation
{
    float3 n : SV_Target0;
    float3 a : SV_Target1;
    float3 tau : SV_Target2;
    float3 backN : SV_Target3;
};
float GuideError(float depth, float reference)
{
    return abs(depth - reference) / max(.02, reference * .02);
}
Accumulation upsamplePixel(float4 position : SV_Position)
{
    uint2 pixel = uint2(position.xy);
    float opaque = LoadOpaqueViewDepth(pixel);
    float interfaceZ = InterfaceDepth.Load(int3(pixel, 0));
    // Low rays use the bottom-right full-resolution texel of each 2x2 block.
    float2 location = (position.xy - 1.5) * .5;
    if (Camera[9].w > 0.)
    {
        uint2 p = pixel & 31u;
        uint2 q = (pixel + uint2(13, 7)) & 31u;
        float2 noise = (float2(VolumeBlueNoise[p.y * 32 + p.x], VolumeBlueNoise[q.y * 32 + q.x]) + .5) / 1024.;
        noise = frac(noise + (Camera[9].w - 1.) * float2(.754877666, .569840296));
        location += noise - .5;
    }
    int2 base = int2(floor(location));
    float2 f = frac(location);
    float weights[4];
    int2 taps[4];
    float sum = 0., bestError = 1e30;
    uint best = 0;
    [unroll] for (uint j = 0; j < 4; ++j)
    {
        int2 offset = int2(j & 1, j >> 1);
        taps[j] = clamp(base + offset, 0, int2(WIDTH / 2 - 1, HEIGHT / 2 - 1));
        int2 guide = taps[j] * 2 + 1;
        float z = LoadOpaqueViewDepth(guide);
        float iz = InterfaceDepth.Load(int3(guide, 0));
        float error = GuideError(z, opaque);
        error += ((iz > 0.) != (interfaceZ > 0.))
                     ? 1000.
                     : (interfaceZ > 0. ? GuideError(ViewDepthFromDevice(iz), ViewDepthFromDevice(interfaceZ)) : 0.);
        float spatial = (offset.x ? f.x : 1. - f.x) * (offset.y ? f.y : 1. - f.y);
        weights[j] = spatial * exp2(-error * error);
        sum += weights[j];
        if (error < bestError)
        {
            bestError = error;
            best = j;
        }
    }
    Accumulation o = (Accumulation)0;
    [unroll] for (uint k = 0; k < 4; ++k)
    {
        float w = sum > 1e-6 ? weights[k] / sum : (k == best ? 1. : 0.);
        o.n += LowN.Load(int3(taps[k], 0)) * w;
        o.a += LowA.Load(int3(taps[k], 0)) * w;
        o.tau += LowTau.Load(int3(taps[k], 0)) * w;
        o.backN += LowBackN.Load(int3(taps[k], 0)) * w;
    }
    return o;
}
