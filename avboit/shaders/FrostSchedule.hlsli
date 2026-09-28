// SPDX-License-Identifier: MIT
// Called from bounds occupancy, once per actor. Mark producer masks directly;
// no rescan, compaction, counters, indirect arguments or preparation dispatch.
Buffer<float4> Camera : register(t15);
Buffer<float4> FrostFootprints : register(t16);
RWBuffer<uint> FrostWorkMask : register(u2);
RWBuffer<uint> FrostRectangles : register(u5); // diagnostic envelopes only
#include "TileSchedule.hlsli"

void Envelope(uint stage, int4 rect)
{
    InterlockedMin(FrostRectangles[stage * 4], uint(rect.x));
    InterlockedMin(FrostRectangles[stage * 4 + 1], uint(rect.y));
    InterlockedMax(FrostRectangles[stage * 4 + 2], uint(rect.z));
    InterlockedMax(FrostRectangles[stage * 4 + 3], uint(rect.w));
}
void MarkGaussian(uint level, int4 rect, uint lane)
{
    uint stage = level * 2 + 1;
    uint2 size = TileStageSize(stage);
    rect = int4(clamp(rect.xy, 0, int2(size)), clamp(rect.zw, 0, int2(size)));
    uint2 first = uint2(rect.xy) / 4, last = (uint2(rect.zw) + 3) / 4;
    if (any(last <= first))
        return;
    if (lane == 0)
        Envelope(stage, int4(first * 4, min(last * 4, size)));
    uint2 extent = last - first;
    uint base = TileStageOffset(stage), pitch = (size.x + 3) / 4;
    for (uint i = lane; i < extent.x * extent.y; i += 64)
    {
        uint2 tile = first + uint2(i % extent.x, i / extent.x);
        InterlockedOr(FrostWorkMask[base + 1 + tile.y * pitch + tile.x], 1u);
    }
}
void ScheduleFrost(int4 target, uint flags, float displacement, uint lane)
{
    if ((flags & 2u) == 0)
        return;
    float2 size = Camera[6].xy;
    uint top = min(flags >> 3, uint(Camera[8].z) - 1);
    for (uint level = 0; level <= top; ++level)
    {
        float2 halo = displacement + FrostFootprints[top * 5 + level].xy;
        float2 mipSize = TileStageSize(level * 2 + 1);
        MarkGaussian(level, int4(floor((target.xy - halo) * mipSize / size), ceil((target.zw + halo) * mipSize / size)),
                     lane);
    }
}
