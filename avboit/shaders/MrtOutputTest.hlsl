// SPDX-License-Identifier: MIT
// Deliberately distinct output signatures; do not replace missing targets with zero.
cbuffer DrawConstants : register(b0)
{
    uint DrawSeed;
};

float4 fullscreen(uint vertex : SV_VertexID) : SV_Position
{
    return float4(vertex == 2 ? 3.0 : -1.0, vertex == 1 ? 3.0 : -1.0, 0.5, 1.0);
}

float4 color(float4 position, uint target)
{
    // Share the hash across outputs so four MRTs do not also benchmark four independent hashes.
    uint h = uint(position.x) + uint(position.y) * 2560u + DrawSeed * 747796405u;
    h = (h ^ (h >> 16)) * 2246822519u;
    h = (h ^ (h >> 13)) * 3266489917u;
    h ^= h >> 16;
    // Spatially varying, exactly representable values avoid constant-color fast-clear shortcuts.
    return float4((h & 15u) + 1u, ((h >> 4) & 15u) + 1u, ((h >> 8) & 15u) + 1u, ((h >> 12) & 15u) + 1u) / 64.0 +
           float(target) * 0.25;
}

float4 one(float4 position : SV_Position) : SV_Target0
{
    return color(position, 0);
}

struct Three
{
    float4 a : SV_Target0;
    float4 b : SV_Target1;
    float4 c : SV_Target2;
};

Three three(float4 position : SV_Position)
{
    Three output;
    output.a = color(position, 0);
    output.b = color(position, 1);
    output.c = color(position, 2);
    return output;
}

struct Four
{
    float4 a : SV_Target0;
    float4 b : SV_Target1;
    float4 c : SV_Target2;
    float4 d : SV_Target3;
};

Four four(float4 position : SV_Position)
{
    Four output;
    output.a = color(position, 0);
    output.b = color(position, 1);
    output.c = color(position, 2);
    output.d = color(position, 3);
    return output;
}

Four zeroExtra(float4 position : SV_Position)
{
    Four output = (Four)0;
    output.a = color(position, 0);
    return output;
}

Four zeroLast(float4 position : SV_Position)
{
    Four output = four(position);
    output.d = 0;
    return output;
}

struct Holes
{
    float4 a : SV_Target0;
    float4 d : SV_Target3;
};

Holes holes(float4 position : SV_Position)
{
    Holes output;
    output.a = color(position, 0);
    output.d = color(position, 3);
    return output;
}

// Independent work used only by Target1. The same compiled PS is used for its
// masked and unmasked PSOs; neither path has UAV/discard/depth side effects.
Texture2D<float4> WorkTexture : register(t0);
SamplerState WorkSampler : register(s0);
struct Two
{
    float4 a : SV_Target0;
    float4 b : SV_Target1;
};
float4 AluWork(float4 position, uint iterations)
{
    float4 value = color(position, 1);
    [loop] for (uint i = 0; i < iterations; ++i)
    {
        value = frac(value.wxyz * 1.6180339 + value.zwxy * .71327 + float4(.13, .27, .39, .51));
        value = frac(value * value.yzwx * 3.731 + .173);
    }
    return .25 + .5 * value;
}
float4 TextureWork(float4 position, uint iterations)
{
    float2 uv = (position.xy + float(DrawSeed)) / 512.;
    float4 value = color(position, 1);
    [loop] for (uint i = 0; i < iterations; ++i)
    {
        // Dependent addresses prevent collapsing repeated samples into one.
        uv = frac(uv + value.xy * .371 + float2(.137, .239));
        value = .75 * WorkTexture.SampleLevel(WorkSampler, uv, 0) + .25 * value;
    }
    return .25 + .5 * value;
}
Two alu64(float4 position : SV_Position)
{
    Two output;
    output.a = color(position, 0);
    output.b = AluWork(position, 64);
    return output;
}
Two alu256(float4 position : SV_Position)
{
    Two output;
    output.a = color(position, 0);
    output.b = AluWork(position, 256);
    return output;
}
Two texture8(float4 position : SV_Position)
{
    Two output;
    output.a = color(position, 0);
    output.b = TextureWork(position, 8);
    return output;
}
Two texture32(float4 position : SV_Position)
{
    Two output;
    output.a = color(position, 0);
    output.b = TextureWork(position, 32);
    return output;
}
Two texture128(float4 position : SV_Position)
{
    Two output;
    output.a = color(position, 0);
    output.b = TextureWork(position, 128);
    return output;
}
