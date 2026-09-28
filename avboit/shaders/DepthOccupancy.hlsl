// SPDX-License-Identifier: MIT
// Each input pair is a conservative virtual log-depth [first,last] interval.
// The renderer's bounds projection supplies these ranges. This stage does not
// evaluate material opacity: false positives are safe, missing occupancy is not.
Buffer<uint> DepthRanges : register(t0);
RWBuffer<uint> Occupancy : register(u0);
[numthreads(64, 1, 1)] void main(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    uint first = min(DepthRanges[group.x * 2], 2047u);
    uint last = min(DepthRanges[group.x * 2 + 1], 2047u);
    for (uint slice = first + lane; slice <= last; slice += 64)
        InterlockedOr(Occupancy[slice], 1u);
}
