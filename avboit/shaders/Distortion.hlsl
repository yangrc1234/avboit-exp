// SPDX-License-Identifier: MIT
#include "TransparencyCommon.hlsli"
Buffer<uint> DepthWarp : register(t1);
Texture3D<float4> TransmittanceLut : register(t2);
Texture2D<float4> Opaque : register(t3);
#include "OpaqueDepth.hlsli"
#include "InterfaceDepth.hlsli"
#include "DepthMapping.hlsli"
// VFX material output is a signed screen-space displacement in internal pixels.
// This shock-wave material can be replaced by texture/normal-driven materials.
float2 vfxPixel(Varyings i) : SV_Target
{
    float2 rasterSize = (uint(Camera[7].w) & 8u) != 0 ? float2(WIDTH, HEIGHT) : ceil(float2(WIDTH, HEIGHT) * .5);
    float2 uv = i.position.xy / rasterSize;
    uint2 p = min(uint2(uv * float2(WIDTH, HEIGHT)), uint2(WIDTH - 1, HEIGHT - 1));
    if (i.depth >= LoadOpaqueViewDepth(p))
        discard;
    float4 hit = LoadInterface(p);
    if (hit.a > 0 && i.depth > hit.r + InterfaceTolerance(hit.r))
        discard;
    float r = length(i.local), radius = .58 + .08 * sin(Camera[2].w * 1.7);
    float ring = exp(-pow((r - radius) / .09, 2.)) * (1. - smoothstep(.85, 1., r));
    float2 direction = r > 1e-5 ? i.local / r : float2(0, 0);
    if (i.effect.x == 1)
    {
        ring = 1. - smoothstep(.55, 1., r);
        float t = Camera[2].w + i.effect.y * 2.4;
        direction = float2(sin(i.local.y * 15. + t * 3.), cos(i.local.x * 11. - t * 2.));
    }
    else if (i.effect.x == 2)
    {
        float phase = frac(Camera[2].w / 2.4 + .22 + i.effect.y);
        float band = (r - (.08 + phase * .86)) / .055;
        ring = exp(-band * band) * smoothstep(0., .08, phase) * (1. - smoothstep(.72, 1., phase));
        direction = i.local / max(r, .001) * (1. - .35 * band);
    }
    float mapped = MapAdaptiveDepth(DepthWarp, i.depth, 80., 1., 2.);
    float3 t = mapped < 0
                   ? float3(1, 1, 1)
                   : TransmittanceLut.SampleLevel(LinearClamp, float3(uv, (mapped + .5) / float(SLICE_COUNT)), 0).rgb;
    float visibility = dot(t, float3(.2126, .7152, .0722));
    return direction * ring * Camera[3].w * visibility * i.optics.y;
}
