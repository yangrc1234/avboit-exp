// SPDX-License-Identifier: MIT
#ifndef AVBOIT_RESOLVE_DATA
#define AVBOIT_RESOLVE_DATA
#include "TransparencyCommon.hlsli"
Buffer<uint> DepthWarp : register(t1);
Texture3D<float4> TransmittanceLut : register(t2);
Texture2D<float4> Opaque : register(t3);
#include "OpaqueDepth.hlsli"
Texture2D<float3> Numerator : register(t4);
Texture2D<float3> Denominator : register(t5);
Texture2D<float3> TotalTau : register(t6);
#include "InterfaceDepth.hlsli"
Texture2D<float4> InterfaceSurface : register(t8);
Texture2D<float3> BackNumerator : register(t9);
#include "DepthMapping.hlsli"
#include "OpticalDepth.hlsli"

// Color-independent weights are shared by B preparation and final composition.
struct ResolveTerms
{
    float3 totalT, normalization;
    float4 transmission;
    bool hasInterface, backgroundValid, opaqueOccluded;
};
float3 SampleResolveTransmittance(float2 uv, float viewDepth)
{
    float mapped = MapAdaptiveDepth(DepthWarp, viewDepth, 80., 1., 2.);
    return mapped < 0
               ? float3(1, 1, 1)
               : TransmittanceLut.SampleLevel(LinearClamp, float3(uv, (mapped + .5) / float(SLICE_COUNT)), 0).rgb;
}
ResolveTerms GetResolveTerms(uint2 p, float2 uv)
{
    ResolveTerms o = (ResolveTerms)0;
    // Use the hardware depth (including zero-T quads) for source validity.
    float sceneDepth = OpaqueHardwareDepth.Load(int3(p, 0));
    o.totalT = exp(-TotalTau.Load(int3(p, 0)));
    if ((uint(Camera[7].w) & 2u) != 0)
    {
        // Zero-T depth can cull layers whose unweighted tau would otherwise
        // close this ray. Reuse the filtered LUT at the CURRENT depth, not its
        // far endpoint: a nearer opaque surface may be in front of the smoke.
        // This also closes per-pixel LUT zeros outside conservative quad tiles.
        float viewDepth = Camera[5].y / (sceneDepth - Camera[5].x);
        if (all(SampleResolveTransmittance(uv, viewDepth) == 0))
            o.totalT = 0;
    }
    o.normalization = (1. - o.totalT) / max(Denominator.Load(int3(p, 0)), .000001);
    float4 hit = LoadInterface(p);
    o.hasInterface = hit.a > 0;
    if (!o.hasInterface)
    {
        o.transmission = float4(1, 1, 1, 1);
        o.backgroundValid = true;
        return o;
    }
    // The prepass retains occluded geometry so foreground emission is excluded
    // from frost's source before any bilinear/Gaussian filtering takes place.
    // Both real foreground and zero-T quads reject occluded frost sources.
    o.opaqueOccluded = InterfaceDepth.Load(int3(p, 0)) >= sceneDepth;
    if (o.opaqueOccluded)
    {
        o.transmission = 0; // keep the ordinary OIT/opaque result at this target
        o.backgroundValid = InterfaceSurface.Load(int3(p, 0)).a <= 0;
        return o; // sharp refraction keeps its accepted foreground-sampling rule
    }
    float3 frontT = SampleResolveTransmittance(uv, hit.r);
    float3 q = saturate(frontT * InterfaceSurface.Load(int3(p, 0)).rgb);
    if (Camera[7].x > 0)
        q = max(o.totalT, q);
    o.backgroundValid = Camera[7].y == 0 || min(q.r, min(q.g, q.b)) >= Camera[7].z;
    o.transmission = float4(q, 1);
    return o;
}
float3 ResolveOpaqueContribution(uint2 p, float3 totalT)
{
    // Deferred hosts may skip lighting behind zero-T depth. Do not read that
    // color and multiply by zero: an undefined/NaN value would still propagate.
    [branch] if (all(totalT == 0)) return 0;
    return Opaque.Load(int3(p, 0)).rgb * totalT;
}
float4 ResolveBackground(uint2 p, float2 uv)
{
    ResolveTerms t = GetResolveTerms(p, uv);
    if (!t.backgroundValid)
        return 0;
    float3 opaqueContribution = ResolveOpaqueContribution(p, t.totalT);
    if (!t.hasInterface || t.opaqueOccluded)
        return float4(Numerator.Load(int3(p, 0)) * t.normalization + opaqueContribution, 1);
    float3 residual = BackNumerator.Load(int3(p, 0)) * t.normalization + opaqueContribution;
    // q stays in registers: no intermediate texture or storage quantization.
    return float4(residual / max(t.transmission.rgb, .00001), 1);
}
#endif
