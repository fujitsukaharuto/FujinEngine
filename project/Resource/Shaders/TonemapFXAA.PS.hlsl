cbuffer TonemapCB : register(b0) {
    float BloomStrength;
    float Exposure;
    float TexelW;
    float TexelH;
    float FXAAEnabled;
    float TonemapMode;          // 0 = ACES (Narkowicz), 1 = AgX
    float VignetteIntensity;    // 0 = off
    float VignetteSoftness;     // radial start (0..1); darkening ramps from here to the corners
    float ChromaticAberration;  // 0 = off; lateral RGB split strength
    float3 _pad;
};

Texture2D    HDRTex   : register(t0);
Texture2D    BloomTex : register(t1);
SamplerState LinearSamp : register(s0);
SamplerState PointSamp  : register(s1);

// ── ACES filmic (Narkowicz 2015) — returns ~linear LDR ───────────────────────
float3 ACESFilmic(float3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// ── AgX (Minimal AgX approximation, Troy Sobotka / bwrensch) ──────────────────
// Produces display-encoded (sRGB-ish) values; we convert back to linear below so
// the single shared gamma encode at the end of main() re-encodes it correctly.
float3 AgXContrast(float3 x) {
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return  + 15.5    * x4 * x2
            - 40.14   * x4 * x
            + 31.96   * x4
            - 6.868   * x2 * x
            + 0.4298  * x2
            + 0.1191  * x
            - 0.00232;
}
float3 AgX(float3 val) {
    const float3x3 agxMat = float3x3(
        0.842479062253094,  0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772,  0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const float minEv = -12.47393;
    const float maxEv =   4.026069;
    val = mul(agxMat, val);
    val = clamp(log2(max(val, 1e-10)), minEv, maxEv);
    val = (val - minEv) / (maxEv - minEv);
    return AgXContrast(val);   // display-encoded 0..1
}

float3 Tonemap(float3 hdr) {
    hdr *= Exposure;
    if (TonemapMode < 0.5)
        return ACESFilmic(hdr);
    // AgX returns display-encoded; raise to linear so the trailing pow(1/2.2) gives back AgX output.
    return pow(saturate(AgX(hdr)), 2.2);
}

float3 SampleTonemapped(float2 uv) {
    return Tonemap(HDRTex.SampleLevel(LinearSamp, uv, 0).rgb);
}

float Luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }

// Tonemap + FXAA + bloom for one uv. Returns linear LDR (pre-gamma).
float3 ResolveLDR(float2 uv) {
    float2 rcpFrame = float2(TexelW, TexelH);
    float3 center = SampleTonemapped(uv);

    float3 ldr;
    if (FXAAEnabled < 0.5) {
        ldr = center;
    } else {
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

    // Bloom added after tonemapping (soft glow on top of LDR).
    float3 bloom = BloomTex.SampleLevel(LinearSamp, uv, 0).rgb;
    ldr += bloom * BloomStrength;
    return ldr;
}

float4 main(float4 sv : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target {
    float3 ldr;

    // Chromatic aberration: split RGB along the radial direction from screen center.
    if (ChromaticAberration > 0.0) {
        float2 offset = (uv - 0.5) * (ChromaticAberration * 0.01);
        ldr.r = ResolveLDR(uv + offset).r;
        ldr.g = ResolveLDR(uv).g;
        ldr.b = ResolveLDR(uv - offset).b;
    } else {
        ldr = ResolveLDR(uv);
    }

    // Vignette: darken toward the corners.
    if (VignetteIntensity > 0.0) {
        float dist = distance(uv, float2(0.5, 0.5)) * 1.41421356; // 0 center .. ~1 corner
        float vig  = 1.0 - VignetteIntensity * smoothstep(VignetteSoftness, 1.0, dist);
        ldr *= vig;
    }

    // Gamma encode (sRGB approx).
    ldr = pow(max(ldr, 0.0), 1.0 / 2.2);
    return float4(ldr, 1.0);
}
