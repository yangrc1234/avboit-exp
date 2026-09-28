// SPDX-License-Identifier: MIT
// Camera[8] is the fixed-screen pyramid dimensions/count. Camera[6].xy is render size.
uint2 TileStageSize(uint stage)
{
    if (stage == 10)
        return uint2(Camera[6].xy);
    if (stage == 0 || stage / 2 >= uint(Camera[8].z))
        return 0;
    return uint2(Camera[8].xy) / uint2(1u << (stage / 2), 1u << ((stage & 1) ? stage / 2 : stage / 2 - 1));
}
uint TileBlockSize(uint stage)
{
    return stage == 10 ? 8u : 4u;
}
uint TileStageOffset(uint stage)
{
    uint offset = 0;
    for (uint i = 0; i < stage; ++i)
    {
        uint block = TileBlockSize(i);
        uint2 groups = (i < 10 && (i & 1) != 0) ? (TileStageSize(i) + block - 1) / block : uint2(0, 0);
        offset += 1 + groups.x * groups.y;
    }
    return offset;
}
