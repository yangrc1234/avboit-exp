// SPDX-License-Identifier: MIT
#include "TransparencyCommon.hlsli"
float4 opaquePixel(Varyings i) : SV_Target
{
    float3 color = i.color.rgb;
    if (i.sphere.w < -1.5)
    {
        float3 n = normalize(i.surfaceNormal);
        float diffuse = saturate(dot(n, normalize(float3(-.4, .7, -.5))));
        color *= .25 + 1.4 * diffuse;
    }
    return float4(color, i.depth);
}
