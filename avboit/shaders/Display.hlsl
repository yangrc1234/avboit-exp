// SPDX-License-Identifier: MIT
#include "TransparencyCommon.hlsli"
#include "OccupancyOverlay.hlsli"
Texture2D<float4> ComposedHdr : register(t21);
float4 displayPixel(Fullscreen i) : SV_Target
{
    float3 color = ComposedHdr.SampleLevel(LinearClamp, i.uv, 0).rgb;
    color = pow(max(color, 0.) / (1. + max(color, 0.)), 1. / 2.2);
    return float4(OccupancyOverlay(i.uv, color), 1);
}
