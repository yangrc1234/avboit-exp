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
// Surface-only experiment: decorrelate the coarse XY grid without changing Z.
// Every layer of a screen pixel shares the same offset. No texture fetches.
float2 SurfaceLutUV(float2 uv, float2 pixel)
{
    float width = Camera[13].x;
    [branch] if (width <= 0.) return uv;
    // The host freezes this phase when TAA is disabled.
    float frame = Camera[13].y;
    float2 p = floor(pixel) + frame * 5.588238;
    float2 n = frac(52.9829189 * frac(float2(dot(p, float2(.06711056, .00583715)),
                                             dot(p.yx + float2(19., 47.), float2(.06711056, .00583715)))));
    return uv + (n - .5) * width / Camera[6].zw;
}
// A projected triangle is affine in device depth. Evaluate that plane at the
// actual four LUT column centres, rather than cutting all columns at one Z.
// SampleSurfaceTransmittance retains the existing virtual-slice self bias and
// authored camera offset. Splat's slope push is unchanged, not applied twice.
float3 SampleReceiverPlaneTransmittance(float2 uv, float deviceDepth, float2 depthGradient, float offsetBins)
{
    float2 grid = uv * Camera[6].zw - .5;
    int2 base = int2(floor(grid));
    float2 fraction = frac(grid);
    float3 result = 0.;
    [unroll] for (int y = 0; y < 2; ++y)
    {
        [unroll] for (int x = 0; x < 2; ++x)
        {
            int2 column = clamp(base + int2(x, y), int2(0, 0), int2(Camera[6].zw) - 1);
            float2 tapUV = (float2(column) + .5) / Camera[6].zw;
            float2 deltaPixels = (tapUV - uv) * Camera[6].xy;
            float tapDeviceDepth = saturate(deviceDepth + dot(depthGradient, deltaPixels));
            float tapDepth = ViewDepthFromDevice(tapDeviceDepth);
            float weight = (x == 0 ? 1. - fraction.x : fraction.x) * (y == 0 ? 1. - fraction.y : fraction.y);
            result += weight * SampleSurfaceTransmittance(tapUV, tapDepth, offsetBins);
        }
    }
    return result;
}
[earlydepthstencil] Accumulation accumulationPixel(Varyings i)
{
    // Evaluate before material clipping or per-pixel opaque rejection.
    float2 depthGradient = 0.;
    [branch] if (Camera[13].z != 0.) depthGradient = float2(ddx_fine(i.position.z), ddy_fine(i.position.z));
    float2 uv = i.position.xy / float2(WIDTH, HEIGHT);
    if (i.depth >= LoadOpaqueViewDepth(uint2(i.position.xy)))
        discard;
    float3 alpha = Opacity(i);
    float3 t;
    float2 lutUV = SurfaceLutUV(uv, i.position.xy);
    float flatBin = 0.;
    if (i.flatOrdering.x > 0.)
    {
        flatBin = FlattenedOitBin(DepthWarp, i.flatOrdering, SLICE_COUNT - 2);
        t = flatBin < 1. ? float3(1, 1, 1) : SampleTransmittanceAtZ(lutUV, (flatBin - .5) / float(SLICE_COUNT));
        // The whole group deposits in one bin and shares its exclusive prefix.
        // Only accumulation uses real fragment depth for ordering within the group.
        // Bounded [2^-strength, 1] weights affect N and D equally, never totalTau.
        float relativeDepth = saturate(.5 + (i.depth - i.flatOrdering.x) / max(i.flatWeight.y, .01));
        t *= exp2(-i.flatWeight.x * relativeDepth);
    }
    else if (Camera[13].z != 0.)
        t = SampleReceiverPlaneTransmittance(uv, i.position.z, depthGradient, i.cameraOffsetBins);
    else
        t = SampleSurfaceTransmittance(lutUV, i.depth, i.cameraOffsetBins);
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
#if MARCH_COUNTERS
    o.numerator = 0;
#endif
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
