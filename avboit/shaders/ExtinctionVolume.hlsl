// SPDX-License-Identifier: MIT
#include "TransparencyCommon.hlsli"
Buffer<uint> DepthWarp : register(t1);
Buffer<uint> PhysicalMasks : register(t22);
Texture2D<float4> Opaque : register(t3);
#include "OpaqueDepth.hlsli"
RWBuffer<uint> Extinction : register(u0);
RWBuffer<uint> FirstOverflow : register(u1);
RWTexture3D<float4> Integrated : register(u2);
RWTexture2D<uint> ZeroSlice : register(u3);
RWBuffer<uint> ZeroCandidates : register(u4);
RWBuffer<uint> ZeroDrawArgs : register(u5); // DrawInstanced: vertices, instances, first vertex, first instance
#include "PackedExtinction.hlsli"
#include "DepthMapping.hlsli"
void splatPixel(Varyings i)
{
    // Evaluate derivatives before opacity/opaque rejection. One raster pixel is
    // one LUT XY texel; its bilinear support reaches up to one texel per axis.
    // Device depth is affine on a projected triangle, unlike view/log/warped Z.
    float depthSpan = abs(ddx(i.position.z)) + abs(ddy(i.position.z));
    // Include the optional XY min halo as well as the reconstruction footprint.
    // Keep the existing bounded occupancy guard; no extra Z bins are allocated.
    depthSpan *= 1. + ((uint(Camera[7].w) & 32u) != 0 ? 1. : 0.);
    float logDepth = NormalizedLogDepth(i.depth, 80., 1.);
    if (Camera[9].x != 0 && i.flatOrdering.x <= 0.)
    {
        // Clamp before reciprocal reconstruction to stay inside the far plane.
        float pushedDepth = ViewDepthFromDevice(max(i.position.z - depthSpan, 0.));
        float pushedLogDepth = NormalizedLogDepth(clamp(pushedDepth, Camera[5].z, Camera[5].w), 80., 1.);
        // Bounds occupancy reserves the SAME limit before adaptive compression.
        // No actor-specific shader input or post-warp slice offset is needed.
        logDepth += clamp(pushedLogDepth - logDepth, 0., MaxSlopeSplatPush);
    }
    uint2 pixel = uint2(i.position.xy);
    float2 uv = (float2(pixel) + .5) / float2(VOLUME_WIDTH, VOLUME_HEIGHT);
    if (i.depth >= LoadOpaqueViewDepth(uint2(uv * float2(WIDTH, HEIGHT))))
        discard;
    float3 opacity = Opacity(i);
    if (all(opacity < .00001))
        discard;
    float mapped = i.flatOrdering.x > 0. ? FlattenedOitBin(DepthWarp, i.flatOrdering, SLICE_COUNT - 2)
                                         : MapAdaptiveLogDepth(DepthWarp, logDepth, 0.) - i.cameraOffsetBins;
    SplatExtinction(pixel.y * VOLUME_WIDTH + pixel.x, mapped, TransmittanceToOpticalDepth(1. - opacity),
                    i.scalarExtinction != 0);
}
uint OccupancyTile(uint2 pixel)
{
    return (pixel.y / 8) * ((VOLUME_WIDTH + 7) / 8) + pixel.x / 8;
}

[numthreads(8, 8, 1)] void clearOccupiedExtinction(uint2 pixel : SV_DispatchThreadID)
{
    if (any(pixel >= uint2(VOLUME_WIDTH, VOLUME_HEIGHT)))
        return;
    // Reset in the existing clear dispatch, before any integration group can
    // append. Only the valid list prefix is read, so the list needs no clear.
    if (all(pixel == 0))
    {
        ZeroDrawArgs[0] = 4;
        ZeroDrawArgs[1] = 0;
        ZeroDrawArgs[2] = 0;
        ZeroDrawArgs[3] = 0;
    }
    uint ray = pixel.y * VOLUME_WIDTH + pixel.x, tile = OccupancyTile(pixel);
    for (uint word = 0; word < SLICE_COUNT / 4; ++word)
    {
        uint bits = PhysicalMasks[tile * 8 + word / 8];
        // A scalar DWORD holds four slices. Clear the whole word if any of
        // its payloads may be touched, so integer carries cannot retain dirt.
        if (((bits >> ((word % 8) * 4)) & 15u) != 0)
            Extinction[ray * (SLICE_COUNT / 4) + word] = 0;
    }
    for (uint word = 0; word < 4; ++word)
    {
        uint bits = PhysicalMasks[tile * 8 + 4 + word];
        while (bits != 0)
        {
            uint bit = firstbitlow(bits);
            bits &= bits - 1;
            if (word * 32 + bit < SLICE_COUNT)
                Extinction[SCALAR_WORDS + ray * SLICE_COUNT + word * 32 + bit] = 0;
        }
    }
    // Small per-ray metadata is reset densely; consumers can use it directly.
    for (uint channel = 0; channel < 4; ++channel)
        FirstOverflow[ray * 4 + channel] = SLICE_COUNT;
}

void AppendZeroCandidates(uint2 pixel, uint zeroSlice)
{
    if ((uint(Camera[7].w) & 2u) == 0 || zeroSlice >= SLICE_COUNT)
        return;
    // Each 16x16 screen tile belongs to the ray floor(tile * 16 * volume / screen).
    // That ray is inside the VS's conservative footprint, so an accepted quad
    // must have a zero owner. Invert the mapping with integer ceil division to
    // emit each tile exactly once, even at partial screen/volume edges.
    uint2 screen = uint2(WIDTH, HEIGHT), pitch = 16 * uint2(VOLUME_WIDTH, VOLUME_HEIGHT);
    uint2 tiles = (screen + 15) / 16;
    uint2 first = (pixel * screen + pitch - 1) / pitch;
    uint2 end = min(((pixel + 1) * screen + pitch - 1) / pitch, tiles);
    uint2 extent = end - first;
    uint count = extent.x * extent.y;
    if (count == 0)
        return;
    uint slot;
    InterlockedAdd(ZeroDrawArgs[1], count, slot);
    for (uint y = first.y; y < end.y; ++y)
        for (uint x = first.x; x < end.x; ++x)
            ZeroCandidates[slot++] = y * tiles.x + x;
}

// A 10x10 group integrates 8x8 output columns plus a one-column halo.
// Each lane retains its own integral. Only integrated T is shared/reduced;
// never feed a neighbour minimum back into the next Z integration step.
groupshared float3 IntegratedTile[100];
void Integrate(uint2 pixel, bool sparse, bool minFilter, uint2 lane)
{
    bool writePixel = all(pixel < uint2(VOLUME_WIDTH, VOLUME_HEIGHT));
    if (minFilter)
        writePixel = writePixel && all(lane >= 1) && all(lane <= 8);
    else if (!writePixel)
        return;
    // Halo and partial output groups must participate in every barrier.
    uint2 source = min(pixel, uint2(VOLUME_WIDTH - 1, VOLUME_HEIGHT - 1));
    uint ray = source.y * VOLUME_WIDTH + source.x, tile = OccupancyTile(source);
    float3 integral = 0, t = 1;
    bool saturated = false;
    uint zeroSlice = SLICE_COUNT;
    uint4 scalarBits = 0xffffffffu, rgbBits = 0xffffffffu;
    if (sparse)
        for (uint word = 0; word < 4; ++word)
        {
            scalarBits[word] = PhysicalMasks[tile * 8 + word];
            rgbBits[word] = PhysicalMasks[tile * 8 + 4 + word];
        }
    // Sampling cannot address past the mapped endpoint. Keep one extra slice
    // for floating-point/trilinear footprint conservatism at that endpoint.
    uint limit = sparse ? min(uint(SLICE_COUNT), DepthWarp[1] + 2) : uint(SLICE_COUNT);
    uint4 overflowDepth = uint4(FirstOverflow[ray * 4], FirstOverflow[ray * 4 + 1], FirstOverflow[ray * 4 + 2],
                                FirstOverflow[ray * 4 + 3]);
    for (uint slice = 0; slice < limit; ++slice)
    {
        uint scalarMask = scalarBits[slice / 32];
        uint rgbMask = rgbBits[slice / 32];
        uint bit = 1u << (slice % 32);
        bool scalarActive = (scalarMask & bit) != 0, rgbActive = (rgbMask & bit) != 0;
        if (!saturated && (scalarActive || rgbActive))
        {
            uint scalar =
                scalarActive ? (Extinction[ray * (SLICE_COUNT / 4) + slice / 4] >> ((slice % 4) * 8)) & 255u : 0;
            uint packed = rgbActive ? Extinction[SCALAR_WORDS + ray * SLICE_COUNT + slice] : 0;
            uint3 color = uint3(packed & 1023u, (packed >> 10) & 1023u, (packed >> 20) & 1023u);
            integral += DecodeOpticalDepth(float(scalar) + float3(color), ExtinctionTauMax, 255u);
            t = OpticalDepthToTransmittance(integral);
        }
        if (slice >= overflowDepth.x)
            t = 0;
        if (slice >= overflowDepth.y)
            t.r = 0;
        if (slice >= overflowDepth.z)
            t.g = 0;
        if (slice >= overflowDepth.w)
            t.b = 0;
        // RGBA8 would round every component to zero. Remaining extinction is
        // nonnegative, so no later sample can become nonzero again.
        if (all(t < (.5 / 255.)))
        {
            if (sparse)
            {
                t = 0;
                saturated = true;
            }
        }
        // Empty slices still receive the carried transmittance. Leaving them
        // unwritten would leak previous-frame data through linear filtering.
        float3 outputT = t;
        // Bound added optical depth relative to the ORIGINAL centre, not the
        // running minimum. Retain=0 reproduces the previous absolute-floor filter.
        float3 neighbourFloor = max(16. / 255., t * saturate(Camera[11].w));
        if (minFilter)
        {
            IntegratedTile[lane.y * 10 + lane.x] = t;
            GroupMemoryBarrierWithGroupSync();
            if (writePixel)
            {
                [unroll] for (int y = -1; y <= 1; ++y)
                {
                    [unroll] for (int x = -1; x <= 1; ++x)
                    {
                        // Do not spread a neighbour's quantized zero into a ray
                        // whose own extinction has not closed. Sixteen UNORM8 codes
                        // leave headroom for packed accumulation and preserve Z monotonicity.
                        // outputT starts at the unmodified centre: its real zeros survive.
                        float3 neighbour = IntegratedTile[(int(lane.y) + y) * 10 + int(lane.x) + x];
                        outputT = min(outputT, max(neighbour, neighbourFloor));
                    }
                }
            }
            GroupMemoryBarrierWithGroupSync();
        }
        if (writePixel)
        {
            if (all(outputT < (.5 / 255.)))
                zeroSlice = min(zeroSlice, slice);
            Integrated[uint3(pixel, slice)] = float4(outputT, 1);
        }
    }
    // Same threshold as quantized RGB LUT; sentinel means the ray never closed.
    if (writePixel)
    {
        ZeroSlice[pixel] = zeroSlice;
        AppendZeroCandidates(pixel, zeroSlice);
    }
}
[numthreads(8, 8, 1)] void integrateLut(uint2 pixel : SV_DispatchThreadID)
{ Integrate(pixel, true, false, 0); }[numthreads(8, 8, 1)] void integrateDenseLut(uint2 pixel : SV_DispatchThreadID)
{
    Integrate(pixel, false, false, 0);
}
[numthreads(10, 10, 1)] void integrateMinLut(uint2 group : SV_GroupID, uint2 lane : SV_GroupThreadID)
{
    Integrate(uint2(max(int2(group * 8 + lane) - 1, 0)), true, true, lane);
}[numthreads(10, 10, 1)] void integrateDenseMinLut(uint2 group : SV_GroupID, uint2 lane : SV_GroupThreadID)
{
    Integrate(uint2(max(int2(group * 8 + lane) - 1, 0)), false, true, lane);
}
