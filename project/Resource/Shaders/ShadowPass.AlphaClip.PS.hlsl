Texture2D    AlbedoTex  : register(t0);
SamplerState LinearWrap : register(s0);

void main(float4 sv : SV_POSITION, float2 uv : TEXCOORD0) {
    clip(AlbedoTex.Sample(LinearWrap, uv).a - 0.5);
}
