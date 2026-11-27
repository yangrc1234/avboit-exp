// SPDX-License-Identifier: MIT
#define VDB_GROUP_MARCH 1
#include "VdbProxy.hlsl"

VolumeVaryings groupVertex(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    VolumeVaryings actor;
    float4 position = ProjectGroup(vertex, instance, actor);
    actor.position = position;
    return actor;
}
