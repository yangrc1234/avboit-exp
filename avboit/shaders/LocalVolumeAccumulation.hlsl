// SPDX-License-Identifier: MIT
#include "LocalVolume.hlsli"
#include "ResolveData.hlsli"
#include "VolumeLighting.hlsli"
#include "VolumeBlueNoise.hlsli"
struct VolumeAccumulation
{
    float3 numerator : SV_Target0;
    float3 denominator : SV_Target1;
    float3 totalTau : SV_Target2;
    float3 backNumerator : SV_Target3;
};
// Volume samples have no thin-surface depth push. LUT texel k stores bin k's
// exit, so the continuous coordinate starts at the preceding endpoint.
float3 SampleVolumeTransmittance(float2 uv, float viewZ)
{
    float mapped = MapAdaptiveDepth(DepthWarp, viewZ, 80., 1., 0.);
    if (mapped <= 0.)
        return 1.;
    if (mapped < 1.)
    {
        float3 first = TransmittanceLut.SampleLevel(LinearClamp, float3(uv, .5 / float(SLICE_COUNT)), 0).rgb;
        return lerp(float3(1, 1, 1), first, mapped);
    }
    return TransmittanceLut.SampleLevel(LinearClamp, float3(uv, (mapped - .5) / float(SLICE_COUNT)), 0).rgb;
}
void AccumulateVolumeSegment(float2 segment, float sampleFraction, float rayZ, VolumeVaryings i, bool behind,
                             bool textured, float2 uv, float3 beginT, float3 endT, inout VolumeAccumulation o)
{
    if (segment.y <= segment.x)
        return;
    // Jitter the quadrature point, not the integration bounds: the complete
    // interval still contributes tau, including the first and last partial step.
    float3 tau = i.sigma * (segment.y - segment.x) / rayZ;
    float sampleZ = lerp(segment.x, segment.y, sampleFraction);
    if (textured)
    {
        // Integrate density more finely than lighting. A single 50cm sample
        // can miss the visible skin while the LUT already sees opaque smoke.
        // Four stratified taps share one lighting evaluation and one LUT interval.
        float2 data = 0;
        [unroll] for (uint tap = 0; tap < 4; ++tap) data +=
            SampleVolumeData(i, uv, lerp(segment.x, segment.y, (float(tap) + sampleFraction) * .25), rayZ) * .25;
        tau *= data.r;
        if (VolumeLights[4].y == 0.)
            i.source *= .18 + .82 * exp(-i.sigma * data.g * (2. * i.extent.y));
    }
    if (!any(tau > 0.))
        return;
    if (VolumeLights[4].y > 0.)
    {
        float3 world = Camera[0].xyz + WorldRay(uv) * (sampleZ / rayZ);
        i.source *= EvaluateVolumeLighting(world);
    }
    // Integrate the visibility over the segment instead of allowing a random
    // point behind the quantized zero frontier to erase the entire interval.
    float3 meanT = .5 * (beginT + endT);
    float3 weight = tau * meanT;
    // Differential weights are additive under actor partition. AVBOIT's common
    // (1-exp(-totalTau))/denominator normalization supplies integrated opacity.
    float3 radiance = i.source * weight;
    o.numerator += radiance;
    o.denominator += weight;
    o.totalTau += tau;
    if (behind)
        o.backNumerator += radiance;
}
VolumeAccumulation ShadeVolume(VolumeVaryings i, bool textured, bool halfResolution = false)
{
    VolumeAccumulation o = (VolumeAccumulation)0;
    uint2 pixel = uint2(i.position.xy);
    float2 uv = i.position.xy / float2(WIDTH, HEIGHT), interval;
    float rayZ;
    if (!VolumeInterval(i, uv, Camera[5].w, interval, rayZ))
        return o;
    float stopZ = min(interval.y, LoadOpaqueViewDepth(pixel));
    if (stopZ <= interval.x)
        return o;
    // A quantized zero frontier may stop subsequent steps, but must not move
    // quadrature/lighting points inside the last fixed-size step. Real opaque
    // surfaces with nonzero transmission still clip the integration interval.
    if (any(SampleVolumeTransmittance(uv, stopZ) > 0.))
        interval.y = stopZ;
    float interfaceDevice = InterfaceDepth.Load(int3(pixel, 0));
    float interfaceZ = interfaceDevice > 0 ? ViewDepthFromDevice(interfaceDevice) : Camera[5].w;
    float baseStep = Camera[10].x;
    uint maxSteps = uint(Camera[10].w);
    // Full-resolution rays stratify depth phases over a quad. Half-resolution
    // rays use independent blue noise: fixed quad phases become visible as an
    // axis-aligned pattern after upsampling high-extinction smoke.
    uint2 marchPixel = halfResolution ? pixel / 2u : pixel;
    uint2 noisePixel = (halfResolution ? marchPixel : marchPixel / 2u) & 31u;
    float jitter = (VolumeBlueNoise[noisePixel.y * 32u + noisePixel.x] + .5) / 1024.;
    if (Camera[9].w > 0.)
        jitter = frac(jitter + (Camera[9].w - 1.) * .618033989);
    if (!halfResolution)
    {
        uint phase = (marchPixel.x & 1u) * 2u;
        phase = (marchPixel.y & 1u) ? 3u - phase : phase; // [0,2;3,1]
        if (Camera[9].w > 0.)
            phase = (phase + uint(Camera[9].w - 1.)) & 3u;
        jitter = (float(phase) + jitter) * .25;
    }
    float z = interval.x;
    float3 t = SampleVolumeTransmittance(uv, z);
    [loop] for (uint step = 0; step < maxSteps && z < stopZ; ++step)
    {
        float visibleT = max(t.r, max(t.g, t.b));
        float lowT = 1. - smoothstep(0., Camera[10].y, visibleT);
        float ds = baseStep * lerp(1., Camera[10].z, lowT);
        // A budget limit coarsens the remaining interval rather than dropping
        // its density/tau and accidentally making long volumes transparent.
        ds = max(ds, (interval.y - z) / (rayZ * float(maxSteps - step)));
        float endZ = min(z + ds * rayZ, interval.y);
        float split = clamp(interfaceZ, z, endZ);
        float3 endT = SampleVolumeTransmittance(uv, endZ);
        float3 splitT = split <= z ? t : (split >= endZ ? endT : SampleVolumeTransmittance(uv, split));
        AccumulateVolumeSegment(float2(z, split), jitter, rayZ, i, false, textured, uv, t, splitT, o);
        AccumulateVolumeSegment(float2(split, endZ), jitter, rayZ, i, true, textured, uv, splitT, endT, o);
        z = endZ;
        t = endT;
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
    return ShadeVolume(i, false, true);
}
VolumeAccumulation texturedAccumulationHalf(VolumeVaryings i)
{
    i.position.xy = floor(i.position.xy) * 2. + 1.5;
    return ShadeVolume(i, true, true);
}
