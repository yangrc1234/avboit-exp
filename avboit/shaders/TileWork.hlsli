// SPDX-License-Identifier: MIT
// Per-mip 4x4 output mask. All lanes take the same branch, before any barrier.
Buffer<uint> WorkMask : register(t28);
bool TileOrigin(uint2 group, uint2 size, out uint2 origin)
{
    origin = group * 4;
    return WorkMask[group.y * ((size.x + 3) / 4) + group.x] != 0;
}
