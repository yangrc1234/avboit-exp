// SPDX-License-Identifier: MIT
#ifndef AVBOIT_INTERFACE_DEPTH
#define AVBOIT_INTERFACE_DEPTH
Texture2D<float> InterfaceDepth : register(t7);
float4 LoadInterface(uint2 pixel)
{
    float d = InterfaceDepth.Load(int3(pixel, 0));
    return d < 1. ? float4(Camera[5].y / (d - Camera[5].x), 0, 0, 1) : float4(0, 0, 0, 0);
}
// View-Z reconstruction amplifies D32 rounding with distance. Exclude the
// selected surface itself while preserving the previous near-field tolerance.
float InterfaceTolerance(float z)
{
    return max(.00003, z * z * 1.5e-6);
}
// Compare rasterized device depths directly for OIT classification. Rebuilding
// view Z here can put the selected surface behind itself after D32 rounding.
bool BehindInterface(uint2 pixel, float deviceDepth)
{
    float d = InterfaceDepth.Load(int3(pixel, 0));
    float delta = d - Camera[5].x;
    float bias = max(.00003 * delta * delta / abs(Camera[5].y), 6e-8);
    return d < 1. && deviceDepth > d + bias;
}
#endif
