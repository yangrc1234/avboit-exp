// SPDX-License-Identifier: MIT
// Conservative 16x16 full-resolution zero-transmittance quads.
// Integration emits the first slice whose RGB quantizes to zero. Include the
// entire shading XY footprint when reducing those depths to a tile in the VS.
#include "TransparencyCommon.hlsli"
Buffer<uint> DepthWarp : register(t1);
Texture2D<uint> FirstZeroSlice : register(t2);
float4 quadVertex(uint vertex : SV_VertexID, uint instance : SV_InstanceID) : SV_Position
{
    uint2 tiles = (uint2(WIDTH, HEIGHT) + 15) / 16;
    uint2 tile = uint2(instance % tiles.x, instance / tiles.x);
    // Four vertices form one instanced triangle strip. Invalid tiles collapse
    // outside the viewport; there is no PS, append buffer or indirect draw.
    const float4 rejected = float4(2, 2, 0, 1);
    uint2 begin = tile * 16, end = min(begin + 16, uint2(WIDTH, HEIGHT));
    // Exact footprint of all full-resolution pixel centers, with an extra
    // texel on each side to cover arithmetic and bilinear boundary roundoff.
    float2 ratio = float2(VOLUME_WIDTH, VOLUME_HEIGHT) / float2(WIDTH, HEIGHT);
    int2 lo = max(int2(0, 0), int2(floor((begin + .5) * ratio - .5)) - 1);
    int2 hi = min(int2(VOLUME_WIDTH, VOLUME_HEIGHT) - 1, int2(floor((end - .5) * ratio - .5)) + 2);
    uint firstZero = 0;
    for (int y = lo.y; y <= hi.y; ++y)
        for (int x = lo.x; x <= hi.x; ++x)
        {
            uint slice = FirstZeroSlice.Load(int3(x, y, 0));
            if (slice >= SLICE_COUNT)
                return rejected;
            firstZero = max(firstZero, slice);
        }
    // Invert the adaptive prefix at a virtual-slice boundary. At/after this
    // boundary the lower Z filtering tap is already zero. Compensate the same
    // two-virtual-slice sample bias as accumulation, plus one guard slice.
    uint count = DepthWarp[0], l = 0, r = count;
    while (l < r)
    {
        uint m = (l + r) / 2;
        if (DepthWarp[2 + 2 * m] >= firstZero)
            r = m;
        else
            l = m + 1;
    }
    uint boundary = l + 3;
    if (boundary >= count)
        return rejected;
    float z = exp2(float(boundary) / float(count) * log2(81.)) - 1.;
    if (z <= Camera[5].z || z >= Camera[5].w)
        return rejected;
    float deviceDepth = Camera[5].x + Camera[5].y / z;
    static const float2 corners[4] = {float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 1)};
    float2 uv = lerp(float2(begin), float2(end), corners[vertex]) / float2(WIDTH, HEIGHT);
    return float4(uv * float2(2, -2) + float2(-1, 1), deviceDepth, 1);
}
