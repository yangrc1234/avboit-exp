// SPDX-License-Identifier: MIT
SamplerState LinearClamp : register(s0);
#include "VolumeLighting.hlsli"
Texture3D<float2> Density : register(t26);
Buffer<float4> Casters : register(t32);
RWTexture2DArray<float4> MomentsOut : register(u0);
RWTexture2DArray<float> TauOut : register(u1);

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID)
{
    uint width, height, layers;
    MomentsOut.GetDimensions(width, height, layers);
    if (any(id.xy >= uint2(width, height)) || VolumeLights[37][id.z] == 0.)
        return;
    float2 uv = (id.xy + .5) / float2(width, height);
    float4 nearP = LightTransform(float4(uv, 1, 1), 25 + 4 * id.z);
    float4 farP = LightTransform(float4(uv, 0, 1), 25 + 4 * id.z);
    float3 origin = nearP.xyz / nearP.w, end = farP.xyz / farP.w;
    float rayLength = length(end - origin);
    float3 ray = (end - origin) / rayLength;
    float total = 0.;
    float4 moments = 0.;
    for (uint actor = 0; actor < uint(VolumeLights[38].x); ++actor)
    {
        float4 center = Casters[actor * 5];
        float3 extent = Casters[actor * 5 + 1].xyz;
        float sigma = dot(Casters[actor * 5 + 2].rgb, float3(.2126, .7152, .0722));
        float3 axisX = Casters[actor * 5 + 3].xyz, axisY = Casters[actor * 5 + 4].xyz;
        float3 axisZ = cross(axisX, axisY), delta = origin - center.xyz;
        float3 localOrigin = float3(dot(delta, axisX), dot(delta, axisY), dot(delta, axisZ));
        float3 localRay = float3(dot(ray, axisX), dot(ray, axisY), dot(ray, axisZ));
        float enter = 0., leave = rayLength;
        [unroll] for (uint axis = 0; axis < 3; ++axis)
        {
            if (abs(localRay[axis]) < 1e-7)
            {
                if (abs(localOrigin[axis]) > extent[axis])
                    leave = -1.;
            }
            else
            {
                float2 t = (float2(-1, 1) * extent[axis] - localOrigin[axis]) / localRay[axis];
                enter = max(enter, min(t.x, t.y));
                leave = min(leave, max(t.x, t.y));
            }
        }
        if (leave <= enter || sigma <= 0.)
            continue;
        // Fixed deterministic integration for the reference prototype; actor
        // shaders can later replace the density evaluator without changing MSM.
        uint steps = center.w > 0 ? 96 : 32;
        float ds = (leave - enter) / steps;
        for (uint step = 0; step < steps; ++step)
        {
            float3 world = origin + ray * (enter + (step + .5) * ds);
            float3 local = localOrigin + localRay * (enter + (step + .5) * ds);
            float density = center.w > 0 ? Density.SampleLevel(LinearClamp, local / (2. * extent) + .5, 0).r : 1.;
            float tau = sigma * density * ds;
            float4 projected = LightTransform(float4(world, 1), 13 + id.z * 4);
            float z = MomentDepth(projected.z / projected.w, id.z);
            float z2 = z * z;
            total += tau;
            moments += tau * float4(z, z2, z * z2, z2 * z2);
        }
    }
    // Integrate in FP32; normalize and quantize once at the final UAV store.
    MomentsOut[id] = EncodeVolumeMoments(total > 1e-6 ? moments / total : 0.);
    TauOut[id] = total;
}
