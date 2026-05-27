cbuffer TonemapCB : register(b0) {
    float BloomStrength;
    float Exposure;
    float TexelW;
    float TexelH;
    float FXAAEnabled;
    float3 _pad;
};

Texture2D    HDRTex   : register(t0);
Texture2D    BloomTex : register(t1);
SamplerState LinearSamp : register(s0);
SamplerState PointSamp  : register(s1);

// ACES filmic tone mapping approximation (Narkowicz 2015)
float3 ACESFilmic(float3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Tonemap HDR scene only (no bloom) — used as FXAA input so edges stay sharp
float3 SampleTonemapped(float2 uv) {
    float3 hdr = HDRTex.SampleLevel(LinearSamp, uv, 0).rgb;
    return ACESFilmic(hdr * Exposure);
}

float Luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }

float4 main(float4 sv : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target {
    float2 rcpFrame = float2(TexelW, TexelH);

    float3 center = SampleTonemapped(uv);

    float3 ldr;
    if (FXAAEnabled < 0.5) {
        ldr = center;
    } else {
        // FXAA — simplified Lottes variant (runs on tonemapped scene without bloom)
        float3 rgbNW = SampleTonemapped(uv + float2(-1,-1) * rcpFrame);
        float3 rgbNE = SampleTonemapped(uv + float2( 1,-1) * rcpFrame);
        float3 rgbSW = SampleTonemapped(uv + float2(-1, 1) * rcpFrame);
        float3 rgbSE = SampleTonemapped(uv + float2( 1, 1) * rcpFrame);

        float lumaNW = Luma(rgbNW), lumaNE = Luma(rgbNE);
        float lumaSW = Luma(rgbSW), lumaSE = Luma(rgbSE);
        float lumaM  = Luma(center);

        float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
        float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

        float lumaRange = lumaMax - lumaMin;
        if (lumaRange < max(0.0312, lumaMax * 0.125)) {
            ldr = center;
        } else {
            float2 dir = float2(
                -((lumaNW + lumaNE) - (lumaSW + lumaSE)),
                 ((lumaNW + lumaSW) - (lumaNE + lumaSE)));

            float dirReduce  = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.03125, 0.0078125);
            float rcpDirMin  = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
            dir = clamp(dir * rcpDirMin, -8.0, 8.0) * rcpFrame;

            float3 rgbA = 0.5 * (SampleTonemapped(uv + dir * (1.0/3.0 - 0.5)) +
                                  SampleTonemapped(uv + dir * (2.0/3.0 - 0.5)));
            float3 rgbB = rgbA * 0.5 + 0.25 * (SampleTonemapped(uv + dir * -0.5) +
                                                 SampleTonemapped(uv + dir *  0.5));

            float lumaB = Luma(rgbB);
            ldr = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
        }
    }

    // Add bloom additively AFTER tonemapping (same as pre-engine approach).
    // Bloom is already a blurred/soft texture, so adding it on top of LDR gives
    // a visible glow without being compressed by the tone curve.
    float3 bloom = BloomTex.SampleLevel(LinearSamp, uv, 0).rgb;
    ldr += bloom * BloomStrength;

    // Gamma correction
    ldr = pow(max(ldr, 0.0), 1.0 / 2.2);
    return float4(ldr, 1.0);
}
