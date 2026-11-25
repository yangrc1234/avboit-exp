// SPDX-License-Identifier: MIT
#define VOLUME_COMPUTE 1
#define SMOKE_MODE 4
#include "LocalVolumeAccumulation.hlsl"

cbuffer MarchDispatch : register(b4)
{
    uint FirstVertex;
    uint PixelStride;
};
RWTexture2D<float4> MarchN : register(u0);
RWTexture2D<float4> MarchD : register(u1);
RWTexture2D<float4> MarchTau : register(u2);
RWTexture2D<float4> MarchBack : register(u3);

groupshared uint nextLane;
// One group owns an 8x4 tile. Number adjacent lanes in 2x2 screen quads
// explicitly so the ray interval and the T-LUT broadcast use identical quads.
[numthreads(32, 1, 1)]
void main(uint3 group : SV_GroupID, uint thread : SV_GroupIndex)
{
    uint lane = WavePrefixCountBits(true);
    if (WaveGetLaneCount() < 32)
    {
        if (thread == 0)
            nextLane = 0;
        GroupMemoryBarrierWithGroupSync();
        uint count = WaveActiveCountBits(true), base = 0;
        if (WaveIsFirstLane())
            InterlockedAdd(nextLane, count, base);
        lane += WaveReadLaneFirst(base);
    }
    uint2 pixel = group.xy * uint2(8, 4) +
                  uint2(((lane >> 2) & 3) * 2 + (lane & 1), (lane >> 4) * 2 + ((lane >> 1) & 1));
    uint2 size = uint2(WIDTH, HEIGHT) / PixelStride;
#if VDB_QUARTER_REFERENCE
    // Keep depth, T-LUT and output targets identical when measuring a coarser independent-ray grid.
    // Padded lanes still participate in quad broadcasts before suppressing out-of-range writes.
    const uint spacing = 2;
    uint2 representative = min(pixel * spacing, size - 1u);
    float2 position = min((representative + .5) * PixelStride + (PixelStride - .5), float2(WIDTH, HEIGHT) - .5);
#else
    const uint spacing = 1;
    if (any(pixel >= size))
        return; // Full and half resolution are even, so whole quads exit.
    float2 position = pixel * PixelStride + (PixelStride - .5);
#endif
    VolumeVaryings actor = LoadVolumeActor(FirstVertex);
    actor.position.xy = position;
    VolumeMarchResult o = ShadeVolumeQuad(actor, PixelStride == 2);
    if (o.totalTau > 0. || any(o.numerator))
    {
        [unroll] for (uint y = 0; y < spacing; ++y)
        [unroll] for (uint x = 0; x < spacing; ++x)
        {
            uint2 dst = pixel * spacing + uint2(x, y);
#if VDB_QUARTER_REFERENCE
            if (any(dst >= size))
                continue;
#endif
#if VDB_NEAR_RAY_LOD && MARCH_COUNTERS
            MarchN[dst] = float4(o.numerator, float(o.lodMask));
#else
            MarchN[dst] = float4(o.numerator, 0);
#endif
            MarchD[dst] = float4(o.denominator, 0);
            MarchTau[dst] = float4(o.totalTau.xxx, 0);
            MarchBack[dst] = float4(o.backNumerator, 0);
        }
    }
}
