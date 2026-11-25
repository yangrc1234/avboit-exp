// SPDX-License-Identifier: MIT
#include "TransparencyCommon.hlsli"
#include "OccupancyOverlay.hlsli"
Texture2D<float4> ComposedHdr : register(t21);
float4 displayPixel(Fullscreen i) : SV_Target
{
    float3 color = ComposedHdr.SampleLevel(LinearClamp, i.uv, 0).rgb;
    color = pow(max(color, 0.) / (1. + max(color, 0.)), 1. / 2.2);
    return float4(OccupancyOverlay(i.uv, color), 1);
}

// Read the source ray counter directly, before upsampling, TAA and tone mapping.
// R counts candidate intervals, B counts those rejected by empty-space tests.
float4 marchStepsPixel(Fullscreen i) : SV_Target
{
    uint w, h;
    ComposedHdr.GetDimensions(w, h);
    uint2 pixel = min(uint2(i.uv * uint2(w, h)), uint2(w, h) - 1);
    float3 stats = ComposedHdr.Load(int3(pixel, 0)).rgb;
    float steps = OverlaySettings[1].x > 0. ? stats.b : max(0., stats.r - stats.b);
    const float3 ramp[6] = {float3(0,0,0), float3(.12,.25,.9), float3(0,.85,.9),
                            float3(.38,.92,.25), float3(1,.67,.05), float3(1,.12,.12)};
    float band = steps < 8. ? steps / 8. : min(1. + log2(steps / 8.), 5.);
    uint lo = min(uint(band), 4u);
    float3 color = lerp(ramp[lo], ramp[lo + 1], band - lo);
    // Stacked readouts use the same source ray in either view.
    float2 cursor = OverlaySettings[0].xy;
    float2 viewport = OverlaySettings[0].zw;
    int2 origin = clamp(int2(cursor * viewport) + int2(10, 12), 0, max(int2(viewport) - int2(34, 28), 0));
    int2 local = int2(i.uv * viewport) - origin;
    if (all(cursor >= 0.) && all(cursor < 1.) && all(local >= 0) && all(local < int2(34, 28)))
    {
        bool skippedRow = local.y >= 14;
        uint2 cell = uint2(local.x, local.y % 14) / 2;
        uint2 at = min(uint2(cursor * uint2(w, h)), uint2(w, h) - 1);
        float3 pick = ComposedHdr.Load(int3(at, 0)).rgb;
        uint value = uint(clamp(round(skippedRow ? pick.b : pick.r - pick.b), 0., 9999.));
        const uint place[4] = {1000, 100, 10, 1};
        // Each glyph is five 3-bit rows.
        const uint glyph[10] = {31599, 11415, 29671, 29647, 23497, 31183, 31215, 29257, 31727, 31695};
        color = .035;
        if (cell.x > 0 && cell.y > 0 && cell.y <= 5)
        {
            uint x = cell.x - 1;
            if (x < 16 && (x % 4) < 3)
            {
                uint digit = (value / place[x / 4]) % 10;
                uint bit = (5 - cell.y) * 3 + 2 - x % 4;
                if ((glyph[digit] >> bit) & 1u)
                    color = skippedRow ? float3(0., .9, 1.) : 1.;
            }
        }
    }
    return float4(color, 1.);
}
