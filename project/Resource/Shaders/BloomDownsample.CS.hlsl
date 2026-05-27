// 2× downsample with luminance threshold: HDR full-res → bloom half-res
cbuffer BloomCB : register(b0) {
    float2 InTexelSize;  // 1/inputWidth, 1/inputHeight
    float  Threshold;
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

    // 4-tap bilinear at half-res coordinates (each tap covers 2×2 input pixels)
    float2 ts = InTexelSize * 0.5;
    float4 s =  Input.SampleLevel(LinearSamp, uv + float2(-ts.x, -ts.y), 0)
             +  Input.SampleLevel(LinearSamp, uv + float2( ts.x, -ts.y), 0)
             +  Input.SampleLevel(LinearSamp, uv + float2(-ts.x,  ts.y), 0)
             +  Input.SampleLevel(LinearSamp, uv + float2( ts.x,  ts.y), 0);
    s *= 0.25;

    // Soft threshold: only pass bright pixels
    float brightness = max(s.r, max(s.g, s.b));
    float knee       = Threshold * 0.5;
    float rq         = clamp(brightness - Threshold + knee, 0.0, 2.0 * knee);
    rq               = (knee > 0.0) ? (rq * rq / (4.0 * knee + 0.00001)) : 0.0;
    s.rgb *= max(rq, brightness - Threshold) / max(brightness, 0.00001);

    Output[DTid.xy] = s;
}
