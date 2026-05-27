cbuffer ShadowObj : register(b0) {
    row_major float4x4 LightWVP;
};

struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut main(float3 pos : POSITION, float2 uv : TEXCOORD) {
    VSOut o;
    o.pos = mul(LightWVP, float4(pos, 1.0));
    o.uv  = uv;
    return o;
}
