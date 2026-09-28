// SPDX-License-Identifier: MIT
// First fixed-screen Gaussian filter of the resolved background texture.
#include "FrostScale.hlsli"
uint FrostPairCount()
{
    return Camera[4].z > 0 ? 8u : 32u;
}
// Offset is in UV; weight is per sample (each pair contributes two samples).
float2 FrostPairOffset(uint pair, out float weight)
{
    float2 sigma = FrostBaseSigma() / FrostReferenceSize();
    if (Camera[4].z > 0)
    {
        float radius = 1.022333333 * sqrt(-2. * log(1. - (pair + .5) / 8.));
        float angle = pair * 2.39996323;
        weight = 1. / 16.;
        return sigma * radius * float2(cos(angle), sin(angle));
    }
    static const float x[8] = {-4.14454719, -2.80248586, -1.63651904, -.53907981,
                               .53907981,   1.63651904,  2.80248586,  4.14454719};
    static const float w[8] = {.000112615, .009635220, .117239908, .373012257,
                               .373012257, .117239908, .009635220, .000112615};
    uint i = pair % 4, y = pair / 4;
    weight = w[i] * w[y];
    return sigma * float2(x[i], x[y]);
}

float4 FilterBackgroundBase(float2 uv)
{
    float4 sum = 0;
    [loop] for (uint pair = 0; pair < FrostPairCount(); ++pair)
    {
        float weight;
        float2 offset = FrostPairOffset(pair, weight);
        float4 a = SampleBackground(uv + offset), b = SampleBackground(uv - offset);
        if ((uint(Camera[7].w) & 4u) != 0)
        {
            if (a.a <= 1e-5)
                a = b;
            else if (b.a <= 1e-5)
                b = a;
        }
        sum += (a + b) * weight;
    }
    return sum;
}
