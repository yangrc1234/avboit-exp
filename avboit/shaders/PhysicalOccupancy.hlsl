// SPDX-License-Identifier: MIT
// Remap raw 2048-bit per-tile occupancy after Adaptive Z has selected its
// accepted virtual resolution. Output: four scalar + four RGB words per tile.
Buffer<uint> VirtualMasks : register(t0);
Buffer<uint> DepthWarp : register(t1);
RWBuffer<uint> PhysicalMasks : register(u0);
Buffer<float4> Bounds : register(t2);
#define WORDS_PER_VIRTUAL_TILE 64
groupshared uint occupied[8];

[numthreads(64, 1, 1)] void main(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    uint tile = group.x;
    uint2 tileSize = uint2(ceil(Bounds[0].xy / Bounds[0].z));
    uint tileCount = tileSize.x * tileSize.y;
    if (lane < 8)
        occupied[lane] = 0;
    GroupMemoryBarrierWithGroupSync();
    uint virtualCount = DepthWarp[0];
    for (uint kind = 0; kind < 2; ++kind)
    {
        uint bits = VirtualMasks[2048 + (kind * tileCount + tile) * 64 + lane];
        while (bits != 0)
        {
            uint bit = firstbitlow(bits);
            bits &= bits - 1;
            uint virtualSlice = (lane * 32 + bit) * virtualCount / 2048;
            uint physicalSlice = min(126u, DepthWarp[2 + virtualSlice * 2]);
            // Two linear-Z taps, plus one physical slot on each side for
            // CPU bounds / GPU vertex-transform floating-point disagreement.
            uint first = physicalSlice > 0 ? physicalSlice - 1 : 0;
            uint last = min(127u, physicalSlice + 2);
            for (uint slot = first; slot <= last; ++slot)
                InterlockedOr(occupied[kind * 4 + slot / 32], 1u << (slot % 32));
        }
    }
    GroupMemoryBarrierWithGroupSync();
    // Always overwrite all words, including empty tiles after camera movement.
    if (lane < 8)
        PhysicalMasks[tile * 8 + lane] = occupied[lane];
}
