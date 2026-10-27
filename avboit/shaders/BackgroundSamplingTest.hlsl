// SPDX-License-Identifier: MIT
// Independent full-image oracle for the production cache sampling helper.
Buffer<float4> Camera : register(t15);
Texture2D<float4> Opaque : register(t3);
SamplerState LinearClamp : register(s0);
#include "BackgroundSampling.hlsli"
Texture2D<float4> Reference : register(t0);
RWTexture2D<float4> OpaqueOut : register(u0);
RWTexture2D<float4> ReferenceOut : register(u1);
RWTexture2D<float4> CacheOut : register(u2);
RWTexture2D<uint> TilesOut : register(u3);
RWBuffer<uint> Failures : register(u4);
static const int4 Geometry[3] = {int4(63, 64, 128, 126), int4(257, 129, 319, 191), int4(576, 300, 644, 360)};
bool Occupied(int2 tile)
{
    int2 low = tile * 64, high = low + 64;
    for (uint i = 0; i < 3; ++i)
        if (all(high > Geometry[i].xy) && all(low < Geometry[i].zw))
            return true;
    return false;
}
[numthreads(8, 8, 1)] void initialize(uint2 p : SV_DispatchThreadID)
{
    if (any(p >= uint2(Camera[6].xy)))
        return;
    float4 color = float4((p.x % 17) * 64., (p.y % 31) * 32., (p.x + p.y) % 71, 1);
    OpaqueOut[p] = color;
    for (uint i = 0; i < 3; ++i)
        if (all(int2(p) >= Geometry[i].xy) && all(int2(p) < Geometry[i].zw))
            color = ((p.x + p.y) % 5) == 0 ? 0 : float4(2048, 3, 64, 1);
    // Both the dense oracle and partial cache explicitly define invalid texels.
    ReferenceOut[p] = Occupied(int2(p / 64)) ? color : 0;
    CacheOut[p] = Occupied(int2(p / 64)) ? color : 0;
    if (all(p < uint2(ceil(Camera[6].xy / 64.))))
        TilesOut[p] = Occupied(p) ? 4 : 0;
}[numthreads(8, 8, 1)] void compare(uint2 p : SV_DispatchThreadID)
{
    float2 size = Camera[6].xy;
    if (any(p >= uint2(size)))
        return;
    for (uint i = 0; i < 16; ++i)
    {
        // Includes exact texel/tile boundaries, half taps and far outside screen.
        float2 jitter = float2((i % 4) * .25 - .5, (i / 4) * .25 - .5);
        float2 uv = (float2(p) + jitter) / size;
        if (i == 15)
            uv = uv * 4. - 1.5;
        float4 actual = SampleBackgroundCache(uv);
        float2 clampedUV = clamp(uv, .5 / size, 1. - .5 / size);
        uint2 center = min(uint2(clampedUV * size), uint2(size) - 1);
        float4 expected = Occupied(int2(center / 64)) ? Reference.SampleLevel(LinearClamp, clampedUV, 0)
                                                      : Opaque.SampleLevel(LinearClamp, clampedUV, 0);
        if (!all(isfinite(actual)) || any(abs(actual - expected) > .0001))
            InterlockedAdd(Failures[0], 1);
    }
}
