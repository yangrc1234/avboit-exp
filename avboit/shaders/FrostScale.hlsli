// SPDX-License-Identifier: MIT
#ifndef AVBOIT_FROST_SCALE
#define AVBOIT_FROST_SCALE
// Camera[8]: fixed-screen base width/height, level count, resolve flags (bit 0: no tile cull).
// Sigma uses pixels at a reference viewport height of 1440, never render pixels.
float FrostVariance(float referenceMip)
{
    return max(0., 9. + 2.12297680594 * (exp2(2. * referenceMip) - 16.) / 3.);
}
float FrostBaseLevel()
{
    return 6. - Camera[8].z;
}
float FrostBaseSigma()
{
    return sqrt(FrostVariance(FrostBaseLevel()));
}
float2 FrostReferenceSize()
{
    return float2(Camera[6].x / Camera[6].y, 1.) * 1440.;
}
#endif
