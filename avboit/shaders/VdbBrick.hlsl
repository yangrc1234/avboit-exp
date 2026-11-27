// SPDX-License-Identifier: MIT
#define SMOKE_MODE 4
#ifndef BRICK_PIXEL_STRIDE
#define BRICK_PIXEL_STRIDE 1
#endif
#include "LocalVolume.hlsli"
#include "VdbProxyProjection.hlsli"
#include "ResolveData.hlsli"
#include "VolumeTransmittance.hlsli"
#include "VolumeLighting.hlsli"
#include "VolumeBlueNoise.hlsli"
#include "SmokeFilterRandom.hlsli"

cbuffer BrickDraw : register(b4) { uint ActorVertex; };

#if BRICK_LIGHT_MODE == 3
Texture3D<float4> BrickLightCache : register(t37);
#endif

#if CLASSIFIED_BRICK
Buffer<uint4> BrickList : register(t39);
#endif

struct Brick
{
    float4 position : SV_Position;
#if BRICK_VERTEX_LIGHT || BRICK_LIGHT_MODE == 2
    nointerpolation float3 cornerLight[8] : BRICKLIGHT;
#endif
#if BRICK_CUBE
    nointerpolation uint exitFace : EXITFACE;
#if BRICK_PIXEL_STRIDE == 1
    float surfaceViewZ : SURFACEVIEWZ;
#endif
#endif
    nointerpolation uint record : RECORD;
    nointerpolation uint3 firstCell : CELL;
    nointerpolation float3 densityDecode : DECODE; // density bias, scale and AO extinction-length product
    nointerpolation float3 origin : ORIGIN;
    nointerpolation float3 axisX : AXISX;
    nointerpolation float3 axisY : AXISY;
    nointerpolation float3 axisZ : AXISZ;
    nointerpolation float4 sourceSigma : MATERIAL;
};

Brick brickVertex(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    Brick o = (Brick)0;
    o.position.w = 1.;
#if CLASSIFIED_BRICK
    // The two draws consume opposite ends of the same compact list.
    bool low = (ActorVertex >> 31u) != 0u;
    uint count = VdbGrid.x * VdbGrid.y * VdbGrid.z;
    uint4 item = BrickList[low ? count - 1u - instance : instance];
    instance = item.x & 0x7fffffffu;
    uint entry = item.y;
    uint actorVertex = ActorVertex & 0x7fffffffu;
#else
    uint entry = VdbEntry(instance);
    if (!entry)
        return o;
    uint actorVertex = ActorVertex;
#endif
    VolumeVaryings actor = LoadVolumeActor(actorVertex);
    o.firstCell = 4u * uint3(instance % VdbGrid.x, (instance / VdbGrid.x) % VdbGrid.y,
                             instance / (VdbGrid.x * VdbGrid.y));
    float3 lo = select(o.firstCell == 0u, 0., float3(o.firstCell) + .5);
    float3 hi = min(float3(o.firstCell) + 4.5, float3(VdbSize));
#if BRICK_CUBE
    o.exitFace = item.x >> 31u;
    uint corner = vertex ^ o.exitFace; // Reflect X to retain exit faces for near-plane bricks.
    float3 uvw = lerp(lo, hi, float3(corner & 1, (corner >> 1) & 1, corner >> 2)) / VdbSize;
    float3 local = (uvw * 2. - 1.) * actor.extent;
    float3 delta = actor.center - Camera[0].xyz + local.x * actor.axisX + local.y * actor.axisY +
                   local.z * cross(actor.axisX, actor.axisY);
    float z = dot(delta, Camera[3].xyz);
    o.position = float4(dot(delta, Camera[1].xyz) / (.57735027 * float(WIDTH) / HEIGHT),
                        dot(delta, Camera[2].xyz) / .57735027, ClipDepthFromView(z), z);
#if BRICK_PIXEL_STRIDE == 1
    // Near-crossing bricks rasterize their exit. An opaque surface may lie inside
    // the brick: let its PS clip the interval against that surface.
    o.surfaceViewZ = z;
    if (o.exitFace)
        o.position.z = o.position.w;
#endif
    o.position.xy += (Camera[11].xy - .5 * (BRICK_PIXEL_STRIDE - 1)) * float2(2., -2.) / Camera[6].xy * z;
#else
    // Both proxy triangles use the same leading corner with indexed drawing.
    const uint cornerOrder[6] = {1, 2, 0, 1, 2, 5};
    bool crossesNearPlane;
    o.position = ProjectVdbBox(cornerOrder[vertex], actor, lo, hi, false, crossesNearPlane);
#endif
#if BRICK_VERTEX_LIGHT
    if ((vertex == 0 || vertex == 3) && VolumeLights[4].y > 0.)
    {
        [loop] for (uint i = 0; i < 8; ++i)
        {
            float3 corner = float3(i & 1u, (i >> 1) & 1u, i >> 2);
            float3 uvw = lerp(lo, hi, corner) / VdbSize;
            float3 local = (uvw * 2. - 1.) * actor.extent;
            float3 world = actor.center + local.x * actor.axisX + local.y * actor.axisY +
                           local.z * cross(actor.axisX, actor.axisY);
            float visibility = SampleVdbAmbientVisibility(uvw, VdbExtinctionLength(actor.sigma.r, actor.extent));
            o.cornerLight[i] = EvaluateVolumeLighting(world, visibility);
        }
    }
#endif
    o.record = VdbPayloadOffset + (entry - 1u) * 17u;
    uint range = SmokeBricks[o.record];
    float bias = f16tof32(range & 65535u);
    o.densityDecode = float3(bias, (f16tof32(range >> 16) - bias) / 15., VdbExtinctionLength(actor.sigma.r, actor.extent));
    float3 scale = VdbSize / (2. * actor.extent);
    o.axisX = actor.axisX * scale.x;
    o.axisY = actor.axisY * scale.y;
    o.axisZ = cross(actor.axisX, actor.axisY) * scale.z;
    o.origin = VolumeLocal(actor, Camera[0].xyz - actor.center) * scale + .5 * VdbSize - .5;
    o.sourceSigma = float4(actor.source, actor.sigma.r);
#if CLASSIFIED_BRICK
    o.sourceSigma.rgb *= float3(f16tof32(item.z & 65535u), f16tof32(item.z >> 16), f16tof32(item.w & 65535u));
#endif
    return o;
}

bool ClipBrick(Brick b, float3 velocity, inout float2 interval)
{
    float3 lo = select(b.firstCell == 0u, -.5, float3(b.firstCell));
    float3 hi = min(float3(b.firstCell + 4u), float3(VdbSize) - .5);
    [unroll] for (uint axis = 0; axis < 3; ++axis)
    {
        float p = b.origin[axis], v = velocity[axis];
        if (v == 0.)
        {
            if (p < lo[axis] || p >= hi[axis])
                return false;
        }
        else
        {
            float2 hit = (float2(lo[axis], hi[axis]) - p) / v;
            interval = float2(max(interval.x, min(hit.x, hit.y)), min(interval.y, max(hit.x, hit.y)));
        }
    }
    return interval.y > interval.x;
}

float BrickDensity(Brick b, float3 voxel, uint filterSeed)
{
    float3 p = clamp(voxel, 0., float3(VdbSize - 1u));
    float3 local = clamp(p - b.firstCell, 0., 4.);
    uint3 cell = min(uint3(local), 3u);
    float3 f = local - cell;
    uint base = cell.x + 5u * cell.y + 25u * cell.z;
#if SMOKE_STOCHASTIC
    uint3 corner = uint3(SmokeFilterRandom(filterSeed) < f);
    float value = LoadSmokeVoxel(b.record, base + corner.x + 5u * corner.y + 25u * corner.z, false);
#else
    float value = 0.;
    [unroll] for (uint corner = 0; corner < 8; ++corner)
    {
        uint3 c = uint3(corner & 1u, (corner >> 1) & 1u, corner >> 2);
        float3 w = lerp(1. - f, f, float3(c));
        value += w.x * w.y * w.z * LoadSmokeVoxel(b.record, base + c.x + 5u*c.y + 25u*c.z, false);
    }
#endif
    float mask = GameplaySmokeMask.SampleLevel(LinearClamp, (voxel + .5) / VdbSize, 0);
    return (b.densityDecode.x + value * b.densityDecode.y) * mask;
}

#if BRICK_LIGHT_MODE == 3
float3 InterpolateBrickLight(Brick b, float3 f)
{
    float3 uvw = (float3(b.firstCell / 4u) + f + .5) / float3(VdbGrid + 1u);
    return BrickLightCache.SampleLevel(LinearClamp, uvw, 0).rgb;
}
#elif BRICK_LIGHT_MODE >= 2
float3 InterpolateBrickLight(Brick b, float3 f)
{
    float3 nearLight = lerp(lerp(b.cornerLight[0], b.cornerLight[1], f.x),
                            lerp(b.cornerLight[2], b.cornerLight[3], f.x), f.y);
    float3 farLight = lerp(lerp(b.cornerLight[4], b.cornerLight[5], f.x),
                           lerp(b.cornerLight[6], b.cornerLight[7], f.x), f.y);
    return lerp(nearLight, farLight, f.z);
}
#endif

struct BrickTargets
{
    float3 numerator : SV_Target0;
    float3 denominator : SV_Target1;
    float3 totalTau : SV_Target2;
    float3 backNumerator : SV_Target3;
};

#if !CLASSIFIED_LOW
void AddBrickLight(inout BrickTargets o, float3 materialColor, float2 uv, float3 ray, float rayZ,
                   float t, float tau, bool behind, float3 localUV, float extinctionLength)
{
    float3 T = SampleVolumeTransmittance(uv, t * rayZ);
    float3 weight = tau * T;
    float3 source = materialColor;
    if (VolumeLights[4].y > 0.)
    {
        float visibility = SampleVdbAmbientVisibility(localUV, extinctionLength);
#if CLASSIFIED_BRICK
        source *= EvaluateVolumeLighting(Camera[0].xyz + ray * t, visibility);
#else
        source *= max(T.r, max(T.g, T.b)) < .001 ? VolumeLights[4].xxx * visibility :
                  EvaluateVolumeLighting(Camera[0].xyz + ray * t, visibility);
#endif
    }
    float3 light = weight * source;
    o.denominator += weight;
    o.numerator += light;
    if (behind)
        o.backNumerator += light;
}
#endif

#if BRICK_CUBE && BRICK_PIXEL_STRIDE == 1
[earlydepthstencil]
#endif
BrickTargets brickPixel(Brick b)
{
    uint2 rasterPixel = uint2(b.position.xy);
    uint2 pixel = rasterPixel * BRICK_PIXEL_STRIDE + (BRICK_PIXEL_STRIDE - 1);
    float2 uv = (float2(pixel) + .5) / float2(WIDTH, HEIGHT);
    float3 ray = WorldRay(uv);
    float rayZ = dot(ray, Camera[3].xyz);
    float3 velocity = float3(dot(b.axisX, ray), dot(b.axisY, ray), dot(b.axisZ, ray));
    float2 interval = float2(Camera[5].z, min(Camera[5].w, LoadOpaqueViewDepth(pixel))) / rayZ;
#if BRICK_CUBE
#if BRICK_PIXEL_STRIDE == 1
    float faceT = b.surfaceViewZ / rayZ;
#else
    float faceT = ViewDepthFromDevice(b.position.z) / rayZ;
#endif
    bool exitFace = b.exitFace != 0;
    float3 lo = select(b.firstCell == 0u, -.5, float3(b.firstCell));
    float3 hi = min(float3(b.firstCell + 4u), float3(VdbSize) - .5);
    float3 other = select((velocity >= 0.) != exitFace, hi, lo);
    float3 hit = select(velocity != 0., (other - b.origin) / velocity, exitFace ? -1e30 : 1e30);
    interval.x = max(interval.x, exitFace ? max(hit.x, max(hit.y, hit.z)) : faceT);
    interval.y = min(interval.y, exitFace ? faceT : min(hit.x, min(hit.y, hit.z)));
    if (interval.y <= interval.x)
#else
    if (!ClipBrick(b, velocity, interval))
#endif
        discard;

    float device = InterfaceDepth.Load(int3(pixel, 0));
    float interfaceT = (device > 0. ? ViewDepthFromDevice(device) : Camera[5].w) / rayZ;
    BrickTargets o = (BrickTargets)0;
    uint filterSeed = SmokeFilterSeed(pixel);
#if CLASSIFIED_LOW
    float t = .5 * (interval.x + interval.y);
    float sigma = BrickDensity(b, b.origin + velocity * t, filterSeed ^ (b.record * 0x85ebca6bu)) * b.sourceSigma.w;
    if (sigma <= 0.)
        discard;
    float lengthInBrick = interval.y - interval.x;
    float3 weightPerMeter = sigma * SampleVolumeTransmittance(uv, t * rayZ);
    float3 source = b.sourceSigma.rgb;
#if BRICK_LIGHT_MODE == 3
    if (VolumeLights[4].y > 0.)
    {
        float3 lo = select(b.firstCell == 0u, -.5, float3(b.firstCell));
        float3 hi = min(float3(b.firstCell + 4u), float3(VdbSize) - .5);
        float3 f = saturate((b.origin + velocity * t - lo) / (hi - lo));
        source *= InterpolateBrickLight(b, f);
    }
#endif
    float3 lightPerMeter = weightPerMeter * source;
    o.totalTau = sigma * lengthInBrick;
    o.denominator = weightPerMeter * lengthInBrick;
    o.numerator = lightPerMeter * lengthInBrick;
    // A glass interface can cut this segment even when its midpoint lies in front.
    o.backNumerator = lightPerMeter * (interval.y - clamp(interfaceT, interval.x, interval.y));
#else
    // Fine-step bricks share the same jittered world-distance lattice along this ray.
    float voxelStep = rcp(max(abs(velocity.x), max(abs(velocity.y), abs(velocity.z))));
    float stepSize = min(Camera[10].x, voxelStep) * Camera[12].w;
    uint2 noisePixel = rasterPixel & 31u;
    float jitter = (VolumeBlueNoise[noisePixel.y * 32u + noisePixel.x] + .5) / 1024.;
    if (Camera[9].w > 0.)
        jitter = frac(jitter + (Camera[9].w - 1.) * .618033989);
    int first = int(ceil(interval.x / stepSize - jitter));
    int end = int(ceil(interval.y / stepSize - jitter));
#if BRICK_LIGHT_MODE == 1
    float2 opticalDepth = 0., firstMoment = 0.;
#endif
#if BRICK_LIGHT_MODE >= 2
    float3 lightLo = select(b.firstCell == 0u, -.5, float3(b.firstCell));
    float3 lightHi = min(float3(b.firstCell + 4u), float3(VdbSize) - .5);
    float3 invLightExtent = rcp(lightHi - lightLo);
#endif
    [loop] for (int k = first; k < end; ++k)
    {
        float t = (k + jitter) * stepSize;
        float tau = BrickDensity(b, b.origin + velocity * t, filterSeed ^ (uint(k) * 0x85ebca6bu)) * (stepSize * b.sourceSigma.w);
        if (tau <= 0.)
            continue;
        o.totalTau += tau;
#if BRICK_LIGHT_MODE == 1
        float2 contribution = t < interfaceT ? float2(tau, 0.) : float2(0., tau);
        opticalDepth += contribution;
        firstMoment += contribution * (t - interval.x);
#elif BRICK_LIGHT_MODE >= 2
        float3 f = saturate((b.origin + velocity * t - lightLo) * invLightExtent);
        float3 T = SampleVolumeTransmittance(uv, t * rayZ);
        float3 weight = tau * T;
        float3 incoming = 1.;
        if (VolumeLights[4].y > 0.)
        {
#if CLASSIFIED_BRICK
            incoming = InterpolateBrickLight(b, f);
#else
            incoming = max(T.r, max(T.g, T.b)) < .001 ? VolumeLights[4].xxx *
                SampleVdbAmbientVisibility((b.origin + velocity * t + .5) / VdbSize, b.densityDecode.z) : InterpolateBrickLight(b, f);
#endif
        }
        float3 light = weight * b.sourceSigma.rgb * incoming;
        o.numerator += light;
        o.denominator += weight;
        if (t >= interfaceT)
            o.backNumerator += light;
#else
        AddBrickLight(o, b.sourceSigma.rgb, uv, ray, rayZ, t, tau, t >= interfaceT,
                      (b.origin + velocity * t + .5) / VdbSize, b.densityDecode.z);
#endif
        if (o.totalTau.r >= Camera[13].w)
            break;
    }
    if (o.totalTau.r == 0.)
        discard;
#if BRICK_LIGHT_MODE == 1
    // Evaluate lighting and incident T at the density-weighted depth on
    // each side of the glass. All density taps still contribute to totalTau.
    [loop] for (uint side = 0; side < 2; ++side)
    {
        float tau = opticalDepth[side];
        if (tau == 0.)
            continue;
        float t = interval.x + firstMoment[side] / tau;
        AddBrickLight(o, b.sourceSigma.rgb, uv, ray, rayZ, t, tau, side == 1,
                      (b.origin + velocity * t + .5) / VdbSize, b.densityDecode.z);
    }
#endif
#endif
    return o;
}
