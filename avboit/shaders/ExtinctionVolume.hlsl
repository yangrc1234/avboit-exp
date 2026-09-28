// SPDX-License-Identifier: MIT
#include "TransparencyCommon.hlsli"
Buffer<uint> DepthWarp : register(t1);
Buffer<uint> PhysicalMasks : register(t22);
Texture2D<float4> Opaque : register(t3);
#include "OpaqueDepth.hlsli"
RWBuffer<uint> Extinction : register(u0);
RWBuffer<uint> FirstOverflow : register(u1);
RWTexture3D<float4> Integrated : register(u2);
RWTexture2D<uint> ZeroSlice : register(u3);
#include "PackedExtinction.hlsli"
#include "DepthMapping.hlsli"
void splatPixel(Varyings i)
{
    uint2 pixel = uint2(i.position.xy);
    float2 uv = (float2(pixel) + .5) / float2(VOLUME_WIDTH, VOLUME_HEIGHT);
    if (i.depth >= LoadOpaqueViewDepth(uint2(uv * float2(WIDTH, HEIGHT))))
        discard;
    float3 opacity = Opacity(i);
    if (all(opacity < .00001))
        discard;
    float mapped = MapAdaptiveDepth(DepthWarp, i.depth, 80., 1., 0.);
    SplatExtinction(pixel.y * VOLUME_WIDTH + pixel.x, mapped, TransmittanceToOpticalDepth(1. - opacity),
                    i.scalarExtinction != 0);
}
uint OccupancyTile(uint2 pixel)
{
    return (pixel.y / 8) * ((VOLUME_WIDTH + 7) / 8) + pixel.x / 8;
}

[numthreads(8, 8, 1)] void clearOccupiedExtinction(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= uint2(VOLUME_WIDTH, VOLUME_HEIGHT)))
        return;
    uint ray = pixel.y * VOLUME_WIDTH + pixel.x, tile = OccupancyTile(pixel);
    for (uint word = 0; word < SLICE_COUNT / 4; ++word)
    {
        uint bits = PhysicalMasks[tile * 8 + word / 8];
        // A scalar DWORD holds four slices. Clear the whole word if any of
        // its payloads may be touched, so integer carries cannot retain dirt.
        if (((bits >> ((word % 8) * 4)) & 15u) != 0)
            Extinction[ray * (SLICE_COUNT / 4) + word] = 0;
    }
    for (uint word = 0; word < 4; ++word)
    {
        uint bits = PhysicalMasks[tile * 8 + 4 + word];
        while (bits != 0)
        {
            uint bit = firstbitlow(bits);
            bits &= bits - 1;
            if (word * 32 + bit < SLICE_COUNT)
                Extinction[SCALAR_WORDS + ray * SLICE_COUNT + word * 32 + bit] = 0;
        }
    }
    // Small per-ray metadata is reset densely; consumers can use it directly.
    for (uint channel = 0; channel < 4; ++channel)
        FirstOverflow[ray * 4 + channel] = SLICE_COUNT;
}

void Integrate(uint2 pixel, bool sparse)
{
    if (any(pixel >= uint2(VOLUME_WIDTH, VOLUME_HEIGHT)))
        return;
    uint ray = pixel.y * VOLUME_WIDTH + pixel.x, tile = OccupancyTile(pixel);
    float3 integral = 0, t = 1;
    bool saturated = false;
    uint zeroSlice = SLICE_COUNT;
    uint4 scalarBits = 0xffffffffu, rgbBits = 0xffffffffu;
    if (sparse)
        for (uint word = 0; word < 4; ++word)
        {
            scalarBits[word] = PhysicalMasks[tile * 8 + word];
            rgbBits[word] = PhysicalMasks[tile * 8 + 4 + word];
        }
    // Sampling cannot address past the mapped endpoint. Keep one extra slice
    // for floating-point/trilinear footprint conservatism at that endpoint.
    uint limit = sparse ? min(uint(SLICE_COUNT), DepthWarp[1] + 2) : uint(SLICE_COUNT);
    uint4 overflowDepth = uint4(FirstOverflow[ray * 4], FirstOverflow[ray * 4 + 1], FirstOverflow[ray * 4 + 2],
                                FirstOverflow[ray * 4 + 3]);
    for (uint slice = 0; slice < limit; ++slice)
    {
        uint scalarMask = scalarBits[slice / 32];
        uint rgbMask = rgbBits[slice / 32];
        uint bit = 1u << (slice % 32);
        bool scalarActive = (scalarMask & bit) != 0, rgbActive = (rgbMask & bit) != 0;
        if (!saturated && (scalarActive || rgbActive))
        {
            uint scalar =
                scalarActive ? (Extinction[ray * (SLICE_COUNT / 4) + slice / 4] >> ((slice % 4) * 8)) & 255u : 0;
            uint packed = rgbActive ? Extinction[SCALAR_WORDS + ray * SLICE_COUNT + slice] : 0;
            uint3 color = uint3(packed & 1023u, (packed >> 10) & 1023u, (packed >> 20) & 1023u);
            integral += DecodeOpticalDepth(float(scalar) + float3(color), ExtinctionTauMax, 255u);
            t = OpticalDepthToTransmittance(integral);
        }
        if (slice >= overflowDepth.x)
            t = 0;
        if (slice >= overflowDepth.y)
            t.r = 0;
        if (slice >= overflowDepth.z)
            t.g = 0;
        if (slice >= overflowDepth.w)
            t.b = 0;
        // RGBA8 would round every component to zero. Remaining extinction is
        // nonnegative, so no later sample can become nonzero again.
        if (all(t < (.5 / 255.)))
        {
            zeroSlice = min(zeroSlice, slice);
            if (sparse)
            {
                t = 0;
                saturated = true;
            }
        }
        // Empty slices still receive the carried transmittance. Leaving them
        // unwritten would leak previous-frame data through linear filtering.
        Integrated[uint3(pixel, slice)] = float4(t, 1);
    }
    // Same threshold as quantized RGB LUT; sentinel means the ray never closed.
    ZeroSlice[pixel] = zeroSlice;
}
[numthreads(8, 8, 1)] void integrateLut(uint2 pixel : SV_DispatchThreadID)
{ Integrate(pixel, true); }[numthreads(8, 8, 1)] void integrateDenseLut(uint2 pixel : SV_DispatchThreadID)
{
    Integrate(pixel, false);
}
