// SPDX-License-Identifier: MIT
#include "LocalVolume.hlsli"
#include "ResolveData.hlsli"
#include "VolumeLighting.hlsli"
struct VolumeAccumulation
{
    float3 numerator : SV_Target0;
    float3 denominator : SV_Target1;
    float3 totalTau : SV_Target2;
    float3 backNumerator : SV_Target3;
};
void AccumulateVolumeSegment(float2 segment, uint cell, uint count, float rayZ, float3 entryT, float3 exitT,
                             VolumeVaryings i, bool behind, bool textured, float2 uv, inout VolumeAccumulation o)
{
    if (segment.y <= segment.x)
        return;
    float3 tau = i.sigma * max(segment.y - segment.x, 0.) / rayZ;
    float middle = (segment.x + segment.y) * .5;
    if (textured)
    {
        float2 data = SampleVolumeData(i, uv, middle, rayZ);
        tau *= data.r;
        if (VolumeLights[4].y == 0.)
            i.source *= .18 + .82 * exp(-i.sigma * data.g * (2. * i.extent.y));
    }
    if (!any(tau > 0.))
        return;
    if (VolumeLights[4].y > 0.)
    {
        float3 world = Camera[0].xyz + WorldRay(uv) * (middle / rayZ);
        i.source *= EvaluateVolumeLighting(world);
    }
    float fraction = saturate(NormalizedLogDepth(middle, 80., 1.) * count - cell);
    float3 weight = tau * lerp(entryT, exitT, fraction);
    // Differential weights are additive under actor partition. AVBOIT's common
    // (1-exp(-totalTau))/denominator normalization supplies integrated opacity.
    float3 radiance = i.source * weight;
    o.numerator += radiance;
    o.denominator += weight;
    o.totalTau += tau;
    if (behind)
        o.backNumerator += radiance;
}
VolumeAccumulation ShadeVolume(VolumeVaryings i, bool textured)
{
    VolumeAccumulation o = (VolumeAccumulation)0;
    uint2 pixel = uint2(i.position.xy);
    float2 uv = i.position.xy / float2(WIDTH, HEIGHT), interval;
    float rayZ;
    if (!VolumeInterval(i, uv, LoadOpaqueViewDepth(pixel), interval, rayZ))
        return o;
    uint count = DepthWarp[0];
    uint2 range = VolumeVirtualRange(interval, count);
    float interfaceDevice = InterfaceDepth.Load(int3(pixel, 0));
    float interfaceZ = interfaceDevice > 0 ? ViewDepthFromDevice(interfaceDevice) : Camera[5].w;
    for (uint cell = range.x; cell < range.y; ++cell)
    {
        uint slice = DepthWarp[2 + cell * 2];
        if (!(DepthWarp[3 + cell * 2] & 1u) || slice >= SLICE_COUNT)
            continue;
        // Integrated texel k is the EXIT of bin k. Interpolate from the preceding
        // endpoint, without the thin-surface two-slice push/bias.
        float3 exitT = TransmittanceLut.SampleLevel(LinearClamp, float3(uv, (slice + .5) / float(SLICE_COUNT)), 0).rgb;
        float3 entryT =
            slice == 0
                ? float3(1, 1, 1)
                : TransmittanceLut.SampleLevel(LinearClamp, float3(uv, (slice - .5) / float(SLICE_COUNT)), 0).rgb;
        float2 segment = VolumeSegment(cell, count, interval);
        float split = clamp(interfaceZ, segment.x, segment.y);
        AccumulateVolumeSegment(float2(segment.x, split), cell, count, rayZ, entryT, exitT, i, false, textured, uv, o);
        AccumulateVolumeSegment(float2(split, segment.y), cell, count, rayZ, entryT, exitT, i, true, textured, uv, o);
    }
    return o;
}

[earlydepthstencil] VolumeAccumulation volumeAccumulation(VolumeVaryings i)
{ return ShadeVolume(i, false); }[earlydepthstencil] VolumeAccumulation texturedAccumulation(VolumeVaryings i)
{
    return ShadeVolume(i, true);
}

// Match guide texels used by the bilateral reconstruction. Full-resolution
// depth is sampled explicitly; no mismatched full-size DSV is bound here.
VolumeAccumulation volumeAccumulationHalf(VolumeVaryings i)
{
    i.position.xy = floor(i.position.xy) * 2. + 1.5;
    return ShadeVolume(i, false);
}
VolumeAccumulation texturedAccumulationHalf(VolumeVaryings i)
{
    i.position.xy = floor(i.position.xy) * 2. + 1.5;
    return ShadeVolume(i, true);
}
