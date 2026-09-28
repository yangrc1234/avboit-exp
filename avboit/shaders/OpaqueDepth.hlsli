// SPDX-License-Identifier: MIT
#ifndef AVBOIT_OPAQUE_DEPTH
#define AVBOIT_OPAQUE_DEPTH
Texture2D<float> OpaqueHardwareDepth : register(t24);
// Host SceneDepth: real opaque depth before zero-T drawing, then
// min(opaque, zero-T quads). Frost and VFX respect both kinds of occluder.

// Camera[4].w selects the host's depth source. The legacy reference stores
// positive view-space Z in color alpha. Native hosts can supply D32 directly;
// Camera[5].xy are projection coefficients where deviceDepth = A + B / viewZ.
// This also supports reverse Z by changing A/B; no full-screen packing pass.
float LoadOpaqueViewDepth(uint2 pixel)
{
    if (Camera[4].w == 0)
        return Opaque.Load(int3(pixel, 0)).a;
    float deviceDepth = OpaqueHardwareDepth.Load(int3(pixel, 0));
    return Camera[5].y / (deviceDepth - Camera[5].x);
}
#endif
