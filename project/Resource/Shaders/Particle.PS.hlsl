cbuffer SubUVCB : register(b1) {
    int SubUVCols; int SubUVRows; int HasTexture; int _su;
};

Texture2D    SpriteTex : register(t1);   // t0 is the GPU particle buffer (VS); use t1 for the sprite
SamplerState SpriteSamp : register(s0);

float4 main(float4 sv : SV_POSITION, float2 uv : TEXCOORD0, float4 color : COLOR) : SV_Target0 {
    if (HasTexture != 0) {
        // uv is already remapped to the flipbook frame's sub-rect by the VS.
        float4 tex = SpriteTex.Sample(SpriteSamp, uv);
        return float4(color.rgb * tex.rgb, color.a * tex.a);
    }
    // Procedural soft-circle sprite (no texture set)
    float2 c = uv - 0.5;
    float  r = dot(c, c) * 4.0;     // 0 at center, 1 at edge
    float  a = saturate(1.0 - r);
    a = pow(a, 0.6);                 // softer falloff
    return float4(color.rgb, color.a * a);
}
