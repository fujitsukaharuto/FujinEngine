cbuffer ShadowObj : register(b0) {
    row_major float4x4 LightWVP;
};

float4 main(float3 pos : POSITION) : SV_POSITION {
    return mul(LightWVP, float4(pos, 1.0));
}
