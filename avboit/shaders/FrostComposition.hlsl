// SPDX-License-Identifier: MIT
#include "ResolveData.hlsli"
Texture2D<float4> TemporaryColor : register(t10);
Texture2D<float4> BlurPyramid : register(t13);
Texture2D<float2> InterfaceOffset : register(t14);
#include "BackgroundSampling.hlsli"
#include "FrostScale.hlsli"
float4 CubicLevel(float2 uv, uint level)
{
    uint w, h, levels;
    BlurPyramid.GetDimensions(level, w, h, levels);
    float2 size = float2(w, h), p = uv * size - .5, b = floor(p), f = frac(p);
    float2 w0 = (1. - f) * (1. - f) * (1. - f) / 6., w1 = (3. * f * f * f - 6. * f * f + 4.) / 6.,
           w2 = (-3. * f * f * f + 3. * f * f + 3. * f + 1.) / 6., w3 = f * f * f / 6.;
    float2 g0 = w0 + w1, g1 = w2 + w3, p0 = (b - .5 + w1 / g0) / size, p1 = (b + 1.5 + w3 / g1) / size;
    return lerp(lerp(BlurPyramid.SampleLevel(LinearClamp, p0, level),
                     BlurPyramid.SampleLevel(LinearClamp, float2(p1.x, p0.y), level), g1.x),
                lerp(BlurPyramid.SampleLevel(LinearClamp, float2(p0.x, p1.y), level),
                     BlurPyramid.SampleLevel(LinearClamp, p1, level), g1.x),
                g1.y);
}
float4 SampleInterfaceBackground(uint2 p, float2 uv)
{
    float sigma = InterfaceSurface.Load(int3(p, 0)).a;
    float base = FrostBaseLevel(), variance = max(sigma * sigma, FrostVariance(base));
    float referenceLevel = floor(.5 * log2(16. + 3. * (variance - 9.) / 2.12297680594));
    float a = FrostVariance(referenceLevel), b = FrostVariance(referenceLevel + 1.);
    uint maxMip = uint(Camera[4].y);
    float lod = clamp(referenceLevel - base + (variance - a) / max(b - a, .00001), 0., float(maxMip));
    uv = clamp(uv + InterfaceOffset.Load(int3(p, 0)) / float2(WIDTH, HEIGHT), .5 / float2(WIDTH, HEIGHT),
               1. - .5 / float2(WIDTH, HEIGHT));
    float4 background;
    if (sigma <= 0)
        background = SampleBackgroundCache(uv);
    else if (Camera[0].w == 0)
        background = BlurPyramid.SampleLevel(LinearClamp, uv, lod);
    else
        background = lerp(CubicLevel(uv, uint(lod)), CubicLevel(uv, min(uint(lod) + 1, maxMip)), frac(lod));
    return background;
}
float4 composePixel(Fullscreen i) : SV_Target0
{
    uint2 p = min(uint2(i.position.xy), uint2(WIDTH - 1, HEIGHT - 1));
    // The unculled diagnostic draw also visits tiles outside the cache.
    if ((BackgroundTiles.Load(int3(p / 64, 0)) & 4u) == 0)
        return float4(Opaque.Load(int3(p, 0)).rgb, 1);
    // B is already the fully resolved I where no special interface exists.
    if (LoadInterface(p).a <= 0)
        return float4(CachedBackground.Load(int3(p, 0)).rgb, 1);
    float2 uv = (float2(p) + .5) / float2(WIDTH, HEIGHT);
    ResolveTerms t = GetResolveTerms(p, uv);
    float3 n = Numerator.Load(int3(p, 0));
    float3 color = n * t.normalization + Opaque.Load(int3(p, 0)).rgb * t.totalT;
    if (t.hasInterface && t.transmission.a > 0)
    {
        float4 b = SampleInterfaceBackground(p, uv);
        if (b.a > .00001)
        {
            float3 front = (n - BackNumerator.Load(int3(p, 0))) * t.normalization;
            color = front + t.transmission.rgb * b.rgb / b.a;
        }
    }
    return float4(max(color, 0.), 1);
}
float4 copyPixel(Fullscreen i) : SV_Target0
{
    return float4(TemporaryColor.Load(int3(uint2(i.position.xy), 0)).rgb, 1);
}
