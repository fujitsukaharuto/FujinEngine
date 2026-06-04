// SSGI composite — edge-aware spatial denoise of the accumulated GI + add to the scene.
//   • Bilateral blur of the (temporally accumulated) GI guided by depth + normal so it smooths the
//     residual noise without bleeding light across geometry edges. GI is low-frequency, so a small
//     radius is plenty; BlurRadius 0 disables the spatial pass (temporal only).
//   • Output = scene + GI · Intensity. Sky / outside-viewport pixels keep the scene unchanged
//     (GI is 0 / invalid there), so the result copied back into the HDR RT is a pure pass-through
//     wherever there is no GI — it can only ever ADD bounced light.
//   • Convention matches the trace/temporal passes (viewport-relative, full-RT uv sampling).
cbuffer SSGICompositeCB : register(b0) {
    float4 Viewport;    // x, y, w, h
    float2 RTSize;      // full render-target size
    float  Intensity;   // overall GI strength
    int    BlurRadius;  // bilateral half-extent in pixels (0 = temporal only)
    float  DepthSigma;  // relative depth tolerance for the bilateral weight
    float3 _pad;
};

Texture2D<float4>   SceneHDR : register(t0);   // lit scene (HDR)
Texture2D<float4>   GITex    : register(t1);   // accumulated GI (rgb) + valid (a)
Texture2D<float>    DepthTex : register(t2);
Texture2D<float4>   NormalRT : register(t3);   // GBuffer RT1 normal(rgb)+roughness(a)
RWTexture2D<float4> Output   : register(u0);

SamplerState LinearSamp : register(s0);
SamplerState PointSamp  : register(s1);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint2 dims; Output.GetDimensions(dims.x, dims.y);
    if (DTid.x >= dims.x || DTid.y >= dims.y) return;
    int2 pix = int2(DTid.xy);

    float3 scene = SceneHDR.Load(int3(pix, 0)).rgb;
    Output[pix] = float4(scene, 1.0);   // default pass-through (sky / outside viewport)

    if (pix.x < (int)Viewport.x || pix.y < (int)Viewport.y ||
        pix.x >= (int)(Viewport.x + Viewport.z) || pix.y >= (int)(Viewport.y + Viewport.w))
        return;

    float4 giC = GITex.Load(int3(pix, 0));
    if (giC.a < 0.5) return;             // no GI at this pixel (sky / no geometry)

    float3 gi = giC.rgb;

    if (BlurRadius > 0) {
        float  cDepth = DepthTex.Load(int3(pix, 0));
        float3 cN     = normalize(NormalRT.Load(int3(pix, 0)).rgb * 2.0 - 1.0);
        float3 sum    = 0.0;
        float  wsum   = 0.0;
        int    R      = min(BlurRadius, 4);
        float  dTol   = max(DepthSigma, 1e-5) * max(cDepth, 1e-4);
        [loop]
        for (int dy = -R; dy <= R; ++dy)
        [loop]
        for (int dx = -R; dx <= R; ++dx) {
            int2 q = clamp(pix + int2(dx, dy), int2(0, 0), int2(dims) - 1);
            float4 g = GITex.Load(int3(q, 0));
            if (g.a < 0.5) continue;
            float  qd = DepthTex.Load(int3(q, 0));
            float3 qn = normalize(NormalRT.Load(int3(q, 0)).rgb * 2.0 - 1.0);
            float  wd = exp(-abs(qd - cDepth) / dTol);
            float  wn = saturate(dot(cN, qn)); wn *= wn; wn *= wn;   // ^4, sharp normal falloff
            float  w  = wd * wn;
            sum  += g.rgb * w;
            wsum += w;
        }
        if (wsum > 1e-5) gi = sum / wsum;
    }

    Output[pix] = float4(scene + gi * Intensity, 1.0);
}
