cbuffer PerObject : register(b0) {
    row_major float4x4 WorldViewProj;
    row_major float4x4 World;
    row_major float4x4 PrevWorldViewProj;   // shifts material params to offset 192 (reflected)
    float3 AlbedoColor;
    float  Metallic;
    float  Roughness;
    float  AO;
    float2 _pad;
};

Texture2D    AlbedoTex  : register(t0);
Texture2D    NormalTex  : register(t1);
Texture2D    OrmTex     : register(t2);
SamplerState LinearWrap : register(s0);

struct PSOut {
    float4 RT0 : SV_Target0;
    float4 RT1 : SV_Target1;
    float4 RT2 : SV_Target2;
    float2 RT3 : SV_Target3;   // motion vector (UV delta) — filled in a later increment
};

PSOut main(float4 sv : SV_POSITION, float3 wnorm : NORMAL, float3 wtangent : TANGENT, float2 uv : TEXCOORD0,
           float4 curClip : TEXCOORD1, float4 prevClip : TEXCOORD2, bool isFrontFace : SV_IsFrontFace) {
    float4 albedoSample = AlbedoTex.Sample(LinearWrap, uv);
    clip(albedoSample.a - 0.5);
    float3 albedo = albedoSample.rgb * AlbedoColor;

    // Flip geometric normal on back faces (double-sided meshes with CullMode=None)
    float3 N = normalize(isFrontFace ? wnorm : -wnorm);
    float3 normal;
    if (dot(wtangent, wtangent) > 0.001f) {
        float3 T     = normalize(wtangent - dot(wtangent, N) * N);
        float3 B     = cross(N, T);
        float3x3 TBN = float3x3(T, B, N);
        float3 nts   = NormalTex.Sample(LinearWrap, uv).xyz * 2.0 - 1.0;
        normal = normalize(mul(nts, TBN));
    } else {
        normal = N;
    }

    float3 orm  = OrmTex.Sample(LinearWrap, uv).rgb;
    float ao        = orm.r * AO;
    float roughness = orm.g * Roughness;
    float metallic  = orm.b * Metallic;

    PSOut o;
    float2 curUV  = (curClip.xy  / curClip.w)  * float2(0.5, -0.5) + 0.5;
    float2 prevUV = (prevClip.xy / prevClip.w) * float2(0.5, -0.5) + 0.5;

    o.RT0 = float4(albedo, metallic);
    o.RT1 = float4(normal * 0.5 + 0.5, roughness);
    o.RT2 = float4(ao, 0.0, 0.0, 0.0);
    o.RT3 = curUV - prevUV;
    return o;
}
