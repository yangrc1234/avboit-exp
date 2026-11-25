// SPDX-License-Identifier: MIT
Buffer<float4> SmokeTrails : register(t35);
RWTexture3D<uint> Candidates : register(u0);
[numthreads(4, 4, 4)] void main(uint3 cell : SV_DispatchThreadID)
{
    float3 cellSize = rcp(SmokeTrails[32].xyz);
    float3 center = SmokeTrails[31].xyz + (float3(cell) + .5) * cellSize;
    // Enclose the ENTIRE cell in a sphere. Any intersecting capsule must pass
    // this test, even when its radius is much smaller than the LUT cell.
    float cellRadius = .5 * length(cellSize);
    uint mask = 0;
    uint count = min(uint(SmokeTrails[0].x), 15u);
    for (uint i = 0; i < count; ++i)
    {
        float4 a = SmokeTrails[1 + 2 * i], b = SmokeTrails[2 + 2 * i];
        float3 ab = b.xyz - a.xyz, ap = center - a.xyz;
        float t = saturate(dot(ap, ab) / max(dot(ab, ab), 1e-8));
        float3 delta = ap - t * ab;
        float radius = a.w + cellRadius + 1e-4;
        if (dot(delta, delta) <= radius * radius)
            mask |= 1u << i;
    }
    Candidates[cell] = mask;
}
