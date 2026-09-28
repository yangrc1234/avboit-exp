// SPDX-License-Identifier: MIT
#include "TransparencyCommon.hlsli"
Texture2D<float2> VfxOffset : register(t20);
Texture2D<float4> SceneColor : register(t21);
float4 applyDistortion(Fullscreen i) : SV_Target
{
    float2 offset = VfxOffset.SampleLevel(LinearClamp, i.uv, 0);
    float border = min(min(i.uv.x, 1. - i.uv.x) * WIDTH, min(i.uv.y, 1. - i.uv.y) * HEIGHT);
    float2 uv = clamp(i.uv + offset / float2(WIDTH, HEIGHT) * smoothstep(0., 16., border), .5 / float2(WIDTH, HEIGHT),
                      1. - .5 / float2(WIDTH, HEIGHT));
    return float4(SceneColor.SampleLevel(LinearClamp, uv, 0).rgb, 1);
}
