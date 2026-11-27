// SPDX-License-Identifier: MIT
// Volume samples have no thin-surface depth push. LUT texel k stores bin k's
// exit, so the continuous coordinate starts at the preceding endpoint.
float3 SampleVolumeTransmittance(float2 uv, float viewZ)
{
    float mapped = MapAdaptiveDepth(DepthWarp, viewZ, 80., 1., 0.);
    if (mapped <= 0.)
        return 1.;
    if (mapped < 1.)
    {
        float3 first = SampleTransmittanceAtZ(uv, .5 / float(SLICE_COUNT));
        return lerp(float3(1, 1, 1), first, mapped);
    }
    return SampleTransmittanceAtZ(uv, (mapped - .5) / float(SLICE_COUNT));
}
