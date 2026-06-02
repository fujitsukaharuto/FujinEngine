// Temporal Anti-Aliasing resolve (depth reprojection + neighbourhood colour clamp).
//   • Reproject this pixel into last frame using its world position (from depth) and the
//     previous frame's view-projection, sample the history, clamp it to the current 3x3
//     colour box (rejects ghosting), then blend.
//   • The scene is rendered into a sub-rectangle of the full render target (the editor's 3D
//     viewport), so NDC must be derived from VIEWPORT-relative coordinates, and history UVs
//     mapped back through the viewport. Getting this wrong makes the jitter never converge
//     (shimmer) and blends misaligned history (blur).
//   • Convention matches SSAO.CS.hlsl: row_major matrices, mul(M, v), ndc=(u*2-1, 1-v*2).
cbuffer TAACB : register(b0) {
    row_major float4x4 InvViewProjCur;   // current (jittered) inverse view-proj
    row_major float4x4 ViewProjPrev;     // previous (jittered) view-proj
    float4 Viewport;                     // x, y, w, h  (pixels within the full RT)
    float2 RTSize;                       // full render-target width, height
    float  HistoryBlend;                 // fraction of history to keep (e.g. 0.9)
    float  HistoryValid;                 // 0 on the first frame / after a resize
    float2 JitterDeltaUV;                // (curJitter - prevJitter) in viewport-uv: cancels jitter from reprojection
    float2 _pad2;
};

Texture2D<float4>   CurrentHDR : register(t0);
Texture2D<float4>   History    : register(t1);
Texture2D<float>    DepthTex   : register(t2);
Texture2D<float2>   VelocityTex: register(t3);   // screen-UV motion vector (0 where unwritten)
RWTexture2D<float4> Output     : register(u0);

SamplerState LinearSamp : register(s0);
SamplerState PointSamp  : register(s1);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint2 dims;
    Output.GetDimensions(dims.x, dims.y);
    if (DTid.x >= dims.x || DTid.y >= dims.y) return;
    int2  pix = int2(DTid.xy);

    float3 cur = CurrentHDR.Load(int3(pix, 0)).rgb;

    // Outside the rendered viewport there is no geometry to resolve → pass the pixel through.
    if (pix.x < (int)Viewport.x || pix.y < (int)Viewport.y ||
        pix.x >= (int)(Viewport.x + Viewport.z) || pix.y >= (int)(Viewport.y + Viewport.w)) {
        Output[pix] = float4(cur, 1.0);
        return;
    }

    // 3x3 colour neighbourhood (clips the history → suppresses ghosting).
    float3 nmin = cur, nmax = cur;
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    [unroll]
    for (int dx = -1; dx <= 1; ++dx) {
        int2 c = clamp(pix + int2(dx, dy), int2(0, 0), int2(dims) - 1);
        float3 s = CurrentHDR.Load(int3(c, 0)).rgb;
        nmin = min(nmin, s);
        nmax = max(nmax, s);
    }

    float2 fullUV = (float2(pix) + 0.5) / RTSize;          // where depth/history live in the full RT
    float  depth  = DepthTex.SampleLevel(PointSamp, fullUV, 0);

    float3 result = cur;
    if (HistoryValid > 0.5 && depth < 1.0 && depth > 0.0001) {
        float2 vuv = (float2(pix) - Viewport.xy + 0.5) / Viewport.zw;   // viewport-relative uv
        float2 vel = VelocityTex.Load(int3(pix, 0));

        float2 pvuv; bool valid = true;
        if (abs(vel.x) + abs(vel.y) > 1e-6) {
            // Per-pixel motion vector (objects + camera): previous viewport uv.
            pvuv = vuv - vel;
        } else {
            // No motion vector (sky / not yet supported): camera reprojection via depth + prev VP.
            float2 ndc = float2(vuv.x * 2.0 - 1.0, 1.0 - vuv.y * 2.0);
            float4 wp  = mul(InvViewProjCur, float4(ndc, depth, 1.0)); wp /= wp.w;
            float4 pc  = mul(ViewProjPrev, float4(wp.xyz, 1.0));
            valid = (pc.w > 0.0);
            pvuv  = (pc.xy / max(pc.w, 1e-6)) * float2(0.5, -0.5) + 0.5;
        }

        // The matrices used above are jittered, so both branches carry the per-frame jitter delta
        // into the reprojection. Cancel it so static geometry maps history to the same pixel
        // (otherwise the sub-pixel jitter never converges → shimmer + blur).
        pvuv += JitterDeltaUV;

        if (valid && all(pvuv >= 0.0) && all(pvuv <= 1.0)) {
            float2 prevFullUV = (Viewport.xy + pvuv * Viewport.zw) / RTSize;
            float3 hist = History.SampleLevel(LinearSamp, prevFullUV, 0).rgb;
            hist = clamp(hist, nmin, nmax);
            result = lerp(cur, hist, HistoryBlend);
        }
    }

    Output[pix] = float4(result, 1.0);
}
