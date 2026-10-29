// SPDX-License-Identifier: MIT
// Supply the scene's Reversed-Z shadow filter to Donut's material shader.
#pragma pack_matrix(row_major)
#define VOLUME_LIGHT_SPACE space3
#define AVBOIT_SCENE_VOLUME_SHADOWS 1
SamplerState LinearClamp : register(s10, space3);
#include "VolumeLighting.hlsli"
#include "SceneShadows.hlsli"
#include <passes/forward_ps.hlsl>
