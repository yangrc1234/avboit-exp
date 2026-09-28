// SPDX-License-Identifier: MIT
// Exercises the production scheduler independently of projection/AVBOIT depth.
Buffer<float4> Targets : register(t0); // {lo.xy,hi.xy}, {flags, displacement,0,0}
#include "FrostSchedule.hlsli"
[numthreads(64, 1, 1)] void main(uint target : SV_GroupID, uint lane : SV_GroupIndex)
{ ScheduleFrost(int4(Targets[target * 2]), uint(Targets[target * 2 + 1].x), Targets[target * 2 + 1].y, lane); }
