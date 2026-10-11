// SPDX-License-Identifier: MIT
#ifndef AVBOIT_DEVICE_DEPTH
#define AVBOIT_DEVICE_DEPTH
// Finite Reversed-Z: near=1, far/clear=0. Camera[5] is (A, B, near, far),
// where deviceDepth = A + B / viewZ, A < 0 and B > 0.
float ViewDepthFromDevice(float deviceDepth)
{
    return Camera[5].y / (deviceDepth - Camera[5].x);
}
float DeviceDepthFromView(float viewDepth)
{
    return Camera[5].x + Camera[5].y / viewDepth;
}
float ClipDepthFromView(float viewDepth)
{
    return Camera[5].x * viewDepth + Camera[5].y;
}
#endif
