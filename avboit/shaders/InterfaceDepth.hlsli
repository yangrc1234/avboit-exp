// SPDX-License-Identifier: MIT
#ifndef AVBOIT_INTERFACE_DEPTH
#define AVBOIT_INTERFACE_DEPTH
#include "DeviceDepth.hlsli"
Texture2D<float> InterfaceDepth : register(t7);
float4 LoadInterface(uint2 pixel)
{
    float d = InterfaceDepth.Load(int3(pixel, 0));
    return d > 0. ? float4(ViewDepthFromDevice(d), 0, 0, 1) : float4(0, 0, 0, 0);
}
// Allow view-Z reconstruction/rasterization roundoff when excluding the
// selected surface itself. Reversed-Z keeps relative precision with distance.
float InterfaceTolerance(float z)
{
    return max(.00003, z * 1e-6);
}
// Compare rasterized device depths directly for OIT classification. Rebuilding
// view Z here can put the selected surface behind itself after D32 rounding.
bool BehindInterface(uint2 pixel, float deviceDepth)
{
    float d = InterfaceDepth.Load(int3(pixel, 0));
    float delta = d - Camera[5].x;
    float bias = max(.00003 * delta * delta / Camera[5].y, max(d, -Camera[5].x) * 1e-6);
    return d > 0. && deviceDepth < d - bias;
}
#endif
