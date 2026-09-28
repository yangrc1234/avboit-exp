// SPDX-License-Identifier: MIT
#ifndef AVBOIT_DEPTH_MAPPING_HLSLI
#define AVBOIT_DEPTH_MAPPING_HLSLI
// Shared by extinction splat and full-resolution transmittance sampling.
// Warp uses the layout emitted by AdaptiveDepth.hlsl. An empty interval maps
// to the preceding occupied endpoint, so interpolation never crosses a gap.
float MapAdaptiveDepth(Buffer<uint> warp, float viewDepth, float farDepth, float linearizationDistance,
                       float biasInVirtualSlices)
{
    uint count = warp[0];
    float virtualDepth = log2(1. + max(viewDepth, 0.) / linearizationDistance) /
                         log2(1. + farDepth / linearizationDistance) * float(count);
    virtualDepth -= biasInVirtualSlices;
    if (virtualDepth < 0.)
        return -1.; // In front of first slice: transmission = 1.
    virtualDepth = min(virtualDepth, float(count) - 0.0001);
    uint slice = uint(virtualDepth);
    uint prefix = warp[2 + slice * 2];
    bool occupied = (warp[3 + slice * 2] & 1u) != 0;
    return float(prefix) + (occupied ? frac(virtualDepth) : 0.);
}
#endif
