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
ResolveTerms GetResolveTerms(uint2 p, float2 uv)
{
    ResolveTerms o = (ResolveTerms)0;
    // Use the hardware depth (including zero-T quads) for source validity.
    float sceneDepth = OpaqueHardwareDepth.Load(int3(p, 0));
    // Deliberately approximate when zero-T depth culled the remaining layers:
    // keep the usual full-resolution tau resolve, with no cutoff/LUT correction.
    o.totalT = exp(-TotalTau.Load(int3(p, 0)));
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
    float mapped = MapAdaptiveDepth(DepthWarp, hit.r, 80., 1., 2.);
    float3 frontT =
        mapped < 0 ? float3(1, 1, 1)
                   : TransmittanceLut.SampleLevel(LinearClamp, float3(uv, (mapped + .5) / float(SLICE_COUNT)), 0).rgb;
    float3 q = saturate(frontT * InterfaceSurface.Load(int3(p, 0)).rgb);
    if (Camera[7].x > 0)
        q = max(o.totalT, q);
    o.backgroundValid = Camera[7].y == 0 || min(q.r, min(q.g, q.b)) >= Camera[7].z;
    o.transmission = float4(q, 1);
    return o;
}
float4 ResolveBackground(uint2 p, float2 uv)
{
    ResolveTerms t = GetResolveTerms(p, uv);
    if (!t.backgroundValid)
        return 0;
    float3 opaqueContribution = Opaque.Load(int3(p, 0)).rgb * t.totalT;
    if (!t.hasInterface || t.opaqueOccluded)
        return float4(Numerator.Load(int3(p, 0)) * t.normalization + opaqueContribution, 1);
    float3 residual = BackNumerator.Load(int3(p, 0)) * t.normalization + opaqueContribution;
    // q stays in registers: no intermediate texture or storage quantization.
    return float4(residual / max(t.transmission.rgb, .00001), 1);
}
#endif
