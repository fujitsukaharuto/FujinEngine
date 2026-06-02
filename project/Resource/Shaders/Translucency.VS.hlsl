cbuffer PerObject : register(b0) {
    row_major float4x4 WorldViewProj;
    row_major float4x4 World;
    float3 AlbedoColor; float  Metallic;
    float  Roughness;   float  AO; float Opacity; float _pad;
};

struct VSIn {
    float3 Pos    : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent: TANGENT;
    float2 UV     : TEXCOORD0;
};

struct VSOut {
    float4 Sv      : SV_POSITION;
    float3 WPos    : TEXCOORD1;
    float3 WNorm   : NORMAL;
    float3 WTangent: TANGENT;
    float2 UV      : TEXCOORD0;
};

VSOut main(VSIn v) {
    VSOut o;
    float4 wp  = mul(World, float4(v.Pos, 1.0));
    o.Sv       = mul(WorldViewProj, float4(v.Pos, 1.0));
    o.WPos     = wp.xyz;
    o.WNorm    = normalize(mul((float3x3)World, v.Normal));
    float3 rawT = mul((float3x3)World, v.Tangent);
    o.WTangent = (dot(rawT, rawT) > 1e-6f) ? normalize(rawT) : float3(0, 0, 0);
    o.UV       = v.UV;
    return o;
}
