// SPDX-License-Identifier: MIT
#ifndef ACCUMULATION_AUDIT
#define ACCUMULATION_AUDIT 0
#endif
#include "LocalVolume.hlsli"
#include "ResolveData.hlsli"
#include "VolumeLighting.hlsli"
#include "VolumeBlueNoise.hlsli"
struct VolumeMarchResult
{
    float3 numerator;
    float3 denominator;
    LocalOpticalDepth totalTau;
    float3 backNumerator;
#if VDB_NEAR_RAY_LOD && MARCH_COUNTERS
    uint lodMask;
#endif
};
#include "VolumeTransmittance.hlsli"
// Diagnostic variants retain the initial Zero-T clipping query. Replacing only
// march queries requires fixed steps and disabled LUT-equality skipping.
float3 SampleMarchTransmittance(float2 uv, float viewZ)
{
#if (ACCUMULATION_AUDIT & 64)
    return 1.;
#else
    return SampleVolumeTransmittance(uv, viewZ);
#endif
}
// The regular Q4 trilinear permutation never reads the diagnostic flags in the march loop.
#ifndef MARCH_COUNTERS
#define MARCH_COUNTERS 0
#endif
static const bool MarchStats = MARCH_COUNTERS != 0;
#if VDB_TRILINEAR_PRODUCTION
static const bool ReferenceSampling = false, LutIntervalSkip = false;
#else
#define ReferenceSampling ((uint(Camera[12].z) & 2u) != 0)
#define LutIntervalSkip ((uint(Camera[12].z) & 1u) != 0)
#endif

#include "SmokeFilterRandom.hlsli"
void AccumulateVolumeSegment(float2 segment, float sampleFraction, float rayZ, VolumeVaryings i, bool behind,
                             bool textured, float2 uv, float3 beginT, float3 endT, inout VolumeMarchResult o,
                             inout float shadingCount, uint filterSeed,
                             float2 tFraction = float2(0, 1))
{
    if (segment.y <= segment.x)
        return;
    // Jitter the quadrature point, not the integration bounds: the complete
    // interval still contributes tau, including the first and last partial step.
    LocalOpticalDepth sigma = LocalExtinction(i.sigma);
    LocalOpticalDepth tau = sigma * (segment.y - segment.x) / rayZ;
    float sampleZ = lerp(segment.x, segment.y, sampleFraction);
    if (textured)
    {
#if (ACCUMULATION_AUDIT & 32)
        tau *= .5; // Diagnostic only: removes density lookup, not brick traversal.
#else
        uint taps = 4;
        if (SmokeStorageKind() != 0 && !ReferenceSampling)
        {
            // Keep roughly one density sample per crossed voxel on the fastest
            // changing axis. Include the gameplay mask's spatial frequency.
#if SPECIALIZED_VDB
            float3 resolution = VdbFootprintSize;
#else
            uint mx, my, mz;
            GameplaySmokeMask.GetDimensions(mx, my, mz);
            if (SmokeStorageKind() >= 256)
                mx = my = mz = SmokeBricks[1];
            float3 resolution = max(float3(SmokeBricks[1], SmokeBricks[2], SmokeBricks[3]), float3(mx, my, mz));
#endif
            float3 delta =
                abs(VolumeLocal(i, WorldRay(uv))) * ((segment.y - segment.x) / rayZ) / (2. * i.extent) * resolution;
            float footprint = max(delta.x, max(delta.y, delta.z));
            taps = footprint <= 1. ? 1u : footprint <= 2. ? 2u : 4u;
        }
#if (ACCUMULATION_AUDIT & 8)
        taps = 1;
#endif
        float2 data = 0;
        [unroll] for (uint tap = 0; tap < 4; ++tap)
        {
            if (tap < taps)
            {
                float fraction = (float(tap) + sampleFraction) / float(taps);
                float3 filterRandom = 0;
#if SMOKE_STOCHASTIC
                filterRandom = SmokeFilterRandom(filterSeed ^ (tap * 0x9e3779b9u));
#endif
                data += SampleVolumeData(i, uv, lerp(segment.x, segment.y, fraction), rayZ, filterRandom) /
                        float(taps);
            }
        }
        tau *= data.r;
        if (VolumeLights[4].y == 0.)
            i.source *= .18 + .82 * exp(-sigma * data.g * (2. * i.extent.y));
#endif
    }
    if (!any(tau > 0.))
        return;
    shadingCount += 1.;
    // Interpolate only the segment's mean visibility after density decoding.
    // Quad callers retain their existing block endpoints instead of keeping
    // three additional RGB endpoints alive throughout the density taps.
#if QUAD_T && SMOKE_MODE < 6
    float3 meanT = lerp(beginT, endT, .5 * (tFraction.x + tFraction.y));
#else
    float3 meanT = .5 * (beginT + endT);
#endif
    if ((ACCUMULATION_AUDIT & 1) == 0 && VolumeLights[4].y > 0.)
    {
        float3 world = Camera[0].xyz + WorldRay(uv) * (sampleZ / rayZ);
        // Preserve density, denominator and BackN accounting in the tail.
        // Only direct lighting/shadow evaluation is omitted at very low RGB T.
        // Retaining the ambient source avoids turning the entire tail black.
        bool lowContribution = max(meanT.r, max(meanT.g, meanT.b)) < .001;
        float3 incoming;
        if (lowContribution && !ReferenceSampling)
            incoming = VolumeLights[4].xxx;
        else
        {
#if VDB_SHARED_LIGHTING
            // Only coarsen direct lighting when all participating rays are attenuated.
            // A partially active quad at an interface or a brick edge shades independently.
            if (!ReferenceSampling && WaveActiveAllTrue(max(meanT.r, max(meanT.g, meanT.b)) < .125))
            {
                uint lane = WaveGetLaneIndex();
                uint4 active = WaveActiveBallot(true);
                uint quadMask = (active[lane / 32] >> (lane & 28u)) & 15u;
                if (quadMask == 15u)
                {
                    uint sourceLane = (lane & ~3u) + (filterSeed & 3u);
                    float3 light = 0.;
                    if (lane == sourceLane)
                        light = EvaluateVolumeLighting(world);
                    incoming = WaveReadLaneAt(light, sourceLane);
                }
                else
                    incoming = EvaluateVolumeLighting(world);
            }
            else
#endif
                incoming = EvaluateVolumeLighting(world);
        }
#if SPECIALIZED_VDB
        if (textured && VdbAmbientColumnScale > 0.)
        {
            float3 localUV = VolumeLocal(i, world - i.center) / (2. * i.extent) + .5;
            incoming -= VolumeLights[4].xxx * (1. - SampleVdbAmbientVisibility(localUV, VdbExtinctionLength(i.sigma.r, i.extent)));
        }
#endif
        i.source *= incoming;
    }
    // Integrate the visibility over the segment instead of allowing a random
    // point behind the quantized zero frontier to erase the entire interval.
    float3 weight = tau * meanT;
    // Differential weights are additive under actor partition. AVBOIT's common
    // (1-exp(-totalTau))/denominator normalization supplies integrated opacity.
    float3 radiance = i.source * weight;
    o.numerator += radiance;
    o.denominator += weight;
    o.totalTau += tau;
    if (behind)
        o.backNumerator += radiance;
}

// The camera uploads -log(0.01), or a disabled cutoff above all scene optical depths.
bool ReachedLocalOpacity(LocalOpticalDepth totalTau)
{
#if SMOKE_MODE || ACCUMULATION_AUDIT
    return totalTau >= Camera[13].w;
#else
    return min(totalTau.r, min(totalTau.g, totalTau.b)) >= Camera[13].w;
#endif
}
#if QUAD_T
float QuadMinimum(float v)
{
    return min(min(QuadReadLaneAt(v, 0), QuadReadLaneAt(v, 1)), min(QuadReadLaneAt(v, 2), QuadReadLaneAt(v, 3)));
}
float QuadMaximum(float v)
{
    return max(max(QuadReadLaneAt(v, 0), QuadReadLaneAt(v, 1)), max(QuadReadLaneAt(v, 2), QuadReadLaneAt(v, 3)));
}
float3 SharedVolumeT(float2 uv, float z)
{
    // Quad lanes use identical coordinates. Let the texture cache reuse the
    // query without a leader-only branch and a dependent RGB broadcast.
#if VOLUME_COMPUTE
    // Each 2x2 ray quad has a common UV and depth. One lane performs the LUT lookup.
    uint leader = WaveGetLaneIndex() & ~3u;
    float3 transmission = 0.;
    if (WaveGetLaneIndex() == leader)
        transmission = SampleVolumeTransmittance(uv, z);
    return WaveReadLaneAt(transmission, leader);
#else
    return SampleVolumeTransmittance(uv, z);
#endif
}
#if QUAD_T == 2
// Eight anchors in view Z, independent of the density integration lattice.
// Queries advance monotonically: retain only two RGB endpoints, not an array
// of eight live values. Opaque/Zero-T clipping keeps its separate exact query.
struct VolumeTAnchors
{
    float start, length;
    uint cell;
    float3 lower, upper;
};
float3 InterpolateVolumeT(float2 uv, float z, inout VolumeTAnchors anchors)
{
    float coordinate = saturate((z - anchors.start) / anchors.length) * 7.;
    uint cell = min(uint(coordinate), 6u);
    [loop] while (anchors.cell < cell)
    {
        ++anchors.cell;
        anchors.lower = anchors.upper;
        anchors.upper = SharedVolumeT(uv, anchors.start + anchors.length * (float(anchors.cell + 1u) / 7.));
    }
    return lerp(anchors.lower, anchors.upper, saturate(coordinate - float(anchors.cell)));
}
#endif
#if QUAD_T == 3
uint LoadBrickDistance(uint index, uint tableWords)
{
    uint word = SmokeBricks[8 + tableWords + index / 4];
    return (word >> (8 * (index & 3u))) & 255u;
}

// The leader selects a proven-empty box. Each lane intersects its own ray
// with that same box, including at silhouettes and partial brick boundaries.
float QuadEmptyBoxEnd(VolumeVaryings i, float2 uv, float rayZ, float z, uint leader, uint lane)
{
    uint3 size = SmokeVoxelSize();
    uint3 grid = VdbBrickGrid();
    float3 direction = WorldRay(uv) / rayZ;
    float3 pos = VolumeLocal(i, Camera[0].xyz - i.center + direction * z) / (2. * i.extent) + .5;
    float3 p = (pos * size - .5) * .25;
    int3 cell = int3(clamp(floor(p), 0., float3(grid - 1)));
    int3 anchor = QuadReadLaneAt(cell, leader);
    uint d = 0;
    if (lane == leader)
    {
        uint index = anchor.x + grid.x * (anchor.y + grid.y * anchor.z);
        uint words = (grid.x * grid.y * grid.z + 1) / 2;
        d = LoadBrickDistance(index, words);
    }
    d = QuadReadLaneAt(d, leader);
    if (d == 0 || any(abs(cell - anchor) >= int(d)))
        return z;

    float3 v = VolumeLocal(i, direction) * size / (8. * i.extent);
    int3 lo = anchor - int(d) + 1, hi = anchor + int(d);
    float exitZ = 1e30;
    [unroll] for (int axis = 0; axis < 3; ++axis)
    {
        // Edge bricks include the clamped fringe at the actor boundary.
        if (v[axis] > 0. && hi[axis] < int(grid[axis]))
            exitZ = min(exitZ, (hi[axis] - p[axis]) / v[axis]);
        if (v[axis] < 0. && lo[axis] > 0)
            exitZ = min(exitZ, (lo[axis] - p[axis]) / v[axis]);
    }
    return z + max(exitZ - 1e-5, 0.);
}
#endif

#if VDB_NEAR_RAY_LOD
#include "VdbSharedRay.hlsli"
#endif

#if VDB_RAY_HULL
float4 VdbDiagonals(float3 p)
{
    return p.x + float4(p.y, -p.y, p.y, -p.y) + float4(p.z, p.z, -p.z, -p.z);
}

float2 VdbHullInterval(VolumeVaryings i, float2 uv, float rayZ, float2 bounds)
{
    float3 origin = VolumeLocal(i, Camera[0].xyz - i.center) / (2. * i.extent) + .5;
    float3 slope = VolumeLocal(i, WorldRay(uv)) / (2. * i.extent * rayZ);
    float4 diagonalOrigin = VdbDiagonals(origin), diagonalSlope = VdbDiagonals(slope);
    if (VdbAxisLo.x > VdbAxisHi.x)
        return float2(1., 0.); // Empty animation frame.
    [unroll] for (uint k = 0; k < 7; ++k)
    {
        float2 slab = k < 3 ? float2(VdbAxisLo[k], VdbAxisHi[k]) : float2(VdbDiagonalLo[k-3], VdbDiagonalHi[k-3]);
        float p = k < 3 ? origin[k] : diagonalOrigin[k-3];
        float d = k < 3 ? slope[k] : diagonalSlope[k-3];
        slab += float2(-1., 1.) * 1e-5; // Round out at grazing or parallel rays.
        if (d == 0.)
        {
            if (p < slab.x || p > slab.y)
                return float2(1., 0.);
        }
        else
        {
            float2 hit = (slab - p) / d;
            bounds = float2(max(bounds.x, min(hit.x, hit.y)), min(bounds.y, max(hit.x, hit.y)));
        }
    }
    return bounds;
}
#endif

#if VDB_GROUP_MARCH
#include "VdbGroupGeometry.hlsli"
float2 GroupInterval(VolumeVaryings i, float2 uv, float rayZ, float2 interval)
{
    float3 lo, hi;
    VdbGroupBounds(i.groupIndex, lo, hi);
    float3 scale = VdbSize / (2. * i.extent);
    float3 p = VolumeLocal(i, Camera[0].xyz - i.center) * scale + .5 * VdbSize;
    float3 v = VolumeLocal(i, WorldRay(uv)) * scale / rayZ;
    // The slab is a conservative scheduling bound. Density taps enforce ownership.
    lo -= .0005;
    hi += .0005;
    [unroll] for (uint axis = 0; axis < 3; ++axis)
    {
        if (v[axis] == 0.)
        {
            if (p[axis] < lo[axis] || p[axis] > hi[axis])
                return float2(1., 0.);
        }
        else
        {
            float2 hit = (float2(lo[axis], hi[axis]) - p[axis]) / v[axis];
            interval = float2(max(interval.x, min(hit.x, hit.y)), min(interval.y, max(hit.x, hit.y)));
        }
    }
    return interval;
}
#endif

VolumeMarchResult ShadeVolumeQuad(VolumeVaryings i, bool halfResolution)
{
    VolumeMarchResult o = (VolumeMarchResult)0;
    uint2 pixel = uint2(i.position.xy);
    uint2 rasterPixel = halfResolution ? pixel / 2u : pixel;
    float2 uv = i.position.xy / float2(WIDTH, HEIGHT);
    float2 quadUv = (QuadReadLaneAt(uv, 0) + QuadReadLaneAt(uv, 3)) * .5;
    float2 interval;
    float rayZ;
    bool valid = VolumeInterval(i, uv, Camera[5].w, interval, rayZ);
    float stopZ = min(interval.y, LoadOpaqueViewDepth(pixel));
    valid = valid && stopZ > interval.x;
#if VOLUME_COMPUTE
    if (!WaveActiveAnyTrue(valid))
        return o;
#endif
    // Keep exact per-ray Zero-T/opaque clipping semantics at the boundary.
    if (any(SampleVolumeTransmittance(uv, stopZ) > 0.))
        interval.y = stopZ;
    float z = QuadMinimum(valid ? interval.x : 1e30);
    float end = QuadMaximum(valid ? interval.y : -1.);
    if (end <= z)
        return o; // Quad-uniform exit, never a per-ray early return.
    float device = InterfaceDepth.Load(int3(pixel, 0));
    float interfaceZ = device > 0 ? ViewDepthFromDevice(device) : Camera[5].w;
    float centerRayZ = max(dot(WorldRay(quadUv), Camera[3].xyz), 1e-6);
    uint2 np = rasterPixel & 31u;
    float jitter = (VolumeBlueNoise[np.y * 32u + np.x] + .5) / 1024.;
    if (Camera[9].w > 0.)
        jitter = frac(jitter + (Camera[9].w - 1.) * .618033989);
    uint seed = SmokeFilterSeed(pixel);
    uint budget = max(uint(Camera[10].w), 4u);
    uint firstStep = 0;
    float emptyUntil = -1., recheckAt = -1.;
#if VDB_GROUP_MARCH
    float2 group = GroupInterval(i, uv, rayZ, interval);
    stopZ = valid && group.y >= group.x ? min(stopZ, group.y) : -1.;
    if (QuadMaximum(stopZ) <= z)
        return o;
    emptyUntil = group.x;
#if QUAD_T == 1
    // Start on an actor block boundary before this group. Replaying from the
    // actor entry is required if T changes the step size or an interface cuts a block.
    float fixedDz = Camera[10].x * centerRayZ;
    float limit = QuadMinimum(stopZ > z ? group.x : end);
    if (Camera[10].z == 1. && fixedDz >= (end - z) / budget &&
        QuadMinimum(valid && interfaceZ > z ? interfaceZ : end) >= limit)
    {
        uint blocks = min(uint(max(0., floor((limit - z) / (4. * fixedDz)))), budget / 4u);
        if (blocks && z + blocks * (4. * fixedDz) > limit)
            --blocks;
        z += blocks * (4. * fixedDz);
        firstStep = 4u * blocks;
    }
#endif
#endif
#if VDB_RAY_HULL
    // Keep the original AABB lattice, T anchors, budget and interface cuts.
    // The hull proves an empty prefix and a stopping point, never a new sample origin.
    float2 hull = VdbHullInterval(i, uv, rayZ, float2(interval.x, stopZ));
    stopZ = valid && hull.y >= hull.x ? hull.y : -1.;
    if (QuadMaximum(stopZ) <= z)
        return o;
    recheckAt = emptyUntil = hull.x;
#endif
    float3 stats = 0.;
    float3 t0 = SharedVolumeT(quadUv, z);
#if QUAD_T == 2
    VolumeTAnchors anchors;
    anchors.start = z;
    anchors.length = max(end - z, 1e-10);
    anchors.cell = 0;
    anchors.lower = t0;
    anchors.upper = SharedVolumeT(quadUv, z + anchors.length / 7.);
#endif
#if VDB_NEAR_RAY_LOD
    float sharedZ, sharedEnd;
    bool sharedBehind;
    if (CoherentVdbInterval(interval, stopZ, valid, interfaceZ, centerRayZ, sharedZ, sharedEnd, sharedBehind))
    {
        uint lane = (rasterPixel.x & 1u) + 2u * (rasterPixel.y & 1u);
        float3 sharedT = t0;
        if (sharedZ != z)
            sharedT = SharedVolumeT(quadUv, sharedZ);
        MarchVdbLodPhase(4, .20, i, uv, quadUv, quadUv, rayZ, centerRayZ, sharedEnd, sharedBehind,
                         lane, rasterPixel, budget, sharedZ, firstStep, sharedT, o, stats);
        // Both diagonals have the same midpoint. Offset each representative halfway
        // toward one endpoint. Alternate that endpoint between neighboring quads.
        uint owner = lane < 2u ? lane : 3u - lane;
        if (((rasterPixel.x / 2u ^ rasterPixel.y / 2u) & 1u) != 0)
            owner = 3u - owner;
        float2 oppositeUv = QuadReadAcrossDiagonal(uv);
        float2 pairUv = lerp(quadUv, owner == lane ? uv : oppositeUv, .5);
        MarchVdbLodPhase(2, .55, i, uv, pairUv, quadUv, rayZ, centerRayZ, sharedEnd, sharedBehind,
                         lane, rasterPixel, budget, sharedZ, firstStep, sharedT, o, stats);
        if (firstStep != 0)
        {
            z = sharedZ;
            t0 = sharedT;
            if (ReachedLocalOpacity(o.totalTau) || z >= sharedEnd)
                stopZ = min(stopZ, z);
            if (z >= QuadMaximum(stopZ) || firstStep >= budget)
            {
                if (MarchStats)
                    o.numerator = stats;
                return o;
            }
        }
    }
#endif
    // Up to four interface cuts may split a block without consuming the tail.
    [loop] for (uint step = firstStep; step < budget + 16u && z < end; step += 4u)
    {
#if SPECIALIZED_VDB && !(VDB_GROUP_MARCH && QUAD_T == 1)
        // Only jump on a fixed lattice. With T-dependent step growth, omitted
        // queries could change subsequent sample positions even in empty space.
        if (Camera[10].z == 1. && step < budget)
        {
#if QUAD_T == 3 && !VDB_GROUP_MARCH
            uint lane = (rasterPixel.x & 1u) + 2u * (rasterPixel.y & 1u);
            bool needsProof = valid && z < stopZ && max(z, interval.x) >= recheckAt;
            uint leader = uint(QuadMinimum(needsProof ? float(lane) : 4.));
            if (leader < 4 && VdbSkippingEnabled())
            {
                float boxEnd = QuadEmptyBoxEnd(i, uv, rayZ, max(z, interval.x), leader, lane);
                if (needsProof && boxEnd > max(z, interval.x))
                {
                    emptyUntil = boxEnd;
                    recheckAt = boxEnd;
                }
            }
#endif
            // Reuse the conservative per-ray brick proof from the previous
            // block. Rays outside their actor/depth interval cannot contribute.
            float laneEmptyEnd = !valid || z >= stopZ ? end : max(interval.x, emptyUntil);
            float emptyEnd = QuadMinimum(laneEmptyEnd);
            float nextInterface = QuadMinimum(valid && interfaceZ > z ? interfaceZ : end);
            float limit = min(emptyEnd, min(nextInterface, end));
            float fixedDz = Camera[10].x * centerRayZ;
            // Budget-driven coarsening is not a fixed lattice either.
            if (fixedDz >= (end - z) / float(budget - step))
            {
                float blockSize = 4. * fixedDz;
                uint blocks = min(uint(max(floor((limit - z) / blockSize), 0.)), (budget - step) / 4u);
                float jumpZ = z + float(blocks) * blockSize;
                // Rounding must never take the jump outside the proven prefix.
                if (jumpZ > limit && blocks > 0)
                {
                    --blocks;
                    jumpZ = z + float(blocks) * blockSize;
                }
                if (blocks >= 2u && jumpZ > z)
                {
                    if (MarchStats && valid)
                    {
                        float first = max(floor((interval.x - z) / fixedDz), 0.);
                        float last = min(ceil((stopZ - z) / fixedDz), float(blocks * 4u));
                        float skipped = max(last - first, 0.);
                        stats.x += skipped;
                        stats.z += skipped;
                    }
                    z = jumpZ;
                    step += blocks * 4u; // Empty steps still consume the original budget and RNG indices.
                    if (z >= end || step >= budget + 16u)
                        break;
#if QUAD_T == 2
                    t0 = InterpolateVolumeT(quadUv, z, anchors);
#else
                    t0 = SharedVolumeT(quadUv, z);
#endif
                }
            }
        }
#endif
        float visibleT = max(t0.r, max(t0.g, t0.b));
        float lowT = 1. - smoothstep(0., Camera[10].y, visibleT);
        float dz = Camera[10].x * centerRayZ * lerp(1., Camera[10].z, lowT);
        dz = max(dz, (end - z) / float(max(int(budget) - int(step), 4)));
        float blockEnd = min(z + 4. * dz, end);
        // An explicitly selected interface is a shared interpolation boundary.
        float cut = QuadMinimum(valid && interfaceZ > z && interfaceZ < blockEnd ? interfaceZ : blockEnd);
        blockEnd = min(blockEnd, cut);
#if QUAD_T == 2
        float3 t1 = InterpolateVolumeT(quadUv, blockEnd, anchors);
#else
        float3 t1 = SharedVolumeT(quadUv, blockEnd);
#endif
        [loop] for (uint sub = 0; sub < 4u; ++sub)
        {
            float a = lerp(z, blockEnd, float(sub) * .25);
            float b = lerp(z, blockEnd, float(sub + 1u) * .25);
            if (valid && a < stopZ && b > interval.x)
            {
                float2 segment = float2(max(a, interval.x), min(b, interval.y));
                // Clipping and substep rounding can produce a zero-length interval.
                float count = float(segment.y > segment.x);
                stats.x += count;
#if VDB_NEAR_RAY_LOD && MARCH_COUNTERS
                o.lodMask |= 1u;
#endif
#if !VDB_GROUP_MARCH
                if (segment.x >= recheckAt)
                {
                    float recheckDistance;
                    emptyUntil = segment.x + VolumeEmptyRun(i, uv, segment.x, rayZ, recheckDistance);
                    recheckAt = max(emptyUntil, segment.x + recheckDistance);
                }
#endif
                if (segment.y <= emptyUntil)
                {
                    stats.z += count;
                    continue; // No quad operation occurs inside this divergent sub-loop.
                }
                float split = clamp(interfaceZ, segment.x, segment.y);
                float invRange = rcp(max(blockEnd - z, 1e-10));
                uint filterSeed = seed ^ ((step + sub) * 0x85ebca6bu);
#if SMOKE_MODE < 6
                float beginFraction = saturate((segment.x - z) * invRange);
                float endFraction = saturate((segment.y - z) * invRange);
                float splitFraction = saturate((split - z) * invRange);
                AccumulateVolumeSegment(float2(segment.x, split), jitter, rayZ, i, false, true, uv, t0, t1, o, stats.y,
                                        filterSeed, float2(beginFraction, splitFraction));
                AccumulateVolumeSegment(float2(split, segment.y), jitter, rayZ, i, true, true, uv, t0, t1, o, stats.y,
                                        filterSeed ^ 0xc2b2ae35u, float2(splitFraction, endFraction));
#else
                // Procedural gameplay smoke measures faster with endpoint RGB
                // than with deferred interpolation. Specialize at PSO compile.
                float3 beginT = lerp(t0, t1, saturate((segment.x - z) * invRange));
                float3 endT = lerp(t0, t1, saturate((segment.y - z) * invRange));
                float3 splitT = lerp(t0, t1, saturate((split - z) * invRange));
                AccumulateVolumeSegment(float2(segment.x, split), jitter, rayZ, i, false, true, uv, beginT, splitT, o,
                                        stats.y, filterSeed);
                AccumulateVolumeSegment(float2(split, segment.y), jitter, rayZ, i, true, true, uv, splitT, endT, o,
                                        stats.y, filterSeed ^ 0xc2b2ae35u);
#endif
                // Retire this ray immediately. Other lanes may still need the
                // next block's shared lookup, including its broadcast leader.
                if (ReachedLocalOpacity(o.totalTau))
                    stopZ = min(stopZ, b);
            }
        }
        if (blockEnd >= QuadMaximum(valid ? stopZ : -1.))
            break;
        z = blockEnd;
        t0 = t1;
    }
    if (MarchStats)
        o.numerator = stats;
    return o;
}
#endif
VolumeMarchResult ShadeVolume(VolumeVaryings i, bool textured, bool halfResolution = false)
{
#if QUAD_T
    if (textured)
        return ShadeVolumeQuad(i, halfResolution);
#endif

    VolumeMarchResult o = (VolumeMarchResult)0;
    uint2 pixel = uint2(i.position.xy);
    float2 uv = i.position.xy / float2(WIDTH, HEIGHT), interval;
    float rayZ;
    if (!VolumeInterval(i, uv, Camera[5].w, interval, rayZ))
        return o;
    float stopZ = min(interval.y, LoadOpaqueViewDepth(pixel));
    if (stopZ <= interval.x)
        return o;
    // A quantized zero frontier may stop subsequent steps, but must not move
    // quadrature/lighting points inside the last fixed-size step. Real opaque
    // surfaces with nonzero transmission still clip the integration interval.
    if (any(SampleVolumeTransmittance(uv, stopZ) > 0.))
        interval.y = stopZ;
    float interfaceDevice = InterfaceDepth.Load(int3(pixel, 0));
    float interfaceZ = interfaceDevice > 0 ? ViewDepthFromDevice(interfaceDevice) : Camera[5].w;
    float baseStep = Camera[10].x;
    uint maxSteps = uint(Camera[10].w);
    // Full-resolution rays stratify depth phases over a quad. Half-resolution
    // rays use independent blue noise: fixed quad phases become visible as an
    // axis-aligned pattern after upsampling high-extinction smoke.
    uint2 marchPixel = halfResolution ? pixel / 2u : pixel;
    uint2 noisePixel = (halfResolution ? marchPixel : marchPixel / 2u) & 31u;
    float jitter = (VolumeBlueNoise[noisePixel.y * 32u + noisePixel.x] + .5) / 1024.;
    if (Camera[9].w > 0.)
        jitter = frac(jitter + (Camera[9].w - 1.) * .618033989);
    if (!halfResolution)
    {
        uint phase = (marchPixel.x & 1u) * 2u;
        phase = (marchPixel.y & 1u) ? 3u - phase : phase; // [0,2;3,1]
        if (Camera[9].w > 0.)
            phase = (phase + uint(Camera[9].w - 1.)) & 3u;
        jitter = (float(phase) + jitter) * .25;
    }
    uint filterSeed = SmokeFilterSeed(pixel);
    float3 stats = 0; // intervals, nonzero-density shading evaluations, skipped intervals
    float emptyUntil = -1., recheckAt = -1.;
    float z = interval.x;
    float3 t = SampleMarchTransmittance(uv, z);
    [loop] for (uint step = 0; step < maxSteps && z < stopZ; ++step)
    {
        float visibleT = max(t.r, max(t.g, t.b));
        float lowT = 1. - smoothstep(0., Camera[10].y, visibleT);
        float ds = baseStep * lerp(1., Camera[10].z, lowT);
        // A budget limit coarsens the remaining interval rather than dropping
        // its density/tau and accidentally making long volumes transparent.
        ds = max(ds, (interval.y - z) / (rayZ * float(maxSteps - step)));
        float endZ = min(z + ds * rayZ, interval.y);
        float count = float(endZ > z);
        if (textured && z >= recheckAt)
        {
            float recheckDistance;
            emptyUntil = z + VolumeEmptyRun(i, uv, z, rayZ, recheckDistance);
            recheckAt = max(emptyUntil, z + recheckDistance);
        }
        if (textured && endZ <= emptyUntil)
        {
            // Keep the exact original integration lattice and budget. T can
            // change inside empty smoke due to other transparent actors.
            stats.x += count;
            stats.z += count;
            z = endZ;
            t = SampleMarchTransmittance(uv, z);
            continue;
        }
        float3 endT = SampleMarchTransmittance(uv, endZ);
        stats.x += count;
        // Exact RGB equality is only a quantized empty-space heuristic.
        // Never skip saturated channels: flat zero T does not imply zero density,
        // and this pass still owns the totalTau needed by the common resolve.
        [branch] if (LutIntervalSkip && all(t == endT) && all(t > 0.))
        {
            stats.z += count;
            z = endZ;
            t = endT;
            continue;
        }
        float split = clamp(interfaceZ, z, endZ);
        float3 splitT = split <= z ? t : (split >= endZ ? endT : SampleMarchTransmittance(uv, split));
        AccumulateVolumeSegment(float2(z, split), jitter, rayZ, i, false, textured, uv, t, splitT, o, stats.y,
                                filterSeed ^ (step * 0x85ebca6bu));
        AccumulateVolumeSegment(float2(split, endZ), jitter, rayZ, i, true, textured, uv, splitT, endT, o, stats.y,
                                filterSeed ^ (step * 0x85ebca6bu) ^ 0xc2b2ae35u);
        if (ReachedLocalOpacity(o.totalTau))
            break;
        z = endZ;
        t = endT;
    }
    if (MarchStats)
        o.numerator = stats;
    return o;
}

// Smoke carries a single tau through the march and expands it only for the shared MRT.
struct VolumeAccumulation
{
    float3 numerator : SV_Target0;
    float3 denominator : SV_Target1;
    float3 totalTau : SV_Target2;
    float3 backNumerator : SV_Target3;
};
VolumeAccumulation AccumulationTargets(VolumeMarchResult result)
{
    VolumeAccumulation targets = {result.numerator, result.denominator, (float3)result.totalTau, result.backNumerator};
    return targets;
}
[earlydepthstencil] VolumeAccumulation volumeAccumulation(VolumeVaryings i)
{
    return AccumulationTargets(ShadeVolume(i, false));
}
[earlydepthstencil] VolumeAccumulation texturedAccumulation(VolumeVaryings i)
{
    return AccumulationTargets(ShadeVolume(i, true));
}

// Match guide texels used by the bilateral reconstruction. Full-resolution
// depth is sampled explicitly; no mismatched full-size DSV is bound here.
VolumeAccumulation volumeAccumulationHalf(VolumeVaryings i)
{
    i.position.xy = floor(i.position.xy) * 2. + 1.5;
    return AccumulationTargets(ShadeVolume(i, false, true));
}
VolumeAccumulation texturedAccumulationHalf(VolumeVaryings i)
{
    i.position.xy = floor(i.position.xy) * 2. + 1.5;
    VolumeMarchResult result = ShadeVolume(i, true, true);
#if VDB_GROUP_MARCH
    if (result.totalTau == 0.)
        discard;
#endif
    return AccumulationTargets(result);
}
