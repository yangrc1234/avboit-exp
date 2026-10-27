// SPDX-License-Identifier: MIT
// One group per view-space conservative AABB. Projection is shared by OIT
// occupancy and frost marking; material shading is deliberately not evaluated.
// Header: (width,height,oitTilePixels,blurTilePixels),
//         (projectionScaleX,projectionScaleY,nearDepth,farDepth),
//         (logLinearization,objectCount,0,0).
// Per object: (viewMin.xyz,flags), (viewMax.xyz,reserved).
// Flags: 1 RGB extinction, 2 frost, 4 sharp refraction; bits 3+ are the maximum mip.
// Only the first Gaussian level is masked, directly from actor bounds.
// All rectangles are half-open pixel intervals, without pixel borders.
Buffer<float4> Bounds : register(t0);
RWBuffer<uint> Masks : register(u0);
RWTexture2D<uint> ScreenTiles : register(u1); // frost=1, sharp=2, transparency/B cache=4
#include "FrostSchedule.hlsli"
#include "DepthMapping.hlsli"
#define VIRTUAL_SLICES 2048
#define WORDS_PER_TILE 64

groupshared int4 projectedRectangle;
groupshared uint2 sliceRange;
groupshared uint objectFlags;
groupshared uint rejected;

uint DepthSlice(float z, float nearLog, float farZ)
{
    return min(2047u, uint(NormalizedLogDepth(z, farZ, nearLog) * 2048.));
}

void RasterBounds(uint3 group, uint lane, bool schedule)
{
    float4 screen = Bounds[0], projection = Bounds[1];
    uint2 oitSize = uint2(ceil(screen.xy / screen.z));
    // Low-resolution raster spans the entire viewport, including partial edge tiles.
    float2 oitPitch = screen.xy / ceil(screen.xy / (screen.z / 8.)) * 8.;
    uint2 blurSize = uint2(ceil(screen.xy / screen.w));
    uint tileCount = oitSize.x * oitSize.y;
    uint rgbOffset = VIRTUAL_SLICES + tileCount * WORDS_PER_TILE;

    if (lane == 0)
    {
        float4 lower = Bounds[3 + group.x * 2], upper = Bounds[4 + group.x * 2];
        objectFlags = uint(lower.w);
        rejected = upper.z < projection.z || lower.z > projection.w;
        // Near-plane crossing alone does not imply intersection with the view.
        // Maximize each side-plane equation over the AABB.
        float2 sideZ = upper.z.xx;
        rejected |= any(sideZ + projection.xy * upper.xy < -1e-5) || any(sideZ - projection.xy * lower.xy < -1e-5);
        float2 low = float2(0, 0), high = screen.xy;
        // Near-plane crossing uses a full-screen conservative fallback.
        if (lower.z > projection.z)
        {
            low = float2(1e30, 1e30);
            high = -low;
            for (uint corner = 0; corner < 8; ++corner)
            {
                float3 v = float3((corner & 1) ? upper.x : lower.x, (corner & 2) ? upper.y : lower.y,
                                  (corner & 4) ? upper.z : lower.z);
                float2 pixel = (v.xy / v.z * projection.xy * float2(.5, -.5) + .5) * screen.xy;
                low = min(low, pixel);
                high = max(high, pixel);
            }
        }
        projectedRectangle = int4(floor(low), ceil(high));
        // Sampling expansion is only needed for potentially visible surfaces.
        sliceRange =
            uint2(DepthSlice(lower.z, Bounds[2].x, projection.w), DepthSlice(upper.z, Bounds[2].x, projection.w));
        // Expand only Z. Both global warp occupancy and per-tile scalar/RGB masks
        // consume this interval, so sparse clearing/integration cover every push.
        if (Camera[9].x != 0)
            sliceRange.y = min(VIRTUAL_SLICES - 1u, sliceRange.y + SlopeSplatGuardSlices);
    }
    GroupMemoryBarrierWithGroupSync();
    if (rejected != 0)
        return;
    if (schedule)
        ScheduleFrost(projectedRectangle, objectFlags, lane);
    // XY projection performed once. Tile raster uses one conservative rectangle.
    int2 first = max(int2(0, 0), int2(floor(float2(projectedRectangle.xy) / oitPitch)));
    int2 end = min(int2(oitSize), int2(ceil(float2(projectedRectangle.zw) / oitPitch)));
    uint2 extent = uint2(max(end - first, 0));
    uint xyCount = extent.x * extent.y;
    if (xyCount > 0)
    {
        for (uint z = sliceRange.x + lane; z <= sliceRange.y; z += 64)
            InterlockedOr(Masks[z], 1u);
        uint firstWord = sliceRange.x / 32, lastWord = sliceRange.y / 32;
        uint words = lastWord - firstWord + 1;
        uint base = (objectFlags & 1) ? rgbOffset : VIRTUAL_SLICES;
        for (uint work = lane; work < xyCount * words; work += 64)
        {
            uint xy = work / words, word = firstWord + work % words;
            uint2 tile = uint2(first) + uint2(xy % extent.x, xy / extent.x);
            uint lowBit = word == firstWord ? sliceRange.x % 32 : 0;
            uint highBit = word == lastWord ? sliceRange.y % 32 : 31;
            uint bits = (0xffffffffu << lowBit) & (0xffffffffu >> (31 - highBit));
            InterlockedOr(Masks[base + (tile.y * oitSize.x + tile.x) * WORDS_PER_TILE + word], bits);
        }
    }
    // Includes zero-extinction special interfaces: their resolve is still needed.
    int2 screenFirst = max(int2(0, 0), int2(floor(float2(projectedRectangle.xy) / screen.w)));
    int2 screenEnd = min(int2(blurSize), int2(ceil(float2(projectedRectangle.zw) / screen.w)));
    uint2 screenExtent = uint2(max(screenEnd - screenFirst, 0));
    // Diagnostic envelope only. B prime draws these exact tiles.
    if (schedule && lane == 0 && all(screenExtent > 0))
        Envelope(10, int4(screenFirst * 64, min(screenEnd * 64, int2(screen.xy))));
    for (uint work = lane; work < screenExtent.x * screenExtent.y; work += 64)
    {
        uint2 tile = uint2(screenFirst) + uint2(work % screenExtent.x, work / screenExtent.x);
        uint bits = 4u | ((objectFlags & 2) != 0 ? 1u : 0u) | ((objectFlags & 4) != 0 ? 2u : 0u);
        InterlockedOr(ScreenTiles[tile], bits);
    }
}

// The standalone entry is used by the AVBOIT occupancy unit test.
[numthreads(64, 1, 1)] void main(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{ RasterBounds(group, lane, false); }[numthreads(64, 1, 1)] void withFrost(uint3 group : SV_GroupID,
                                                                           uint lane : SV_GroupIndex)
{
    if (group.x == 0)
    {
        uint flags = uint(Bounds[2].z);
        if ((flags & 1u) != 0)
            ScheduleFrost(int4(0, 0, Camera[6].xy), 2u | (uint(Bounds[2].w) << 3), lane);
    }
    RasterBounds(group, lane, true);
}
