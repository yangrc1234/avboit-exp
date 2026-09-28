// SPDX-License-Identifier: MIT
// Fixed-screen base filter followed by fused separable Gaussian reductions.
// Each descriptor exposes one mip; horizontal results remain in groupshared.
Texture2D<float4> Source : register(t0);
RWTexture2D<float4> Destination : register(u0);
#include "TileWork.hlsli"
SamplerState LinearClamp : register(s0);
Buffer<float4> Camera : register(t15);
Texture2D<float4> Opaque : register(t3);
#include "BackgroundSampling.hlsli"
float4 Pair(float2 uv, float2 offset)
{
    float4 a = Source.SampleLevel(LinearClamp, uv + offset, 0);
    float4 b = Source.SampleLevel(LinearClamp, uv - offset, 0);
    // Continuous validity remains premultiplied. Only empty taps are replaced;
    // partial support is not a hole. Both empty -> still invalid.
    if ((uint(Camera[7].w) & 4u) != 0)
    {
        if (a.a <= 1e-5)
            a = b;
        else if (b.a <= 1e-5)
            b = a;
    }
    return a + b;
}

float4 SampleBackground(float2 uv)
{
    return SampleBackgroundCache(uv);
}
#include "FrostBaseKernel.hlsli"
// Directly resample any source resolution to the fixed-screen first level.
[numthreads(4, 4, 1)] void sparseBase(uint2 group : SV_GroupID, uint2 local : SV_GroupThreadID)
{
    uint w, h;
    Destination.GetDimensions(w, h);
    uint2 origin;
    if (!TileOrigin(group, uint2(w, h), origin))
        return;
    uint2 pixel = origin + local;
    if (any(pixel >= uint2(w, h)))
        return;
    Destination[pixel] = FilterBackgroundBase((pixel + .5) / float2(w, h));
}

// Fuse one horizontal+vertical reduction, preserving the intermediate FP16
// rounding and the per-axis mirror rule. Every reduction is exactly 2:1.
// Four output rows need 14 source rows including halo; no cross-group dependency.
groupshared float4 Horizontal[14 * 4];
float4 CachedTap(uint x, float y, int firstRow)
{
    int row = int(floor(y));
    return lerp(Horizontal[(row - firstRow) * 4 + x], Horizontal[(row + 1 - firstRow) * 4 + x], frac(y));
}
float4 CachedPair(uint x, float center, float offset, int firstRow)
{
    float4 a = CachedTap(x, center + offset, firstRow);
    float4 b = CachedTap(x, center - offset, firstRow);
    if ((uint(Camera[7].w) & 4u) != 0)
    {
        if (a.a <= 1e-5)
            a = b;
        else if (b.a <= 1e-5)
            b = a;
    }
    return a + b;
}
[numthreads(4, 4, 1)] void fused(uint2 group : SV_GroupID, uint2 local : SV_GroupThreadID)
{
    uint w, h, sw, sh;
    Destination.GetDimensions(w, h);
    Source.GetDimensions(sw, sh);
    uint2 origin;
    if (!TileOrigin(group, uint2(w, h), origin))
        return;
    int firstRow = int(origin.y * 2) - 3;
    uint rowCount = 2 * min(4u, h - origin.y) + 6;
    for (uint index = local.y * 4 + local.x; index < rowCount * 4; index += 16)
    {
        uint x = index % 4;
        int y = firstRow + int(index / 4);
        // Inactive edge lanes must not read unproduced source pixels. They
        // still participate in the shared-memory barrier below.
        if (origin.x + x >= w)
        {
            Horizontal[index] = 0;
            continue;
        }
        float2 uv = float2((min(origin.x + x, w - 1) + .5) / w, (clamp(y, 0, int(sh) - 1) + .5) / sh);
        float4 value = Pair(uv, float2(.8906824581564393 / sw, 0)) * .4156442352127602 +
                       Pair(uv, float2(2.708608527326045 / sw, 0)) * .08435576478723977;
        Horizontal[index] = f16tof32(f32tof16(value));
    }
    GroupMemoryBarrierWithGroupSync();
    uint2 pixel = origin + local;
    if (any(pixel >= uint2(w, h)))
        return;
    float center = pixel.y * 2. + .5;
    Destination[pixel] = CachedPair(local.x, center, .8906824581564393, firstRow) * .4156442352127602 +
                         CachedPair(local.x, center, 2.708608527326045, firstRow) * .08435576478723977;
}
