// SPDX-License-Identifier: MIT
#include "LocalVolume.hlsli"
Buffer<uint> DepthWarp : register(t1);
Texture2D<float4> Opaque : register(t3);
#include "OpaqueDepth.hlsli"
RWBuffer<uint> Extinction : register(u0);
RWBuffer<uint> FirstOverflow : register(u1);
#include "PackedExtinction.hlsli"
void InjectVolume(VolumeVaryings i, bool textured)
{
    uint2 pixel = uint2(i.position.xy);
    float2 uv = (float2(pixel) + .5) / float2(VOLUME_WIDTH, VOLUME_HEIGHT);
    float2 interval;
    float rayZ;
    if (!VolumeInterval(i, uv, LoadOpaqueViewDepth(uint2(uv * float2(WIDTH, HEIGHT))), interval, rayZ))
        return;
    uint count = DepthWarp[0], ray = pixel.y * VOLUME_WIDTH + pixel.x;
    uint2 range = VolumeVirtualRange(interval, count);
    // Quantize cumulative tau and difference adjacent endpoints. Rounding every
    // tiny segment independently would erase low-density media at high Z budgets.
    LocalOpticalDepth sigma = LocalExtinction(i.sigma);
    LocalOpticalDepth cumulative = 0;
    LocalExtinctionCode previous = 0;
    float emptyUntil = -1.;
    for (uint cell = range.x; cell < range.y; ++cell)
    {
        float2 segment = VolumeSegment(cell, count, interval);
        float sampleZ = (segment.x + segment.y) * .5;
        if (textured && sampleZ >= emptyUntil)
            emptyUntil = sampleZ + VolumeEmptyRun(i, uv, sampleZ, rayZ);
        if (textured && sampleZ < emptyUntil)
            continue;
        float density = textured ? SampleVolumeData(i, uv, (segment.x + segment.y) * .5, rayZ).r : 1.;
        cumulative += sigma * density * max(segment.y - segment.x, 0.) / rayZ;
        LocalExtinctionCode code = (LocalExtinctionCode)(floor(cumulative * (255. / ExtinctionTauMax) + .5));
        uint entry = DepthWarp[2 + cell], slice = entry >> 3;
        if ((entry & 1u) && slice < SLICE_COUNT)
            AddRgb(ray, slice, min(code - previous, 255u));
        previous = code;
    }
}

void volumeExtinction(VolumeVaryings i)
{
    InjectVolume(i, false);
}
void texturedExtinction(VolumeVaryings i)
{
    InjectVolume(i, true);
}
