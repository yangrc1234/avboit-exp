// SPDX-License-Identifier: MIT
#ifndef AVBOIT_FROST_DISTORTION
#define AVBOIT_FROST_DISTORTION
// Camera[9].y is the post-blur radius in 1440p reference pixels. Both resolves
// share the existing RG16F offset. Pyramid edges use premultiplied validity.
float FrostDetailRadius()
{
    return Camera[9].y * Camera[6].y / 1440.;
}
float2 FrostDetailOffset(float2 offset, float sigma)
{
    return sigma > 0 ? offset * min(1., FrostDetailRadius() / max(length(offset), .00001)) : float2(0, 0);
}
#endif
