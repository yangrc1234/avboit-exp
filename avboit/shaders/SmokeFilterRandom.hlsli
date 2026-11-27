// SPDX-License-Identifier: MIT
#if SMOKE_STOCHASTIC
// Integer avalanche keeps filter randomness separate from ray-position jitter.
uint SmokeFilterHash(uint v)
{
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    return v ^ (v >> 16);
}
float3 SmokeFilterRandom(uint seed)
{
    uint3 bits = uint3(SmokeFilterHash(seed ^ 0xa511e9b3u), SmokeFilterHash(seed ^ 0x63d83595u),
                       SmokeFilterHash(seed ^ 0xb8d4f239u));
    return float3(bits >> 8) * (1. / 16777216.);
}
#endif
uint SmokeFilterSeed(uint2 pixel)
{
#if SMOKE_STOCHASTIC
    return SmokeFilterHash(pixel.x + pixel.y * WIDTH) ^ SmokeFilterHash(uint(Camera[9].w));
#else
    return 0u;
#endif
}
