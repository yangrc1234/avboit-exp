// SPDX-License-Identifier: MIT
// Shared demo geometry/material ABI and camera helpers.
// Camera[6] carries actual full/volume dimensions; no resolution permutations.
#ifndef AVBOIT_TRANSPARENCY_COMMON
#define AVBOIT_TRANSPARENCY_COMMON
#define WIDTH uint(Camera[6].x)
#define HEIGHT uint(Camera[6].y)
#ifndef VOLUME_SCALE
#define VOLUME_SCALE 8
#endif
#define VOLUME_WIDTH uint(Camera[6].z)
#define VOLUME_HEIGHT uint(Camera[6].w)
#ifndef SLICE_COUNT
#define SLICE_COUNT 128
#endif
#define RAY_COUNT (VOLUME_WIDTH * VOLUME_HEIGHT)
#define SCALAR_WORDS (SLICE_COUNT / 4 * RAY_COUNT)
Buffer<float4> Vertices : register(t0);
Buffer<float4> Materials : register(t23);
Buffer<float4> Camera : register(t15);
SamplerState LinearClamp : register(s0);
struct Varyings
{
    float4 position : SV_Position;
    float depth : DEPTH;
    float4 color : COLOR;
    float3 transmission : TRANSMISSION;
    float2 local : TEXCOORD;
    float3 surfaceNormal : NORMAL;
    nointerpolation float4 sphere : SPHERE;
    float roughness : ROUGHNESS;
    float distortion : DISTORTION;
    nointerpolation uint kind : KIND;
    nointerpolation float4 optics : OPTICS; // IOR, gain, displacement, sphere normal mode
    nointerpolation uint scalarExtinction : SCALAR_EXTINCTION;
    nointerpolation float2 effect : EFFECT; // VFX pattern and phase offset.
};
Varyings meshVertex(uint id : SV_VertexID)
{
    float4 p = Vertices[id * 4];
    Varyings o;
    if (p.w >= 0)
    {
        float3 delta = p.xyz - Camera[0].xyz;
        p.xyz = float3(dot(delta, Camera[1].xyz), dot(delta, Camera[2].xyz), dot(delta, Camera[3].xyz));
    } // Negative position.w marks geometry already in view space.
    o.position =
        float4(p.x / (float(WIDTH) / HEIGHT * .57735027), p.y / .57735027, p.z * 80. / 79.95 - 4. / 79.95, p.z);
    uint material = uint(Vertices[id * 4 + 1].w) * 4;
    o.depth = p.z;
    o.color = Materials[material];
    o.transmission = Materials[material + 1].rgb;
    o.roughness = Materials[material + 1].w;
    o.optics = Materials[material + 2];
    o.kind = uint(Materials[material + 3].x);
    o.scalarExtinction = uint(Materials[material + 3].y);
    o.effect = Materials[material + 3].zw;
    o.local = Vertices[id * 4 + 3].xy;
    o.distortion = o.optics.z * o.optics.y;
    o.surfaceNormal = float3(0, 0, -1);
    o.sphere = float4(0, 0, 0, Vertices[id * 4 + 3].w);
    if (o.sphere.w < -1.5)
    {
        float3 normal = Vertices[id * 4 + 3].xyz;
        o.surfaceNormal =
            p.w < 0 ? normal.x * Camera[1].xyz + normal.y * Camera[2].xyz + normal.z * Camera[3].xyz : normal;
    }
    if (o.kind == 4)
    {
        float4 shape = Vertices[id * 4 + 3];
        o.surfaceNormal = shape.xyz;
        o.sphere = float4(Vertices[id * 4].xyz - shape.xyz * shape.w, shape.w);
        o.distortion = (o.optics.w == 0 && o.optics.x <= 1.) ? 0. : 18. * o.optics.y;
    }
    return o;
}
float3 Opacity(Varyings i)
{
    if (i.kind == 2 && i.sphere.w > 0)
        clip(length(i.local) - i.sphere.w);
    float coverage = i.color.a;
    if (i.kind == 1)
    {
        float r = length(i.local);
        coverage *= exp(-dot(i.local, i.local) * 2.5) * (1. - smoothstep(.4, 1., r));
        coverage *= .78 + .22 * sin(i.local.x * 8. + sin(i.local.y * 6.));
    }
    return saturate(coverage * (1. - i.transmission));
}
float3 WorldRay(float2 uv)
{
    return normalize(Camera[3].xyz + Camera[1].xyz * ((uv.x * 2. - 1.) * (float(WIDTH) / HEIGHT) * .57735027) +
                     Camera[2].xyz * ((1. - uv.y * 2.) * .57735027));
}
float3 SphereSurfaceLight(Varyings i, float2 uv)
{
    float3 n = normalize(i.surfaceNormal), v = -WorldRay(uv);
    if (dot(n, v) < 0.)
        n = -n;
    float3 h = v + normalize(float3(-.4, .7, -.5));
    h *= rsqrt(max(dot(h, h), 1e-12));
    float spec = pow(saturate(dot(n, h)), 100.);
    float fres = pow(1. - saturate(dot(n, v)), 5.);
    return float3(.012, .02, .026) + spec * float3(1.8, 1.6, 1.25) + fres * float3(.15, .2, .24);
}
struct Fullscreen
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD;
};
Fullscreen fullscreenVertex(uint id : SV_VertexID)
{
    Fullscreen o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.position = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

#endif
