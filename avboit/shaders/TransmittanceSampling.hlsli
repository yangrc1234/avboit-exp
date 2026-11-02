// SPDX-License-Identifier: MIT
#ifndef AVBOIT_TRANSMITTANCE_SAMPLING
#define AVBOIT_TRANSMITTANCE_SAMPLING
#include "DepthMapping.hlsli"
// Requires the caller's DepthWarp / TransmittanceLut bindings. Positive cubic
// B-spline weights preserve [0,1] and monotonicity along Z. XY reconstruction
// uses four hardware trilinear taps; Z interpolation and surface bias are unchanged.
float3 SampleTransmittanceAtZ(float2 uv, float z)
{
    [branch] if (!UseCubicTransmittance()) return TransmittanceLut.SampleLevel(LinearClamp, float3(uv, z), 0).rgb;
    float2 size = float2(VOLUME_WIDTH, VOLUME_HEIGHT), p = uv * size - .5;
    float2 b = floor(p), f = frac(p);
    float2 w0 = (1. - f) * (1. - f) * (1. - f) / 6.;
    float2 w1 = (3. * f * f * f - 6. * f * f + 4.) / 6.;
    float2 w2 = (-3. * f * f * f + 3. * f * f + 3. * f + 1.) / 6.;
    float2 w3 = f * f * f / 6.;
    float2 g0 = w0 + w1, g1 = w2 + w3;
    float2 p0 = (b - .5 + w1 / g0) / size, p1 = (b + 1.5 + w3 / g1) / size;
    return lerp(lerp(TransmittanceLut.SampleLevel(LinearClamp, float3(p0, z), 0).rgb,
                     TransmittanceLut.SampleLevel(LinearClamp, float3(p1.x, p0.y, z), 0).rgb, g1.x),
                lerp(TransmittanceLut.SampleLevel(LinearClamp, float3(p0.x, p1.y, z), 0).rgb,
                     TransmittanceLut.SampleLevel(LinearClamp, float3(p1, z), 0).rgb, g1.x),
                g1.y);
}
float3 SampleSurfaceTransmittance(float2 uv, float viewDepth, float offsetBins)
{
    float mapped = MapAdaptiveDepth(DepthWarp, viewDepth, 80., 1., 2.) - offsetBins;
    return mapped < 0 ? float3(1, 1, 1) : SampleTransmittanceAtZ(uv, (mapped + .5) / float(SLICE_COUNT));
}
float3 SampleSurfaceTransmittance(float2 uv, float viewDepth)
{
    return SampleSurfaceTransmittance(uv, viewDepth, 0.);
}
#endif
