// SPDX-License-Identifier: MIT
// Bounds in texel coordinates for a group of 2x2x2 four-voxel bricks.
void VdbGroupBounds(uint3 group, out float3 lo, out float3 hi)
{
    lo = float3(group * 8u) + .5;
    lo = float3(group.x ? lo.x : 0., group.y ? lo.y : 0., group.z ? lo.z : 0.);
    hi = min(float3(group * 8u + 8u) + .5, float3(VdbSize));
}
