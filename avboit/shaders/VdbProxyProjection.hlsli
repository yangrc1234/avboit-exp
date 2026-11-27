// SPDX-License-Identifier: MIT
void VdbViewBounds(VolumeVaryings actor, float3 lo, float3 hi, out float3 view, out float3 viewRadius)
{
    float3 localCenter = ((lo + hi) / VdbSize - 1.) * actor.extent;
    float3 radius = (hi - lo) / VdbSize * actor.extent;
    float3 axisZ = cross(actor.axisX, actor.axisY);
    float3 center = actor.center - Camera[0].xyz + localCenter.x * actor.axisX +
                    localCenter.y * actor.axisY + localCenter.z * axisZ;
    [unroll] for (uint j = 0; j < 3; ++j)
    {
        float3 axis = Camera[1+j].xyz;
        view[j] = dot(center, axis);
        viewRadius[j] = dot(abs(float3(dot(axis, actor.axisX), dot(axis, actor.axisY), dot(axis, axisZ))), radius);
    }
}

float4 ProjectVdbBox(uint vertex, VolumeVaryings actor, float3 lo, float3 hi, bool alignQuads, out bool crossesNearPlane)
{
    float3 view, viewRadius;
    VdbViewBounds(actor, lo, hi, view, viewRadius);
    crossesNearPlane = view.z - viewRadius.z <= Camera[5].z;
    float farZ = view.z + viewRadius.z;
    if (farZ < Camera[5].z)
        return float4(0, 0, 0, 1);
    float nearZ = max(view.z - viewRadius.z, Camera[5].z);
    float2 a = view.xy - viewRadius.xy, b = view.xy + viewRadius.xy;
    float2 scale = rcp(float2(float(WIDTH) / HEIGHT, 1.) * .57735027);
    float2 jitter = Camera[11].xy * float2(2., -2.) / Camera[6].xy;
    float2 ndcLo = min(a / nearZ, a / farZ) * scale + jitter;
    float2 ndcHi = max(b / nearZ, b / farZ) * scale + jitter;
    const float2 corners[6] = {float2(0,0),float2(1,0),float2(0,1),float2(0,1),float2(1,0),float2(1,1)};
    if (!alignQuads)
    {
        // The half-resolution guide ray is displaced by half a full-resolution pixel.
        float2 pad = 1. / Camera[6].xy;
        return float4(lerp(ndcLo - pad, ndcHi + pad, corners[vertex % 6u]), 0., 1.);
    }
    // The actor and group shaders also need all lanes used by quad reads.
    float2 tiles = float2(WIDTH, HEIGHT) / 4.;
    float2 low = floor((ndcLo * .5 + .5) * tiles - .125) / tiles * 2. - 1.;
    float2 high = ceil((ndcHi * .5 + .5) * tiles + .125) / tiles * 2. - 1.;
    return float4(lerp(low, high, corners[vertex % 6u]), 0., 1.);
}
