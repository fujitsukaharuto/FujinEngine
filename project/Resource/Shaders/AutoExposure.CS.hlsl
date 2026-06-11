// Auto-exposure metering: one threadgroup samples a 16x16 grid across the HDR
// scene and reduces the geometric-mean luminance into a single float. The CPU
// reads this back (a couple of frames later), does the temporal eye-adaptation,
// and feeds the resulting exposure into the tonemap CB. Cheap (256 taps, 1 group).

Texture2D<float4> HDRTex : register(t0);
RWBuffer<float>   OutLum : register(u0);
SamplerState      Samp   : register(s0);

groupshared float gLum[256];

[numthreads(16, 16, 1)]
void main(uint3 gt : SV_GroupThreadID, uint gi : SV_GroupIndex) {
    // Sample the screen at the center of each 16x16 cell.
    float2 uv  = (float2(gt.xy) + 0.5) / 16.0;
    float3 c   = HDRTex.SampleLevel(Samp, uv, 0).rgb;
    float  lum = max(dot(c, float3(0.2126, 0.7152, 0.0722)), 1e-4);

    gLum[gi] = log(lum);
    GroupMemoryBarrierWithGroupSync();

    // Parallel sum reduction over the 256 samples.
    [unroll]
    for (uint s = 128; s > 0; s >>= 1) {
        if (gi < s) gLum[gi] += gLum[gi + s];
        GroupMemoryBarrierWithGroupSync();
    }

    if (gi == 0)
        OutLum[0] = exp(gLum[0] / 256.0); // geometric-mean luminance
}
