// SPDX-License-Identifier: MIT
// Debug-only reads of the masks actually consumed by the rendering passes.
// Applied in display space, after refraction and tone mapping: the grid itself
// must not refract. No readback, extra render target or scheduling changes.
#include "TileSchedule.hlsli"
Buffer<uint> OverlayPhysicalMasks : register(t22);
Texture2D<uint> OverlayScreenTiles : register(t30);
Buffer<float4> OverlaySettings : register(t32); // mode, opacity, physical Z, grid

float3 OccupancyOverlay(float2 uv, float3 scene)
{
    float4 settings = OverlaySettings[0];
    uint mode = uint(settings.x);
    [branch] if (mode == 0) return scene;

    uint2 pixel = min(uint2(uv * Camera[6].xy), uint2(Camera[6].xy) - 1);
    float2 grid = uv * Camera[6].xy / 64.;
    float3 color = 0;
    if (mode <= 4)
    {
        uint bits = OverlayScreenTiles.Load(int3(pixel / 64, 0));
        if (mode == 1)
            color = float3((bits & 4) != 0, (bits & 1) != 0, (bits & 2) != 0);
        if (mode == 2 && (bits & 4) != 0)
            color = float3(1, .15, .05);
        if (mode == 3 && (bits & 1) != 0)
            color = float3(.1, 1, .2);
        if (mode == 4 && (bits & 2) != 0)
            color = float3(.1, .4, 1);
    }
    else if (mode == 5)
    {
        if ((OverlayScreenTiles.Load(int3(pixel / 64, 0)) & 4u) != 0)
            color = float3(1, .55, .08);
    }
    else
    {
        uint2 volumePixel = min(uint2(uv * Camera[6].zw), uint2(Camera[6].zw) - 1);
        uint tile = (volumePixel.y / 8) * ((VOLUME_WIDTH + 7) / 8) + volumePixel.x / 8;
        uint scalar = 0, rgb = 0;
        if (mode == 6)
        {
            [unroll] for (uint word = 0; word < (SLICE_COUNT + 31) / 32; ++word)
            {
                uint valid = SLICE_COUNT == 16 ? 0xffffu : 0xffffffffu;
                scalar |= OverlayPhysicalMasks[tile * 8 + word] & valid;
                rgb |= OverlayPhysicalMasks[tile * 8 + 4 + word] & valid;
            }
        }
        else
        {
            uint slice = min(uint(settings.z), SLICE_COUNT - 1);
            uint bit = 1u << (slice & 31);
            scalar = OverlayPhysicalMasks[tile * 8 + slice / 32] & bit;
            rgb = OverlayPhysicalMasks[tile * 8 + 4 + slice / 32] & bit;
        }
        color = float3(scalar != 0, rgb != 0, rgb != 0);
        grid = uv * Camera[6].zw / 8.;
    }
    bool active = any(color > 0);
    float3 result = lerp(scene, active ? color : scene * .35, settings.y);
    if (settings.w > 0)
    {
        // One display-pixel line, independent of internal render resolution.
        float2 edge = min(frac(grid), 1. - frac(grid)) / max(fwidth(grid), 1e-5);
        float gridLine = 1. - smoothstep(.25, 1., min(edge.x, edge.y));
        result = lerp(result, active ? float3(.95, .95, .95) : float3(.12, .12, .12), gridLine * .6 * settings.y);
    }
    return result;
}
