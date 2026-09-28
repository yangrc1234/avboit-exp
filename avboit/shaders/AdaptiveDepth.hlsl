// SPDX-License-Identifier: MIT
// Independent implementation of the public AVBOIT depth-compaction idea.
// Input: occupancy in 2048 virtual log-depth slices (one uint per slice).
// Output: [virtualCount, occupiedCount], then 2049 {prefix, flags} entries.
// Flags: bit 0 occupied, bit 1 region start, bit 2 region end.
// Guard slices cover linear splat and the sampling depth bias. They are
// regenerated after EVERY coarsening, rather than being shrunk by merging.
Buffer<uint> InputOccupancy : register(t0);
RWBuffer<uint> DepthWarp : register(u0);
#define VIRTUAL_SLICES 2048
// Budget is supplied as a compile-time entry-point constant.
#define GUARD_SLICES 3
#define THREADS 256

groupshared uint rawOccupancy[VIRTUAL_SLICES];
groupshared uint scanA[VIRTUAL_SLICES];
groupshared uint scanB[VIRTUAL_SLICES];
groupshared uint sliceCount;
groupshared uint occupiedCount;

void BuildAdaptiveDepth(uint thread, uint physicalBudget)
{
    for (uint i = thread; i < VIRTUAL_SLICES; i += THREADS)
        rawOccupancy[i] = InputOccupancy[i] != 0;
    if (thread == 0)
        sliceCount = VIRTUAL_SLICES;
    GroupMemoryBarrierWithGroupSync();
    for (;;)
    {
        for (uint i = thread; i < sliceCount; i += THREADS)
        {
            uint active = 0;
            for (int offset = -GUARD_SLICES; offset <= GUARD_SLICES; ++offset)
            {
                int neighbor = int(i) + offset;
                if (neighbor >= 0 && neighbor < int(sliceCount))
                    active |= rawOccupancy[neighbor];
            }
            scanA[i] = active;
        }
        GroupMemoryBarrierWithGroupSync();
        // Readable, wave-size-independent inclusive scan; optimize after validation.
        for (uint stride = 1; stride < sliceCount; stride *= 2)
        {
            for (uint i = thread; i < sliceCount; i += THREADS)
                scanB[i] = scanA[i] + (i >= stride ? scanA[i - stride] : 0);
            GroupMemoryBarrierWithGroupSync();
            for (uint i = thread; i < sliceCount; i += THREADS)
                scanA[i] = scanB[i];
            GroupMemoryBarrierWithGroupSync();
        }
        if (thread == 0)
            occupiedCount = scanA[sliceCount - 1];
        GroupMemoryBarrierWithGroupSync();
        // Reserve a terminal slice for interpolation at the last occupied slice.
        if (occupiedCount < physicalBudget)
            break;
        for (uint i = thread; i < sliceCount / 2; i += THREADS)
            scanB[i] = rawOccupancy[2 * i] | rawOccupancy[2 * i + 1];
        GroupMemoryBarrierWithGroupSync();
        for (uint i = thread; i < sliceCount / 2; i += THREADS)
            rawOccupancy[i] = scanB[i];
        GroupMemoryBarrierWithGroupSync();
        if (thread == 0)
            sliceCount /= 2;
        GroupMemoryBarrierWithGroupSync();
    }
    if (thread == 0)
    {
        DepthWarp[0] = sliceCount;
        DepthWarp[1] = occupiedCount;
    }
    for (uint i = thread; i <= VIRTUAL_SLICES; i += THREADS)
    {
        uint prefix = i == 0 ? 0 : scanA[min(i, sliceCount) - 1];
        uint active = i < sliceCount ? scanA[i] - prefix : 0;
        uint previous = i > 0 && i < sliceCount ? scanA[i - 1] - (i > 1 ? scanA[i - 2] : 0) : 0;
        uint next = i + 1 < sliceCount ? scanA[i + 1] - scanA[i] : 0;
        DepthWarp[2 + 2 * i] = prefix;
        DepthWarp[3 + 2 * i] = active | ((active && !previous) ? 2 : 0) | ((active && !next) ? 4 : 0);
    }
}

[numthreads(THREADS, 1, 1)] void main(uint t : SV_GroupIndex)
{ BuildAdaptiveDepth(t, 128); }[numthreads(THREADS, 1, 1)] void budget16(uint t : SV_GroupIndex)
{
    BuildAdaptiveDepth(t, 16);
}
[numthreads(THREADS, 1, 1)] void budget32(uint t : SV_GroupIndex)
{ BuildAdaptiveDepth(t, 32); }[numthreads(THREADS, 1, 1)] void budget64(uint t : SV_GroupIndex)
{
    BuildAdaptiveDepth(t, 64);
}
