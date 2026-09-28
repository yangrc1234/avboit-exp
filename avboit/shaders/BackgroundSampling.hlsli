// SPDX-License-Identifier: MIT
#ifndef AVBOIT_BACKGROUND_SAMPLING
#define AVBOIT_BACKGROUND_SAMPLING
Texture2D<float4> CachedBackground : register(t27);
Texture2D<uint> BackgroundTiles : register(t30);

// Occupancy encloses geometry plus one pixel. Cached tiles have another
// one-pixel write border. Thus one center-tile test suffices for bilinear:
// inside, every cache texel exists; outside, every texel equals opaque.
// Callers supply Opaque, Camera and LinearClamp. Only mip 0 is sampled here.
float4 SampleBackgroundCache(float2 uv)
{
    float2 size = Camera[6].xy;
    uv = clamp(uv, .5 / size, 1. - .5 / size);
    uint2 p = min(uint2(uv * size), uint2(size) - 1);
    [branch] if ((BackgroundTiles.Load(int3(p / 64, 0)) & 4u) != 0) return CachedBackground.SampleLevel(LinearClamp, uv,
                                                                                                        0);
    return float4(Opaque.SampleLevel(LinearClamp, uv, 0).rgb, 1);
}
#endif
