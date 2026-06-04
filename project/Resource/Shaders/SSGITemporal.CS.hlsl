// SSGI temporal accumulation — denoises the noisy raw GI signal over time.
//   • Reproject this pixel into last frame (motion vector, with a depth-reprojection fallback for
//     sky-tagged velocity, exactly like TAA.CS.hlsl), sample the accumulated GI history, clamp it to
//     the current 3x3 GI box (rejects ghosting on moving content / disocclusion), then blend.
//   • Only the GI signal is accumulated (never the scene colour), so there is no scene ghosting; the
//     composite pass adds the cleaned GI to the scene afterwards.
//   • Convention matches TAA.CS.hlsl: row_major, mul(M,v), ndc=(u*2-1, 1-v*2), viewport-relative uv,
//     and the per-frame jitter delta is cancelled so static geometry maps history to the same pixel.
cbuffer SSGITemporalCB : register(b0) {
    row_major float4x4 InvViewProjCur;   // current (jittered) inverse view-proj
    row_major float4x4 ViewProjPrev;     // previous (jittered) view-proj
    float4 Viewport;                     // x, y, w, h  (pixels within the full RT)
    float2 RTSize;                       // full render-target size
    float  HistoryBlend;                 // fraction of history kept (e.g. 0.92)
    float  HistoryValid;                 // 0 on the first frame / after a resize
    float2 JitterDeltaUV;                // (curJitter - prevJitter) in viewport-uv
    float2 _pad;
};

Texture2D<float4>   CurrentGI : register(t0);   // raw GI (rgb) + valid flag (a)
Texture2D<float4>   History   : register(t1);   // accumulated GI (rgb) + valid (a)
Texture2D<float>    DepthTex  : register(t2);
Texture2D<float2>   VelocityTex : register(t3);
RWTexture2D<float4> Output    : register(u0);

SamplerState LinearSamp : register(s0);
SamplerState PointSamp  : register(s1);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint2 dims; Output.GetDimensions(dims.x, dims.y);
    if (DTid.x >= dims.x || DTid.y >= dims.y) return;
    int2 pix = int2(DTid.xy);

    float4 cur = CurrentGI.Load(int3(pix, 0));
    // Sky / outside viewport (invalid) → no GI, nothing to accumulate.
    if (cur.a < 0.5) { Output[pix] = float4(0.0, 0.0, 0.0, 0.0); return; }

    // 3x3 GI neighbourhood box (clamps history → suppresses ghosting / lag bleed).
    float3 nmin = cur.rgb, nmax = cur.rgb;
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    [unroll]
    for (int dx = -1; dx <= 1; ++dx) {
        int2 c = clamp(pix + int2(dx, dy), int2(0, 0), int2(dims) - 1);
        float3 s = CurrentGI.Load(int3(c, 0)).rgb;
        nmin = min(nmin, s);
        nmax = max(nmax, s);
    }

    float3 result = cur.rgb;
    float2 fullUV = (float2(pix) + 0.5) / RTSize;
    float  depth  = DepthTex.SampleLevel(PointSamp, fullUV, 0);

    if (HistoryValid > 0.5 && depth < 1.0 && depth > 0.0001) {
        float2 vuv = (float2(pix) - Viewport.xy + 0.5) / Viewport.zw;
        float2 vel = VelocityTex.Load(int3(pix, 0));

        float2 pvuv; bool valid = true;
        if (abs(vel.x) + abs(vel.y) > 1e-6) {
            pvuv = vuv - vel;                                  // per-pixel motion (objects + camera)
        } else {
            float2 ndc = float2(vuv.x * 2.0 - 1.0, 1.0 - vuv.y * 2.0);
            float4 wp  = mul(InvViewProjCur, float4(ndc, depth, 1.0)); wp /= wp.w;
            float4 pc  = mul(ViewProjPrev, float4(wp.xyz, 1.0));
            valid = (pc.w > 0.0);
            pvuv  = (pc.xy / max(pc.w, 1e-6)) * float2(0.5, -0.5) + 0.5;
        }
        pvuv += JitterDeltaUV;   // cancel the per-frame jitter (else static GI never converges)

        if (valid && all(pvuv >= 0.0) && all(pvuv <= 1.0)) {
            float2 prevFullUV = (Viewport.xy + pvuv * Viewport.zw) / RTSize;
            float4 hist = History.SampleLevel(LinearSamp, prevFullUV, 0);
            if (hist.a > 0.5) {
                float3 hc = clamp(hist.rgb, nmin, nmax);
                result = lerp(cur.rgb, hc, HistoryBlend);
            }
        }
    }

    Output[pix] = float4(result, 1.0);
}
