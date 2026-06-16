// Beam / Ribbon pixel shader. UV.x runs along the length, UV.y across the width.
cbuffer TrailCB : register(b1) {
    int   HasTexture; float UVTiling; float UVScroll; int _t0;   // root constants set per emitter
    int   _t1; int _t2;
};

Texture2D    TrailTex  : register(t1);
SamplerState WrapSamp  : register(s1);   // wrap sampler for tiled/scrolling U

float4 main(float4 sv : SV_POSITION, float2 uv : TEXCOORD0, float4 color : COLOR) : SV_Target0 {
    // Soft edge in V direction (width falloff)
    float edge = saturate(1.0 - abs(uv.y - 0.5) * 2.2);
    edge = pow(edge, 0.7);

    float3 rgb = color.rgb;
    float  a   = color.a * edge;

    if (HasTexture != 0) {
        float2 tuv = float2(uv.x * UVTiling + UVScroll, uv.y);
        float4 t   = TrailTex.Sample(WrapSamp, tuv);
        rgb *= t.rgb;
        a   *= t.a;
    }
    return float4(rgb, a);
}
