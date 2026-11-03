// SPDX-License-Identifier: MIT
#ifndef AVBOIT_TRANSMITTANCE_SAMPLING
#define AVBOIT_TRANSMITTANCE_SAMPLING
#include "DepthMapping.hlsli"
// Requires the caller's DepthWarp / TransmittanceLut bindings.
float3 SampleTransmittanceAtZ(float2 uv, float z)
{
    return TransmittanceLut.SampleLevel(LinearClamp, float3(uv, z), 0).rgb;
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
