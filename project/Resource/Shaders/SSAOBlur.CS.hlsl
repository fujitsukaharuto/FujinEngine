Texture2D<float>   SSAOInput  : register(t0);
RWTexture2D<float> Output     : register(u0);
SamplerState       LinearSamp : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint2 dims;
    Output.GetDimensions(dims.x, dims.y);
    if (DTid.x >= dims.x || DTid.y >= dims.y) return;

    float2 texel = 1.0 / float2(dims);
    float2 uv    = (float2(DTid.xy) + 0.5) * texel;

    // 4×4 box blur
    float result = 0.0;
    [unroll] for (int y = -1; y <= 2; ++y)
    [unroll] for (int x = -1; x <= 2; ++x)
        result += SSAOInput.SampleLevel(LinearSamp, uv + float2(x, y) * texel, 0);

    Output[DTid.xy] = result / 16.0;
}
