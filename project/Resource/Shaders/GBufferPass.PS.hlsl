cbuffer PerObject : register(b0) {
    row_major float4x4 WorldViewProj;
    row_major float4x4 World;
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
};

PSOut main(float4 sv : SV_POSITION, float3 wnorm : NORMAL, float3 wtangent : TANGENT, float2 uv : TEXCOORD0, bool isFrontFace : SV_IsFrontFace) {
    float3 albedo = AlbedoTex.Sample(LinearWrap, uv).rgb * AlbedoColor;

    // Flip geometric normal on back faces (double-sided meshes with CullMode=None)
    float3 N = normalize(isFrontFace ? wnorm : -wnorm);
    float3 normal;
    if (dot(wtangent, wtangent) > 0.001f) {
        float3 T   = normalize(wtangent - dot(wtangent, N) * N);
        float3 B   = cross(N, T);
        float3x3 TBN = float3x3(T, B, N);
        float3 nts = NormalTex.Sample(LinearWrap, uv).xyz * 2.0 - 1.0;
        normal = normalize(mul(nts, TBN));
    } else {
        normal = N;
    }

    // ORM: R=AO, G=Roughness, B=Metallic (glTF packed convention)
    float3 orm  = OrmTex.Sample(LinearWrap, uv).rgb;
    float ao        = orm.r * AO;
    float roughness = orm.g * Roughness;
    float metallic  = orm.b * Metallic;

    PSOut o;
    o.RT0 = float4(albedo, metallic);
    o.RT1 = float4(normal * 0.5 + 0.5, roughness);
    o.RT2 = float4(ao, 0.0, 0.0, 0.0);
    return o;
}
