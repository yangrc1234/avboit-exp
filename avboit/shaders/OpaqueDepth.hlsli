// SPDX-License-Identifier: MIT
#ifndef AVBOIT_OPAQUE_DEPTH
#define AVBOIT_OPAQUE_DEPTH
#include "DeviceDepth.hlsli"
Texture2D<float> OpaqueHardwareDepth : register(t24);
// Host SceneDepth: real opaque depth before zero-T drawing, then
// max(opaque, zero-T quads). Frost and VFX respect both kinds of occluder.

// Camera[4].w selects the host's depth source. The legacy reference stores
// positive view-space Z in color alpha. Native hosts can supply D32 directly;
// Reversed-Z reconstruction needs no full-screen packing pass.
float LoadOpaqueViewDepth(uint2 pixel)
{
    if (Camera[4].w == 0)
        return Opaque.Load(int3(pixel, 0)).a;
    float deviceDepth = OpaqueHardwareDepth.Load(int3(pixel, 0));
    return ViewDepthFromDevice(deviceDepth);
}
#endif
