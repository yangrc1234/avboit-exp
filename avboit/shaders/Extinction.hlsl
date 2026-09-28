// SPDX-License-Identifier: MIT
// Quantized extinction storage used by the native renderer.
// Layout: scalar packs four consecutive Z slices into a uint; RGB packs three
// 8-bit payloads into 10-bit fields. First-overflow depths bound integration.
// Compute event submission is a validation adapter. The raster pixel shader
// will call the same SplatExtinction function with material extinction.
#define SLICE_COUNT 128
#define RAY_COUNT 16
#define SCALAR_WORDS (SLICE_COUNT / 4 * RAY_COUNT)
Buffer<float4> Events : register(t0); // two float4 per event; header count in [0].x
RWBuffer<uint> Extinction : register(u0);
RWBuffer<uint> FirstOverflow : register(u1); // scalar,R,G,B per ray; clear to 128
RWBuffer<float4> Transmittance : register(u2);

#include "PackedExtinction.hlsli"

[numthreads(64, 1, 1)] void splat(uint eventIndex : SV_DispatchThreadID)
{
    if (eventIndex >= uint(Events[0].x))
        return;
    float4 event = Events[1 + eventIndex * 2], tau = Events[2 + eventIndex * 2];
    SplatExtinction(uint(event.x), event.y, tau.rgb, event.z != 0.);
}

    [numthreads(64, 1, 1)] void integrate(uint ray : SV_DispatchThreadID)
{
    if (ray >= RAY_COUNT)
        return;
    float3 integral = 0;
    for (uint slice = 0; slice < SLICE_COUNT; ++slice)
    {
        uint scalar = (Extinction[ray * 32 + slice / 4] >> ((slice % 4) * 8)) & 255u;
        uint rgb = Extinction[SCALAR_WORDS + ray * SLICE_COUNT + slice];
        uint3 color = uint3(rgb & 1023u, (rgb >> 10) & 1023u, (rgb >> 20) & 1023u);
        integral += DecodeOpticalDepth(float(scalar) + float3(color), ExtinctionTauMax, 255u);
        float3 transmission = OpticalDepthToTransmittance(integral);
        if (slice >= FirstOverflow[ray * 4])
            transmission = 0.;
        if (slice >= FirstOverflow[ray * 4 + 1])
            transmission.r = 0.;
        if (slice >= FirstOverflow[ray * 4 + 2])
            transmission.g = 0.;
        if (slice >= FirstOverflow[ray * 4 + 3])
            transmission.b = 0.;
        Transmittance[ray * SLICE_COUNT + slice] = float4(transmission, 1);
    }
}
