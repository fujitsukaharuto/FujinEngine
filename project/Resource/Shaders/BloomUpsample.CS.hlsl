// Kawase blur pass: used for iterative ping-pong bloom blurring
cbuffer BloomCB : register(b0) {
    float2 InTexelSize;   // 1/inputWidth, 1/inputHeight
    float  KawaseOffset;  // offset multiplier: 0.5, 1.5, 2.5, 4.5 …
    float  _pad;
};

Texture2D<float4>   Input  : register(t0);
RWTexture2D<float4> Output : register(u0);
SamplerState        LinearSamp : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint2 dims;
    Output.GetDimensions(dims.x, dims.y);
    if (DTid.x >= dims.x || DTid.y >= dims.y) return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(dims);
    float2 ts = InTexelSize * KawaseOffset;

    // 4-tap bilinear Kawase
    float4 result =
        Input.SampleLevel(LinearSamp, uv + float2( ts.x,  ts.y), 0) +
        Input.SampleLevel(LinearSamp, uv + float2(-ts.x,  ts.y), 0) +
        Input.SampleLevel(LinearSamp, uv + float2(-ts.x, -ts.y), 0) +
        Input.SampleLevel(LinearSamp, uv + float2( ts.x, -ts.y), 0);

    Output[DTid.xy] = result * 0.25;
}
