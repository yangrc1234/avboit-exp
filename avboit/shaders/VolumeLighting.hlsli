// SPDX-License-Identifier: MIT
#ifndef AVBOIT_VOLUME_LIGHTING
#define AVBOIT_VOLUME_LIGHTING
#include "VolumeMomentEncoding.hlsli"
#ifndef VOLUME_LIGHT_SPACE
#define VOLUME_LIGHT_SPACE space0
#endif
Buffer<float4> VolumeLights : register(t27, VOLUME_LIGHT_SPACE);
Texture2DArray<float> RegularLightDepth : register(t28, VOLUME_LIGHT_SPACE);
Texture2DArray<float4> LightMoments : register(t29, VOLUME_LIGHT_SPACE);
Texture2DArray<float> LightOpticalDepth : register(t31, VOLUME_LIGHT_SPACE);

float4 LightTransform(float4 p, uint base)
{
    return p.x * VolumeLights[base] + p.y * VolumeLights[base + 1] + p.z * VolumeLights[base + 2] +
           p.w * VolumeLights[base + 3];
}
float MomentDepth(float deviceZ, uint slice)
{
    // Spot moments use linear light distance rather than hyperbolic device Z.
    float d = slice == 2 ? ((2.5 / (24.9 * deviceZ + .1)) - .1) / 24.9 : 1. - deviceZ;
    return saturate(d) * 2. - 1.;
}
// Three-node quadrature with the receiver as one node. The other two nodes
// bound the CDF compatible with four power moments (MBOIT, 2018).
float MomentCdf(float4 b, float z)
{
    b = lerp(b, float4(0., 1. / 3., 0., 1. / 5.), .0005);
    float d1 = max(b.y - b.x * b.x, 1e-7);
    float l21 = (b.z - b.x * b.y) / d1;
    float d2 = max(b.w - b.y * b.y - l21 * l21 * d1, 1e-7);
    float3 c = float3(1., z - b.x, z * z - b.y - l21 * (z - b.x));
    c.y /= d1;
    c.z /= d2;
    c.y -= l21 * c.z;
    c.x -= b.x * c.y + b.y * c.z;
    if (abs(c.z) < 1e-6)
        return z > b.x ? 1. : 0.;
    float p = c.y / c.z, q = c.x / c.z;
    float root = sqrt(max(p * p * .25 - q, 1e-8));
    float a = -p * .5 - root, d = -p * .5 + root;
    // Lagrange basis integrated against moments yields each quadrature weight.
    float w0 = (b.y - (a + d) * b.x + a * d) / ((z - a) * (z - d));
    float w1 = (b.y - (z + d) * b.x + z * d) / ((a - z) * (a - d));
    float w2 = 1. - w0 - w1;
    float cdf = (a < z ? w1 : 0.) + (d < z ? w2 : 0.) + .5 * w0;
    return isfinite(cdf) ? saturate(cdf) : (z > b.x ? 1. : 0.);
}
float SampleVolumeShadow(float3 world, uint slice)
{
    if (VolumeLights[37][slice] == 0.)
        return 1.;
    float4 projected = LightTransform(float4(world, 1), 13 + slice * 4);
    if (projected.w <= 0.)
        return 1.;
    float3 uvz = projected.xyz / projected.w;
    if (any(uvz < 0.) || any(uvz > 1.))
        return 1.;
    float tau = LightOpticalDepth.SampleLevel(LinearClamp, float3(uvz.xy, slice), 0);
    if (tau < 1e-5)
        return 1.;
    // One hardware bilinear sample for normalized encoded moments, one for tau.
    float4 b = DecodeVolumeMoments(LightMoments.SampleLevel(LinearClamp, float3(uvz.xy, slice), 0));
    return exp(-tau * MomentCdf(b, MomentDepth(uvz.z, slice)));
}
float SunVolumeShadow(float3 world)
{
    float4 p = LightTransform(float4(world, 1), 13);
    float3 uvz = p.xyz / p.w;
    float edge = min(min(uvz.x, 1. - uvz.x), min(uvz.y, 1. - uvz.y));
    if (edge <= 0. || uvz.z < 0. || uvz.z > 1.)
        return SampleVolumeShadow(world, 1);
    float nearT = SampleVolumeShadow(world, 0);
    if (edge >= .05)
        return nearT;
    return lerp(SampleVolumeShadow(world, 1), nearT, edge * 20.);
}
float OpaqueLightVisibility(float3 world, uint light)
{
    float4 p = LightTransform(float4(world, 1), 5 + light * 4);
    float3 uvz = p.xyz / max(p.w, 1e-6);
    if (p.w <= 0. || any(uvz < 0.) || any(uvz > 1.))
        return 1.;
    // Four depth comparisons, bilinear weights. Filtering raw depth would leak.
    uint width, height, layers;
    RegularLightDepth.GetDimensions(width, height, layers);
    float2 texel = uvz.xy * float2(width, height) - .5;
    int2 base = int2(floor(texel));
    float2 f = frac(texel);
    float visibility = 0.;
    [unroll] for (uint y = 0; y < 2; ++y)[unroll] for (uint x = 0; x < 2; ++x)
    {
        int2 pixel = clamp(base + int2(x, y), 0, int2(width, height) - 1);
        float depth = RegularLightDepth.Load(int4(pixel, light, 0));
        visibility += (uvz.z + .0002 >= depth ? 1. : 0.) * (x ? f.x : 1. - f.x) * (y ? f.y : 1. - f.y);
    }
    return visibility;
}
float3 EvaluateVolumeLighting(float3 world)
{
    float sunVisibility = VolumeLights[0].w > 0. ? OpaqueLightVisibility(world, 0) : 0.;
    float3 sun = 0.;
    if (sunVisibility > 0.)
        sun = VolumeLights[0].www * sunVisibility * SunVolumeShadow(world);
    float3 fromLight = world - VolumeLights[1].xyz;
    float distance2 = max(dot(fromLight, fromLight), .01);
    float cone =
        smoothstep(VolumeLights[2].w, VolumeLights[3].w, dot(fromLight * rsqrt(distance2), VolumeLights[2].xyz));
    float rangeFade = pow(saturate(1. - pow(sqrt(distance2) / 25., 4.)), 2.);
    float3 spot = 0.;
    float strength = VolumeLights[1].w * cone * rangeFade / distance2;
    if (strength > 0.)
    {
        float visibility = OpaqueLightVisibility(world, 1);
        if (visibility > 0.)
            spot = VolumeLights[3].rgb * strength * visibility * SampleVolumeShadow(world, 2);
    }
    // Isotropic single scattering. Source color controls scattering albedo/tint.
    return VolumeLights[4].xxx + (sun + spot) / (4. * 3.14159265);
}
#endif
