cbuffer PerPass : register(b0) {
    row_major float4x4 ViewProj;
    float3 _CamRight; float _p0;
    float3 _CamUp;    float _p1;
};

struct VSIn {
    float3 Pos   : POSITION;
    float2 UV    : TEXCOORD0;
    float4 Color : COLOR;
};

struct VSOut {
    float4 SvPos : SV_POSITION;
    float2 UV    : TEXCOORD0;
    float4 Color : COLOR;
};

VSOut main(VSIn v) {
    VSOut o;
    o.SvPos = mul(ViewProj, float4(v.Pos, 1.0));
    o.UV    = v.UV;
    o.Color = v.Color;
    return o;
}
