// SPDX-License-Identifier: MIT
#include "TransparencyCommon.hlsli"
Texture2D<float4> Opaque : register(t3);
#include "OpaqueDepth.hlsli"
float2 SolidSphereOffset(Varyings i, float2 uv)
{
    const float ior = i.optics.x;
    float3 center = i.sphere.xyz, eye = Camera[0].xyz, dir = WorldRay(uv), oc = eye - center;
    float radius = i.sphere.w, b = dot(oc, dir), disc = b * b - dot(oc, oc) + radius * radius;
    if (disc <= 0.)
        return 0;
    bool insideEye = dot(oc, oc) < radius * radius;
    float3 entry = eye + dir * (-b - sqrt(disc));
    float3 inside = insideEye ? dir : refract(dir, normalize(entry - center), 1. / ior);
    if (insideEye)
        entry = eye;
    if (dot(inside, inside) < .0001)
        return 0;
    float3 ec = entry - center;
    float eb = dot(ec, inside), ed = eb * eb - dot(ec, ec) + radius * radius;
    if (ed < 0.)
        return 0;
    float3 exitPoint = entry + inside * max(0., -eb + sqrt(ed));
    float3 outside = refract(inside, -normalize(exitPoint - center), ior);
    float dz = dot(outside, Camera[3].xyz);
    if (dot(outside, outside) < .0001 || dz <= .001)
        return 0;
    // Same one-sample depth approximation as the browser fixture: use the
    // source pixel's opaque depth, not a hidden fixed world-space plane.
    float planeDepth = LoadOpaqueViewDepth(uint2(uv * float2(WIDTH, HEIGHT)));
    float distanceToPlane = (planeDepth - dot(exitPoint - eye, Camera[3].xyz)) / dz;
    if (distanceToPlane <= 0.)
    {
        float3 normal = normalize(i.surfaceNormal);
        return float2(dot(normal, Camera[1].xyz), -dot(normal, Camera[2].xyz)) * 1.6 * i.distortion;
    }
    float3 target = exitPoint + outside * distanceToPlane - eye;
    float z = dot(target, Camera[3].xyz);
    if (z <= .05)
        return 0;
    float2 targetUV = .5 + float2(dot(target, Camera[1].xyz) / (float(WIDTH) / HEIGHT), -dot(target, Camera[2].xyz)) /
                               (z * 1.15470054);
    // UV is clamped at consumption, not the material's refraction magnitude.
    return (targetUV - uv) * float2(WIDTH, HEIGHT) * i.optics.y;
}
struct InterfaceOutput
{
    float4 surface : SV_Target0;
    float2 offset : SV_Target1;
};
InterfaceOutput interfacePixel(Varyings i)
{
    if (i.kind == 2 && i.sphere.w > 0)
        clip(length(i.local) - i.sphere.w);
    if ((i.kind != 2 && i.kind != 4) || ((i.roughness <= 0 || Camera[1].w == 0) && i.distortion == 0))
        discard;
    // Keep the nearest interface even behind opaque geometry. Its depth tells
    // B preparation that an occluding foreground pixel is NOT blur background.
    // Visibility is tested by resolve; no extra depth RT/pass is required.
    bool opaqueOccluded = i.position.z >= OpaqueHardwareDepth.Load(int3(uint2(i.position.xy), 0));
    float rough = Camera[1].w > 0 ? i.roughness : 0;
    // Seven material-local roughness bands, matching the current browser fixture.
    if (rough > 0 && i.kind == 2 && i.sphere.w >= 0)
        rough *= min(floor(saturate(i.local.x * .5 + .5) * 7.), 6.) / 7. + 1. / 7.;
    float theta = min(.35, rough * rough * abs(1. - 1. / i.optics.x)) / 1.177410;
    // Reference pixels at 1440p: invariant to render resolution/dynamic scaling.
    float sigma = min(Camera[4].x, .5 * max((1440. / 1.15470054) * theta, 0.));
    if (sigma <= 0 && i.distortion == 0)
        discard;
    InterfaceOutput o;
    o.surface = float4(i.transmission, sigma);
    if (opaqueOccluded)
    {
        o.offset = 0;
        return o;
    }
    if (i.kind == 4)
    {
        float3 normal = normalize(i.surfaceNormal);
        o.offset = i.optics.w > 0 ? float2(dot(normal, Camera[1].xyz), -dot(normal, Camera[2].xyz)) * 1.6 * i.distortion
                                  : SolidSphereOffset(i, i.position.xy / float2(WIDTH, HEIGHT));
    }
    else
        o.offset = float2(sin(i.local.y * 3.), cos(i.local.x * 3.)) * i.distortion;
    // A larger offset reaches the same clamped screen edge. This preserves
    // the sampled UV while keeping the RG16F value finite (no radius cap).
    o.offset = clamp(o.offset, -float2(WIDTH, HEIGHT), float2(WIDTH, HEIGHT));
    return o;
}
