// SPDX-License-Identifier: MIT
#include "ResolveData.hlsli"
#include "FrostDistortion.hlsli"
struct Accumulation
{
    float3 numerator : SV_Target0;
    float3 denominator : SV_Target1;
    float3 totalTau : SV_Target2;
    float3 backNumerator : SV_Target3;
};
[earlydepthstencil] Accumulation accumulationPixel(Varyings i)
{
    float2 uv = i.position.xy / float2(WIDTH, HEIGHT);
    if (i.depth >= LoadOpaqueViewDepth(uint2(i.position.xy)))
        discard;
    float3 alpha = Opacity(i);
    float3 t;
    float flatBin = 0.;
    if (i.flatOrdering.x > 0.)
    {
        flatBin = FlattenedOitBin(DepthWarp, i.flatOrdering, SLICE_COUNT - 2);
        t = flatBin < 1. ? float3(1, 1, 1) : SampleTransmittanceAtZ(uv, (flatBin - .5) / float(SLICE_COUNT));
    }
    else
        t = SampleSurfaceTransmittance(uv, i.depth, i.cameraOffsetBins);
    Accumulation o = (Accumulation)0;
    // Only surviving fragments accumulate tau. The optional shading-only
    // shortcut below preserves tau; hardware zero-T depth rejection does not.
    o.totalTau = TransmittanceToOpticalDepth(1. - alpha);
    [branch] if ((uint(Camera[7].w) & 1u) != 0 && all(t == 0)) return o;
    o.numerator =
        (i.kind == 4 || (i.kind == 2 && i.sphere.w < -1.5) ? SphereSurfaceLight(i, uv) : i.color.rgb * alpha) * t;
    if (i.kind == 2 && i.effect.x > 0)
    {
        float light = .25 + .75 * saturate(dot(normalize(i.surfaceNormal), normalize(float3(-.4, .7, -.6))));
        float stripes = i.effect.x == 2 ? .5 + .5 * smoothstep(-.2, .2, sin(i.materialLocal.y * 24.)) : 1.;
        o.numerator = i.color.rgb * (light * stripes) * alpha * t;
    }
    o.denominator = alpha * t;
    bool behind = BehindInterface(uint2(i.position.xy), i.position.z);
    if (i.cameraOffsetBins != 0 || i.flatOrdering.x > 0.)
    {
        float4 hit = LoadInterface(uint2(i.position.xy));
        float orderingZ =
            i.flatOrdering.x > 0. ? flatBin : MapAdaptiveDepth(DepthWarp, i.depth, 80., 1., 0.) - i.cameraOffsetBins;
        float interfaceZ = MapAdaptiveDepth(DepthWarp, hit.x + InterfaceTolerance(hit.x), 80., 1., 0.);
        behind = hit.w != 0 && orderingZ > interfaceZ;
    }
    o.backNumerator = behind ? o.numerator : float3(0, 0, 0);
    // Diagnostic volume counters use numerator; exclude ordinary surfaces.
    if (Camera[12].w > 0.)
        o.numerator = 0;
    return o;
} Texture2D<uint> ScreenTiles : register(t30);
Texture2D<float2> InterfaceOffset : register(t14);

float4 backgroundPixel(Fullscreen i) : SV_Target0
{
    uint2 p = uint2(i.position.xy);
    // This cache is written at the DESTINATION, only in occupied tiles.
    if ((ScreenTiles[p / 64] & 4u) == 0)
        return float4(Opaque.Load(int3(p, 0)).rgb, 1);
    float2 offset = InterfaceOffset.Load(int3(p, 0));
    float sigma = InterfaceSurface.Load(int3(p, 0)).a;
    // Retain the bounded part for the Gaussian lookup, rather than blurring it.
    offset -= FrostDetailOffset(offset, sigma);
    float depth = InterfaceDepth.Load(int3(p, 0));
    if (all(offset == 0) || depth <= OpaqueHardwareDepth.Load(int3(p, 0)))
        return ResolveBackground(p, (float2(p) + .5) / float2(WIDTH, HEIGHT));

    float2 source = clamp(float2(p) + offset, 0., float2(WIDTH - 1, HEIGHT - 1));
    uint2 nearest = min(uint2(source + .5), uint2(WIDTH - 1, HEIGHT - 1));
    float2 uv = (source + .5) / float2(WIDTH, HEIGHT);
    // Depth, interface membership and validity use the nearest source texel.
    // Frost rejects uncovered/occluded sources; sharp refraction accepts foreground.
    if (sigma > 0)
    {
        float sourceDepth = InterfaceDepth.Load(int3(nearest, 0));
        if (sourceDepth == 0 || sourceDepth <= OpaqueHardwareDepth.Load(int3(nearest, 0)))
            return 0;
    }
    if ((ScreenTiles[nearest / 64] & 4u) == 0)
        return float4(Opaque.SampleLevel(LinearClamp, uv, 0).rgb, 1);
    // Filter the full-screen RT inputs, then resolve once. Never sample the
    // partial B cache here; final Front and RGB q remain at the destination.
    return ResolveBackground(nearest, uv, true);
}
