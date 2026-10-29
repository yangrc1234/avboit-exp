// SPDX-License-Identifier: MIT
// Adapted from NVIDIA Donut's shadows.hlsli; the 16-tap filter is unchanged.
// Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#ifndef SHADOWS_HLSLI
#define SHADOWS_HLSLI
#include <donut/shaders/light_cb.h>
float4 ShadowVisibility(float4 depths, float reference, float falloff)
{
    return 1. - float4(reference < depths) * (falloff == 0 ? 1. : saturate((reference + falloff - depths) * 10.));
}
float2 EvaluateShadowGather16(Texture2DArray shadowMap, SamplerState shadowSampler, ShadowConstants shadow,
                              float3 world, float2 textureSize)
{
    float4 projected = mul(float4(world, 1), shadow.matWorldToUvzwShadow);
    if (projected.w <= 0)
        return 0;
    float3 uvz = projected.xyz / projected.w;
    if (shadow.shadowFalloffDistance == 0)
        uvz.z = max(uvz.z, .000001);
    float2 edgeFade = saturate(abs(uvz.xy - shadow.shadowMapCenterUV) * shadow.shadowFadeScale + shadow.shadowFadeBias);
    float fade = edgeFade.x * edgeFade.y;
    if (shadow.shadowFalloffDistance > 0)
        fade *= saturate(uvz.z * 10.);
    if (fade == 0)
        return 0;
    float3 location = float3(uvz.xy, shadow.shadowMapArrayIndex);
    float4 nw = ShadowVisibility(shadowMap.GatherRed(shadowSampler, location, int2(-1, -1)), uvz.z,
                                 shadow.shadowFalloffDistance);
    float4 ne = ShadowVisibility(shadowMap.GatherRed(shadowSampler, location, int2(1, -1)), uvz.z,
                                 shadow.shadowFalloffDistance);
    float4 sw = ShadowVisibility(shadowMap.GatherRed(shadowSampler, location, int2(-1, 1)), uvz.z,
                                 shadow.shadowFalloffDistance);
    float4 se =
        ShadowVisibility(shadowMap.GatherRed(shadowSampler, location, int2(1, 1)), uvz.z, shadow.shadowFalloffDistance);
    float2 phase = frac(uvz.xy * textureSize - .5 + 1. / 512.);
    float4 x = phase.x + float4(1, 0, -1, -2), y = phase.y + float4(1, 0, -1, -2);
    float4 wx = saturate(1. - x * x * .25), wy = saturate(1. - y * y * .25);
    float sum = dot(wx.xyyx, wy.yyxx * nw) + dot(wx.zwwz, wy.yyxx * ne) + dot(wx.xyyx, wy.wwzz * sw) +
                dot(wx.zwwz, wy.wwzz * se);
    float weight = dot(wx.xyyx, wy.yyxx) + dot(wx.zwwz, wy.yyxx) + dot(wx.xyyx, wy.wwzz) + dot(wx.zwwz, wy.wwzz);
    float visibility = saturate(sum / weight);
#ifdef AVBOIT_SCENE_VOLUME_SHADOWS
    visibility *= shadow.shadowMapArrayIndex == 0 ? SunVolumeShadow(world) : SampleVolumeShadow(world, 2);
#endif
    return float2(visibility * fade, fade);
}
#endif
