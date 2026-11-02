// SPDX-License-Identifier: MIT
// Uniform log-depth reference using the same warp ABI as AdaptiveDepth.
// Budget-1 intervals plus the final interpolation endpoint consume budget slots.
// Occupancy has no influence on this map; subsequent sparse clear still uses it.
Buffer<float4> Camera : register(t15);
RWBuffer<uint> DepthWarp : register(u0);
void BuildFixedDepth(uint thread, uint budget)
{
    uint2 padding = uint2(Camera[12].xy);
    uint intervals = budget - 1 - padding.x - padding.y;
    if (thread == 0)
    {
        DepthWarp[0] = intervals;
        DepthWarp[1] = intervals + padding.x + padding.y;
    }
    for (uint i = thread; i <= 2048; i += 256)
    {
        DepthWarp[2 + 2 * i] = min(i, intervals) + padding.x;
        DepthWarp[3 + 2 * i] = i < intervals ? (1u | (i == 0 ? 2u : 0u) | (i + 1 == intervals ? 4u : 0u)) : 0u;
    }
}
[numthreads(256, 1, 1)] void main(uint t : SV_GroupIndex)
{ BuildFixedDepth(t, 128); }[numthreads(256, 1, 1)] void budget16(uint t : SV_GroupIndex)
{
    BuildFixedDepth(t, 16);
}
[numthreads(256, 1, 1)] void budget32(uint t : SV_GroupIndex)
{ BuildFixedDepth(t, 32); }[numthreads(256, 1, 1)] void budget64(uint t : SV_GroupIndex)
{
    BuildFixedDepth(t, 64);
}
