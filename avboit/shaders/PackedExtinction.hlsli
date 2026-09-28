// SPDX-License-Identifier: MIT
#include "OpticalDepth.hlsli"
// Shared packed atomic implementation for raster and compute validation.
uint QuantizeExtinction(float tau)
{
    return QuantizeOpticalDepth(tau, ExtinctionTauMax, 255u, true);
}

void AddScalar(uint ray, uint slice, uint contribution)
{
    if (contribution == 0)
        return;
    uint shift = (slice % 4) * 8, previous;
    InterlockedAdd(Extinction[ray * (SLICE_COUNT / 4) + slice / 4], contribution << shift, previous);
    if (((previous >> shift) & 255u) + contribution > 255u)
        InterlockedMin(FirstOverflow[ray * 4], slice);
}

void AddRgb(uint ray, uint slice, uint3 contribution)
{
    if (all(contribution == 0))
        return;
    uint packed = contribution.r | (contribution.g << 10) | (contribution.b << 20), previous;
    InterlockedAdd(Extinction[SCALAR_WORDS + ray * SLICE_COUNT + slice], packed, previous);
    // Overflow guard bits reduce cross-channel carry pollution. This is the
    // published approximation, not three independent saturating additions.
    uint3 accumulated = uint3(previous & 1023u, (previous >> 10) & 1023u, (previous >> 20) & 1023u) + contribution;
    if (accumulated.r > 255u)
        InterlockedMin(FirstOverflow[ray * 4 + 1], slice);
    if (accumulated.g > 255u)
        InterlockedMin(FirstOverflow[ray * 4 + 2], slice);
    if (accumulated.b > 255u)
        InterlockedMin(FirstOverflow[ray * 4 + 3], slice);
}

void SplatExtinction(uint ray, float mappedDepth, float3 tau, bool scalar)
{
    mappedDepth = clamp(mappedDepth, 0., float(SLICE_COUNT) - 1.0001);
    uint slice = uint(mappedDepth);
    float fraction = frac(mappedDepth);
    uint3 a = uint3(QuantizeExtinction(tau.r * (1. - fraction)), QuantizeExtinction(tau.g * (1. - fraction)),
                    QuantizeExtinction(tau.b * (1. - fraction)));
    uint3 b = uint3(QuantizeExtinction(tau.r * fraction), QuantizeExtinction(tau.g * fraction),
                    QuantizeExtinction(tau.b * fraction));
    if (scalar)
    {
        if (slice / 4 == (slice + 1) / 4)
        {
            // Both linear-Z taps fit one DWORD: one atomic, two payload bytes.
            uint shift = (slice % 4) * 8, previous;
            InterlockedAdd(Extinction[ray * (SLICE_COUNT / 4) + slice / 4], (a.r | (b.r << 8)) << shift, previous);
            if (((previous >> shift) & 255u) + a.r > 255u)
                InterlockedMin(FirstOverflow[ray * 4], slice);
            if (((previous >> (shift + 8)) & 255u) + b.r > 255u)
                InterlockedMin(FirstOverflow[ray * 4], slice + 1);
        }
        else
        {
            AddScalar(ray, slice, a.r);
            AddScalar(ray, slice + 1, b.r);
        }
    }
    else
    {
        AddRgb(ray, slice, a);
        AddRgb(ray, slice + 1, b);
    }
}
