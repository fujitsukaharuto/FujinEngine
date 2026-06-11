// Screen-space reflections (world-space ray march against the depth buffer).
//   • Reconstruct the pixel's world position + normal, reflect the view ray, then march the
//     reflection ray in world space. At each step project to screen and compare the marched
//     point's camera distance against the stored surface there; a hit samples the lit HDR.
//   • Additive: Output = scene + reflectionColor * (fresnel · (1-roughness) · edgeFade · Intensity).
//     With Intensity 0 (or rough surfaces) it is a pure pass-through, so it can't regress the look.
//   • Convention matches SSAO/TAA: row_major matrices, mul(M,v), ndc=(u*2-1, 1-v*2). The scene is
//     rendered into a viewport sub-rectangle of the full RTs, so NDC is viewport-relative and
//     sampling maps viewport-uv back to full-RT uv.
static const float PI = 3.14159265358979;

cbuffer SSRCB : register(b0) {
    row_major float4x4 InvViewProj;
    row_major float4x4 ViewProj;
    float3 CameraPos;   float _p0;
    float4 Viewport;    // x, y, w, h  (pixels within the full RT)
    float2 RTSize;      // full render-target size
    float  MaxDistance; // not used directly (Stride*MaxSteps bounds it); kept for tuning
    float  Thickness;   // world-space tolerance for accepting a hit behind the surface
    int    MaxSteps;
    float  Stride;          // world-space march step length
    float  RoughnessCutoff; // surfaces rougher than this get no reflection
    float  Intensity;       // overall reflection strength (0 => disabled look)
    float3 GridOrigin;   float DdgiFallback;  // off-screen fallback strength (0 = none = SSR-only)
    float3 GridSpacing;  float _p1;
    uint   GX, GY, GZ;   uint  _p2;
};

struct ProbeSH { float sh[27]; float pad; };   // sh[coeff*3 + channel], 112-byte stride

Texture2D<float4>   SceneHDR : register(t0);   // lit scene colour (HDR)
Texture2D<float>    DepthTex : register(t1);   // depth buffer (NDC z, [0,1])
Texture2D<float4>   NormalRT : register(t2);   // GBuffer RT1: encoded world normal (rgb) + roughness (a)
StructuredBuffer<ProbeSH> Probes : register(t3); // DDGI probe SH (off-screen reflection fallback)
RWTexture2D<float4> Output   : register(u0);

SamplerState LinearSamp : register(s0);
SamplerState PointSamp  : register(s1);

// viewport-relative uv → world position
float3 ReconstructWorld(float2 vuv, float depth) {
    float2 ndc   = float2(vuv.x * 2.0 - 1.0, 1.0 - vuv.y * 2.0);
    float4 world = mul(InvViewProj, float4(ndc, depth, 1.0));
    return world.xyz / world.w;
}

// SH9 radiance eval along a direction (matches DdgiApply.CS.hlsl). For an off-screen reflection we
// evaluate the probe SH along the reflection vector → a low-frequency environment colour.
float3 EvalSH(uint pi, float3 n) {
    float3 L[9];
    [unroll] for (int k = 0; k < 9; ++k)
        L[k] = float3(Probes[pi].sh[k * 3 + 0], Probes[pi].sh[k * 3 + 1], Probes[pi].sh[k * 3 + 2]);
    const float c1 = 0.429043, c2 = 0.511664, c3 = 0.743125, c4 = 0.886227, c5 = 0.247708;
    float x = n.x, y = n.y, z = n.z;
    float3 E = c4 * L[0]
             + 2.0 * c2 * (L[1] * y + L[2] * z + L[3] * x)
             + c3 * L[6] * (z * z) - c5 * L[6]
             + 2.0 * c1 * (L[4] * (x * y) + L[5] * (y * z) + L[7] * (x * z))
             + c1 * L[8] * (x * x - y * y);
    return max(E, 0.0);
}

uint ProbeIndexClamped(int3 c) {
    c = clamp(c, int3(0, 0, 0), int3((int)GX - 1, (int)GY - 1, (int)GZ - 1));
    return (uint)(c.z * (int)GX * (int)GY + c.y * (int)GX + c.x);
}

// Probe radiance toward direction `dir` at `worldPos` (trilinear over the 8 cell probes). Returns 0
// outside the volume / before any probe capture, so the fallback is harmless when DDGI is off.
float3 ProbeRadiance(float3 worldPos, float3 dir) {
    float3 gp = (worldPos - GridOrigin) / GridSpacing;
    if (any(gp < -0.5) || any(gp > float3(GX, GY, GZ) - 0.5)) return 0.0;
    int3   base = (int3)floor(gp);
    float3 f    = saturate(gp - (float3)base);
    float3 rad = 0.0; float wsum = 0.0;
    [unroll] for (int i = 0; i < 8; ++i) {
        int3   off = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        float3 tw  = lerp(1.0 - f, f, (float3)off);
        float  w   = tw.x * tw.y * tw.z;
        if (w <= 0.0) continue;
        rad  += w * EvalSH(ProbeIndexClamped(base + off), dir);
        wsum += w;
    }
    return (wsum > 1e-5) ? rad / wsum : 0.0;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint2 dims; Output.GetDimensions(dims.x, dims.y);
    if (DTid.x >= dims.x || DTid.y >= dims.y) return;
    int2 pix = int2(DTid.xy);

    float3 scene = SceneHDR.Load(int3(pix, 0)).rgb;
    Output[pix] = float4(scene, 1.0);   // default pass-through

    // Inside the rendered viewport only.
    if (pix.x < (int)Viewport.x || pix.y < (int)Viewport.y ||
        pix.x >= (int)(Viewport.x + Viewport.z) || pix.y >= (int)(Viewport.y + Viewport.w))
        return;
    if (Intensity <= 0.0) return;

    float2 fullUV = (float2(pix) + 0.5) / RTSize;
    float  depth  = DepthTex.SampleLevel(PointSamp, fullUV, 0);
    if (depth >= 1.0 || depth < 0.0001) return;   // sky / no geometry

    float4 nr        = NormalRT.SampleLevel(PointSamp, fullUV, 0);
    float  roughness = nr.a;
    if (roughness >= RoughnessCutoff) return;     // too rough to mirror
    float3 normal = normalize(nr.rgb * 2.0 - 1.0);

    float2 vuv      = (float2(pix) - Viewport.xy + 0.5) / Viewport.zw;
    float3 worldPos = ReconstructWorld(vuv, depth);
    float3 viewDir  = normalize(worldPos - CameraPos);
    float3 R        = reflect(viewDir, normal);
    if (dot(R, viewDir) < 0.0) return;            // reflection points back at camera → skip

    // March the reflection ray in world space.
    float3 p = worldPos + normal * 0.02 + R * Stride;
    bool   hit = false;
    float2 hitUV = float2(0, 0);
    [loop]
    for (int i = 0; i < MaxSteps; ++i) {
        float4 clip = mul(ViewProj, float4(p, 1.0));
        if (clip.w <= 1e-4) break;                // behind camera
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
                // Binary-search refine the crossing for a sharper contact.
                float3 lo = p - R * Stride, hiP = p;
                [unroll]
                for (int k = 0; k < 5; ++k) {
                    float3 mid = (lo + hiP) * 0.5;
                    float4 mc  = mul(ViewProj, float4(mid, 1.0));
                    float2 muv = (mc.xy / mc.w) * float2(0.5, -0.5) + 0.5;
                    float2 mF  = (Viewport.xy + muv * Viewport.zw) / RTSize;
                    float  mz  = DepthTex.SampleLevel(PointSamp, mF, 0);
                    float3 mW  = ReconstructWorld(muv, mz);
                    if (length(mid - CameraPos) > length(mW - CameraPos)) hiP = mid; else lo = mid;
                    svuv = muv;
                }
                hitUV = (Viewport.xy + svuv * Viewport.zw) / RTSize;
                hit = true;
                break;
            }
        }
        p += R * Stride;
    }

    // Common reflection weights: Schlick fresnel + roughness falloff.
    float  cosT  = saturate(dot(-viewDir, normal));
    float  fres  = 0.04 + 0.96 * pow(1.0 - cosT, 5.0);
    float  rough = 1.0 - saturate(roughness / RoughnessCutoff);

    float3 refl;
    float  weight;
    if (hit) {
        refl = SceneHDR.SampleLevel(LinearSamp, hitUV, 0).rgb;
        // Fade as the hit nears the screen edge (screen-space data runs out there).
        float2 huv  = (hitUV * RTSize - Viewport.xy) / Viewport.zw;
        float2 edge = smoothstep(0.0, 0.15, huv) * smoothstep(0.0, 0.15, 1.0 - huv);
        weight = saturate(Intensity * fres * rough * edge.x * edge.y);
    } else {
        // Off-screen / no hit → fall back to the DDGI probe radiance along the reflection vector so the
        // reflection stays continuous instead of cutting to nothing (the Lumen-lite cohesion step).
        if (DdgiFallback <= 0.0) return;
        refl   = ProbeRadiance(worldPos, R);
        weight = saturate(Intensity * fres * rough * DdgiFallback);
    }

    Output[pix] = float4(scene + refl * weight, 1.0);
}
