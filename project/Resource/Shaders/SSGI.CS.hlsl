// Screen-space global illumination — one-bounce indirect diffuse (TRACE pass).
//   • For each pixel, reconstruct its world position + normal, then shoot RayCount cosine-weighted
//     hemisphere rays around the normal and march each against the depth buffer (world space).
//     On a hit, the lit HDR colour there is the incoming radiance for that direction.
//   • Cosine-weighted Monte-Carlo estimator for Lambertian diffuse collapses to
//     indirect = albedo · mean(incomingRadiance), so it only ever ADDS bounced light.
//   • This is the noisy TRACE pass: it outputs the RAW GI signal (rgb = albedo·indirect, BEFORE the
//     Intensity multiply) plus a validity flag in alpha (1 = had geometry, 0 = sky / outside viewport).
//     A temporal-accumulation pass denoises it and a composite pass adds it (×Intensity) to the scene,
//     so the look only regresses when SSGI is explicitly enabled.
//   • Convention matches SSR/SSAO/TAA exactly: row_major matrices, mul(M,v), ndc=(u*2-1, 1-v*2).
//     The scene is rendered into a viewport sub-rectangle of the full RTs, so NDC is viewport-relative
//     and depth/HDR sampling maps the viewport uv back to full-RT uv.
//   • Performance: cost ≈ RayCount·StepCount depth samples / pixel. Kept low by default; the pass is
//     off by default so there is zero cost unless explicitly enabled.
cbuffer SSGICB : register(b0) {
    row_major float4x4 InvViewProj;
    row_major float4x4 ViewProj;
    float3 CameraPos;   float _p0;
    float4 Viewport;    // x, y, w, h  (pixels within the full RT)
    float2 RTSize;      // full render-target size
    float  Radius;      // world-space max march distance per ray
    float  Thickness;   // world-space tolerance for accepting a hit behind the surface
    int    RayCount;    // hemisphere rays per pixel
    int    StepCount;   // march steps per ray
    float  Intensity;   // (unused in trace; applied at composite) kept for CB layout parity
    float  FrameJitter; // per-frame seed so sampling rotates over time (denoised temporally)
};

Texture2D<float4>   SceneHDR : register(t0);   // lit scene colour (HDR)
Texture2D<float>    DepthTex : register(t1);   // depth buffer (NDC z, [0,1])
Texture2D<float4>   NormalRT : register(t2);   // GBuffer RT1: world normal (rgb) + roughness (a)
Texture2D<float4>   AlbedoRT : register(t3);   // GBuffer RT0: albedo (rgb) + metallic (a)
RWTexture2D<float4> Output   : register(u0);

SamplerState LinearSamp : register(s0);
SamplerState PointSamp  : register(s1);

// viewport-relative uv → world position
float3 ReconstructWorld(float2 vuv, float depth) {
    float2 ndc   = float2(vuv.x * 2.0 - 1.0, 1.0 - vuv.y * 2.0);
    float4 world = mul(InvViewProj, float4(ndc, depth, 1.0));
    return world.xyz / world.w;
}

// cheap hash → [0,1)
float Hash12(float2 p) {
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint2 dims; Output.GetDimensions(dims.x, dims.y);
    if (DTid.x >= dims.x || DTid.y >= dims.y) return;
    int2 pix = int2(DTid.xy);

    Output[pix] = float4(0.0, 0.0, 0.0, 0.0);   // default: no GI, invalid (sky / outside)

    // Inside the rendered viewport only.
    if (pix.x < (int)Viewport.x || pix.y < (int)Viewport.y ||
        pix.x >= (int)(Viewport.x + Viewport.z) || pix.y >= (int)(Viewport.y + Viewport.w))
        return;

    float2 fullUV = (float2(pix) + 0.5) / RTSize;
    float  depth  = DepthTex.SampleLevel(PointSamp, fullUV, 0);
    if (depth >= 1.0 || depth < 0.0001) return;   // sky / no geometry

    float3 albedo = AlbedoRT.SampleLevel(PointSamp, fullUV, 0).rgb;
    float3 normal = normalize(NormalRT.SampleLevel(PointSamp, fullUV, 0).rgb * 2.0 - 1.0);

    float2 vuv      = (float2(pix) - Viewport.xy + 0.5) / Viewport.zw;
    float3 worldPos = ReconstructWorld(vuv, depth);

    // Orthonormal basis around the surface normal.
    float3 up = abs(normal.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T  = normalize(cross(up, normal));
    float3 B  = cross(normal, T);

    float rnd    = Hash12(float2(pix) + FrameJitter);
    float stride = Radius / max((float)StepCount, 1.0);

    float3 indirect = 0.0;
    int    rays     = max(RayCount, 1);
    [loop]
    for (int r = 0; r < rays; ++r) {
        // Stratified cosine-weighted hemisphere sample.
        float u1  = frac(rnd + (float)r / (float)rays);
        float u2  = Hash12(float2(pix) + float2((float)r * 1.7, FrameJitter * 2.3 + 1.0));
        float phi = 6.2831853 * u1;
        float ct  = sqrt(1.0 - u2);    // cos(theta)
        float st  = sqrt(u2);          // sin(theta)
        float3 dir = (T * cos(phi) + B * sin(phi)) * st + normal * ct;

        float3 p = worldPos + normal * 0.03 + dir * stride;
        [loop]
        for (int s = 0; s < StepCount; ++s) {
            float4 clip = mul(ViewProj, float4(p, 1.0));
            if (clip.w <= 1e-4) break;                 // behind camera
            float2 sndc = clip.xy / clip.w;
            float2 svuv = sndc * float2(0.5, -0.5) + 0.5;
            if (any(svuv < 0.0) || any(svuv > 1.0)) break;   // left the viewport

            float2 sFull   = (Viewport.xy + svuv * Viewport.zw) / RTSize;
            float  storedZ = DepthTex.SampleLevel(PointSamp, sFull, 0);
            if (storedZ < 1.0 && storedZ > 0.0001) {
                float3 storedW = ReconstructWorld(svuv, storedZ);
                float  dp = length(p - CameraPos);          // marched point distance
                float  ds = length(storedW - CameraPos);    // surface distance at that pixel
                if (dp > ds && (dp - ds) < Thickness) {
                    float3 rad = SceneHDR.SampleLevel(LinearSamp, sFull, 0).rgb;
                    rad = min(rad, 4.0);                     // clamp fireflies
                    indirect += rad;
                    break;
                }
            }
            p += dir * stride;
        }
    }

    // Raw GI signal (albedo · mean radiance). Intensity is applied later, at composite, so the
    // accumulated history stays valid when the user drags the Intensity slider.
    indirect = albedo * indirect / (float)rays;
    Output[pix] = float4(indirect, 1.0);   // a = 1 → valid (had geometry)
}
