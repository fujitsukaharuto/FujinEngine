cbuffer PerObject : register(b0) {
    row_major float4x4 WorldViewProj;       // current (jittered) world-view-proj
    row_major float4x4 World;
    row_major float4x4 PrevWorldViewProj;   // previous frame's (jittered) world-view-proj
    float3 AlbedoColor;
    float  Metallic;
    float  Roughness;
    float  AO;
    float2 _pad;
};

struct VSIn {
    float3 Pos    : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent: TANGENT;
    float2 UV     : TEXCOORD0;
};

struct VSOut {
    float4 Sv      : SV_POSITION;
    float3 WNorm   : NORMAL;
    float3 WTangent: TANGENT;
    float2 UV      : TEXCOORD0;
    float4 CurClip : TEXCOORD1;   // current clip pos (for motion vectors)
    float4 PrevClip: TEXCOORD2;   // previous clip pos
};

VSOut main(VSIn v) {
    VSOut o;
    o.Sv      = mul(WorldViewProj, float4(v.Pos, 1.0));
    o.CurClip = o.Sv;
    o.PrevClip= mul(PrevWorldViewProj, float4(v.Pos, 1.0));
    o.WNorm   = normalize(mul((float3x3)World, v.Normal));
    float3 rawT = mul((float3x3)World, v.Tangent);
    o.WTangent = (dot(rawT, rawT) > 1e-6f) ? normalize(rawT) : float3(0.0, 0.0, 0.0);
    o.UV      = v.UV;
    return o;
}
