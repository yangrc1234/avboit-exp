// SPDX-License-Identifier: MIT
// One wave chooses a single execution mode. Inside the wave, each 2x2 screen
// quad owns either one center ray, two diagonal-pair rays, or four pixel rays.
float VdbLodSum(float x, uint width)
{
    if (width == 2)
        return x + QuadReadAcrossDiagonal(x);
    x += QuadReadAcrossX(x);
    return x + QuadReadAcrossY(x);
}
float3 VdbLodSum(float3 x, uint width)
{
    if (width == 2)
        return x + QuadReadAcrossDiagonal(x);
    x += QuadReadAcrossX(x);
    return x + QuadReadAcrossY(x);
}

bool CoherentVdbInterval(float2 interval, float stopZ, bool valid, float interfaceZ, float centerRayZ,
                         out float start, out float end, out bool behind)
{
    start = QuadMaximum(interval.x);
    end = QuadMinimum(stopZ);
    float tolerance = .25 * Camera[10].x * centerRayZ;
    bool coherent = start - QuadMinimum(interval.x) <= tolerance && QuadMaximum(stopZ) - end <= tolerance;
    behind = QuadMaximum(interfaceZ) <= start;
    bool inFront = QuadMinimum(interfaceZ) >= end;
    return WaveActiveAllTrue(valid && stopZ >= interval.y && end > start && coherent && (behind || inFront));
}

// Consume a prefix of the same four-step lattice as the independent marcher.
// z and step always point to the first unprocessed block. Per-ray accumulated
// values are broadcast to their owning pixels once, at the end of each phase.
void MarchVdbLodPhase(uint width, float maxRadius, VolumeVaryings actor, float2 pixelUv, float2 rayUv,
                      float2 lutUv, float pixelRayZ, float centerRayZ, float end, bool behind, uint lane,
                      uint2 rasterPixel, uint budget, inout float z, inout uint step, inout float3 t0,
                      inout VolumeMarchResult result, inout float3 stats)
{
    float rayZ = max(dot(WorldRay(rayUv), Camera[3].xyz), 1e-6);
    float3 delta = WorldRay(pixelUv) / pixelRayZ - WorldRay(rayUv) / rayZ;
    float radiusPerZ = length(VolumeLocal(actor, delta) * VdbFootprintSize / (2. * actor.extent));
    // Use the actual distance to each pair's (off-center) representative.
    float depthLimit = maxRadius / max(QuadMaximum(radiusPerZ), 1e-8);
    uint rank = width == 4 ? lane : lane / 2;
    uint2 noise = (rasterPixel / 2u) & 31u;
    if (width == 2 && (lane == 1 || lane == 2))
        noise ^= uint2(13, 7);
    float jitter = (VolumeBlueNoise[noise.y * 32u + noise.x] + .5) / 1024.;
    if (Camera[9].w > 0.)
        jitter = frac(jitter + (Camera[9].w - 1.) * .618033989);

    VolumeMarchResult partial = (VolumeMarchResult)0;
    float3 localStats = 0.;
    float runningTau = result.totalTau;
    float emptyUntil = -1., recheckAt = -1.;
    [loop] while (true)
    {
        bool live = !ReachedLocalOpacity(runningTau);
        bool activeQuad = z < end && step < budget && QuadMaximum(float(live)) > 0.;
        float lowT = 1. - smoothstep(0., Camera[10].y, max(t0.x, max(t0.y, t0.z)));
        float dz = max(Camera[10].x * centerRayZ * lerp(1., Camera[10].z, lowT),
                       (end - z) / float(max(int(budget) - int(step), 4)));
        float blockEnd = min(z + 4. * dz, end);
        if (!WaveActiveAnyTrue(activeQuad) || WaveActiveAnyTrue(activeQuad && blockEnd > depthLimit))
            break;
        if (activeQuad)
        {
            if (live && z >= recheckAt)
            {
                float distance;
                emptyUntil = z + VolumeEmptyRun(actor, rayUv, z, rayZ, distance);
                recheckAt = max(emptyUntil, z + distance);
            }
            float emptyEnd = min(QuadMinimum(live ? emptyUntil : end), min(depthLimit, end));
            uint blocks = Camera[10].z == 1. ? uint(floor(max(emptyEnd - z, 0.) / (4. * dz))) : 0u;
            blocks = min(blocks, (budget - step) / 4u);
            if (blocks > 0 && z + blocks * (4. * dz) > emptyEnd)
                --blocks;
            if (blocks > 0)
            {
                if (live)
                    localStats.xz += float(blocks * (4u / width));
                z += blocks * (4. * dz);
                step += blocks * 4u;
                t0 = SharedVolumeT(lutUv, z);
            }
            else
            {
                float3 t1 = SharedVolumeT(lutUv, blockEnd);
                [unroll] for (uint first = 0; first < 4; first += width)
                {
                    float2 fractions = float2(first + rank, first + rank + 1) * .25;
                    float2 segment = lerp(z, blockEnd, fractions);
                    VolumeMarchResult sample = (VolumeMarchResult)0;
                    float shaded = 0.;
                    bool empty = segment.y <= emptyUntil;
                    if (live && !empty)
                        AccumulateVolumeSegment(segment, jitter, rayZ, actor, false, true, rayUv,
                                                t0, t1, sample, shaded, 0u, fractions);
                    float sumTau = VdbLodSum(sample.totalTau, width);
                    bool last = ReachedLocalOpacity(runningTau + sumTau);
                    bool keep = live;
                    if (last)
                    {
                        float preceding = 0.;
                        if (width == 2)
                        {
                            float partnerTau = QuadReadAcrossDiagonal(sample.totalTau);
                            preceding = rank == 1 ? partnerTau : 0.;
                        }
                        else
                        {
                            float a = QuadReadLaneAt(sample.totalTau, 0);
                            float b = QuadReadLaneAt(sample.totalTau, 1);
                            float c = QuadReadLaneAt(sample.totalTau, 2);
                            preceding = (rank >= 1 ? a : 0.) + (rank >= 2 ? b : 0.) + (rank >= 3 ? c : 0.);
                        }
                        keep = live && !ReachedLocalOpacity(runningTau + preceding);
                    }
                    if (keep)
                    {
                        partial.numerator += sample.numerator;
                        partial.denominator += sample.denominator;
                        partial.totalTau += sample.totalTau;
                        localStats += float3(1., shaded, float(empty));
                    }
                    runningTau += sumTau;
                    live = !last;
                }
                z = blockEnd;
                step += 4;
                t0 = t1;
            }
        }
    }
    float3 radiance = VdbLodSum(partial.numerator, width);
    result.numerator += radiance;
    result.denominator += VdbLodSum(partial.denominator, width);
    result.totalTau += VdbLodSum(partial.totalTau, width);
    if (behind)
        result.backNumerator += radiance;
    stats += VdbLodSum(localStats, width);
#if MARCH_COUNTERS
    if (VdbLodSum(localStats.x, width) > 0.)
        result.lodMask |= width;
#endif
}
