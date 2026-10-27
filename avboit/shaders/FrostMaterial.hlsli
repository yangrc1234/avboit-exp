// SPDX-License-Identifier: MIT
#ifndef AVBOIT_FROST_MATERIAL
#define AVBOIT_FROST_MATERIAL
// Linear tangent-space normal XY, DirectX (+Y down) convention. The demo uses
// these two components as an artistic screen-space offset, not a Snell solve.
Texture2D<float2> FrostNormal : register(t25);
SamplerState FrostWrap : register(s1);
float2 FrostMaterialOffset(float2 local, float2 localDx, float2 localDy)
{
    // Local [-1, 1] spans one pane. Camera[9].z counts full texture repeats.
    float tiling = Camera[9].z;
    float2 uv = (local * .5 + .5) * tiling;
    return FrostNormal.SampleGrad(FrostWrap, uv, localDx * (.5 * tiling), localDy * (.5 * tiling)) * 2. - 1.;
}
#endif
