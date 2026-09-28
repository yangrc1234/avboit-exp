// SPDX-License-Identifier: MIT
Buffer<float4> SceneVertices : register(t0);
Buffer<float4> Camera : register(t15);
Texture2D<float4> Albedo : register(t16);
Texture2D<float4> NormalMap : register(t17);
Texture2D<float4> MetallicRoughness : register(t18);
Texture2D<float> ShadowDepth : register(t19);
SamplerState MaterialSampler : register(s0);
struct VertexOutput
{
    float4 position : SV_Position;
    float3 world : WORLD;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float4 material : TEXCOORD;
    float4 factor : COLOR;
    float cutoff : CUTOFF;
    float depth : DEPTH;
};
float3 SafeNormal(float3 n, float3 fallback)
{
    float squared = dot(n, n);
    return squared > 1e-12 ? n * rsqrt(squared) : fallback;
}
VertexOutput sceneVertex(uint id : SV_VertexID)
{
    VertexOutput o;
    o.world = SceneVertices[id * 5].xyz;
    float3 delta = o.world - Camera[0].xyz;
    float3 view = float3(dot(delta, Camera[1].xyz), dot(delta, Camera[2].xyz), dot(delta, Camera[3].xyz));
    o.position = float4(view.x / (Camera[6].x / Camera[6].y * .57735027), view.y / .57735027,
                        view.z * 80. / 79.95 - 4. / 79.95, view.z);
    o.depth = view.z;
    o.normal = SceneVertices[id * 5 + 1].xyz;
    o.cutoff = SceneVertices[id * 5 + 1].w;
    o.material = SceneVertices[id * 5 + 2];
    o.tangent = SceneVertices[id * 5 + 3];
    o.factor = SceneVertices[id * 5 + 4];
    return o;
}
float4 ShadowPosition(float3 world)
{
    float3 light = normalize(float3(-.45, .85, -.32));
    float3 forward = -light;
    float3 right = normalize(cross(float3(0, 1, 0), forward)), up = cross(forward, right);
    float3 delta = world - (float3(0, 2, 8) + light * 30.);
    return float4(dot(delta, right) / 18., dot(delta, up) / 14., dot(delta, forward) / 70., 1);
}
VertexOutput shadowVertex(uint id : SV_VertexID)
{
    VertexOutput o = sceneVertex(id);
    o.position = ShadowPosition(o.world);
    return o;
}
void shadowPixel(VertexOutput i)
{
    float alpha = Albedo.Sample(MaterialSampler, i.material.xy).a * i.factor.a;
    if (alpha < i.cutoff)
        discard;
}
float4 scenePixel(VertexOutput i) : SV_Target
{
    float4 albedo = Albedo.Sample(MaterialSampler, i.material.xy) * i.factor;
    if (albedo.a < i.cutoff)
        discard;
    float3 n = SafeNormal(i.normal, float3(0, 1, 0));
    float3 axis = abs(n.y) < .99 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 t = SafeNormal(i.tangent.xyz - n * dot(n, i.tangent.xyz), SafeNormal(cross(axis, n), float3(1, 0, 0)));
    float3 b = cross(n, t) * i.tangent.w;
    float3 tangentNormal = NormalMap.Sample(MaterialSampler, i.material.xy).xyz * 2. - 1.;
    n = SafeNormal(t * tangentNormal.x + b * tangentNormal.y + n * tangentNormal.z, n);
    float3 view = SafeNormal(Camera[0].xyz - i.world, n);
    if (dot(n, view) < 0)
        n = -n;
    float4 mr = MetallicRoughness.Sample(MaterialSampler, i.material.xy);
    float rough = clamp(mr.g * i.material.z, .15, 1.);
    float3 light = normalize(float3(-.45, .85, -.32)), h = SafeNormal(light + view, n);
    float3 ambient = lerp(float3(.27, .20, .14), float3(.35, .43, .53), n.y * .5 + .5);
    float4 shadowPosition = ShadowPosition(i.world);
    float2 shadowUV = shadowPosition.xy * float2(.5, -.5) + .5;
    float shadow = 0, bias = max(.00015, .001 * (1. - max(0., dot(n, light))));
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            shadow += float(shadowPosition.z - bias <=
                            ShadowDepth.SampleLevel(MaterialSampler, shadowUV + float2(x, y) / 2048., 0)) /
                      9.;
    if (any(shadowUV < 0) || any(shadowUV > 1) || shadowPosition.z < 0 || shadowPosition.z > 1)
        shadow = 1;
    float spec = pow(max(0., dot(n, h)), lerp(120., 5., rough)) * (1. - rough) * shadow;
    return float4(albedo.rgb * (ambient + float3(2.5, 2.22, 1.8) * max(0., dot(n, light)) * shadow) + spec * .16,
                  i.depth);
}
