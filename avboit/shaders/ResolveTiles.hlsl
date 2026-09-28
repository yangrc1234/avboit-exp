// SPDX-License-Identifier: MIT
#include "TransparencyCommon.hlsli"
Texture2D<uint> ScreenTiles : register(t30);
Fullscreen TileVertex(uint vertex, uint instance, uint requiredBit, bool force, float border = 0.)
{
    uint w, h;
    ScreenTiles.GetDimensions(w, h);
    uint2 tile = uint2(instance % w, instance / w);
    Fullscreen o = (Fullscreen)0;
    // Bit 0 in Camera[8].w disables culling for the image/performance reference.
    if (!force && (ScreenTiles[tile] & requiredBit) == 0)
    {
        o.position = float4(2, 2, 0, 1);
        return o;
    }
    static const float2 corner[6] = {float2(0, 0), float2(1, 0), float2(0, 1),
                                     float2(0, 1), float2(1, 0), float2(1, 1)};
    float2 lo = max(float2(tile * 64) - border, 0.), hi = min(float2(tile * 64 + 64) + border, float2(WIDTH, HEIGHT));
    o.uv = lerp(lo, hi, corner[vertex]) / float2(WIDTH, HEIGHT);
    o.position = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
Fullscreen tileVertex(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    return TileVertex(vertex, instance, 4u, (uint(Camera[8].w) & 1u) != 0);
}
Fullscreen backgroundVertex(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    return TileVertex(vertex, instance, 4u, false, 1.);
}
