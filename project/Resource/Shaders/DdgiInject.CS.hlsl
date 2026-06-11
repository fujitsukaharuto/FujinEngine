// DDGI inject — scatter the lit scene's radiance into the nearest probe's SH accumulator.
//   • Per screen pixel: reconstruct world position, take the lit HDR radiance, find the nearest probe,
//     and atomically add L·Y_k(d) (fixed-point) to that probe's 9 SH coeffs (d = probe→surface dir),
//     plus 1 to the sample count. The resolve pass turns the sums into an SH projection.
//   • Reads the lit scene BEFORE DDGI/SSGI add to it, so this is a single bounce (direct+IBL → probe);
//     no feedback loop. Radiance is clamped to keep the int accumulator from overflowing.
//   • Convention matches the other GI passes: row_major, mul(M,v), ndc=(u*2-1, 1-v*2), viewport uv.
static const float SH_SCALE = 64.0;      // fixed-point scale for the int accumulator
static const float RAD_CLAMP = 16.0;     // clamp radiance so sums stay well within int32

static const float PI = 3.14159265358979;

cbuffer DdgiInjectCB : register(b0) {
    row_major float4x4 InvViewProj;
    float4 Viewport;     // x, y, w, h
    float2 RTSize;       // full render-target size
    int2   PixelOffset;  // 0/1 per axis, rotates each frame: injects 1 of every 2x2 pixels (1/4 cost)
    float3 GridOrigin;   float _p1;
    float3 GridSpacing;  float _p2;
    uint   GX, GY, GZ;   float Feedback;   // multi-bounce: fraction of probe irradiance re-injected
};

struct ProbeSH { float sh[27]; float pad; };   // sh[coeff*3 + channel], 112-byte stride

Texture2D<float4>     SceneHDR : register(t0);   // lit scene radiance (HDR), before GI add
Texture2D<float>      DepthTex : register(t1);
RWStructuredBuffer<int> Accum  : register(u0);   // [probe*28 + (coeff*3+ch)] sums, [probe*28+27] count
StructuredBuffer<ProbeSH> Probes : register(t2); // last frame's resolved probe SH (feedback source)
Texture2D<float4>     NormalRT : register(t3);   // RT1 normal(rgb)+roughness(a)
Texture2D<float4>     AlbedoRT : register(t4);   // RT0 albedo(rgb)+metallic(a)

SamplerState PointSamp : register(s0);

float3 ReconstructWorld(float2 vuv, float depth) {
    float2 ndc   = float2(vuv.x * 2.0 - 1.0, 1.0 - vuv.y * 2.0);
    float4 world = mul(InvViewProj, float4(ndc, depth, 1.0));
    return world.xyz / world.w;
}

// SH9 irradiance eval (matches DdgiApply.CS.hlsl) — used for the multi-bounce feedback term.
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

// Indirect irradiance already stored in the probes at this surface (trilinear over the 8 cell probes).
float3 ProbeIrradiance(float3 worldPos, float3 n) {
    float3 gp   = (worldPos - GridOrigin) / GridSpacing;
    int3   base = (int3)floor(gp);
    float3 f    = saturate(gp - (float3)base);
    float3 irr = 0.0; float wsum = 0.0;
    [unroll] for (int i = 0; i < 8; ++i) {
        int3   off = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        float3 tw  = lerp(1.0 - f, f, (float3)off);
        float  w   = tw.x * tw.y * tw.z;
        if (w <= 0.0) continue;
        irr  += w * EvalSH(ProbeIndexClamped(base + off), n);
        wsum += w;
    }
    return (wsum > 1e-5) ? irr / wsum : 0.0;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    // Half-resolution dispatch: each thread maps to one pixel of a 2x2 block, the block offset rotates
    // per frame so every pixel is injected once every 4 frames (temporal blend hides the staggering).
    int2 pix = int2(DTid.xy) * 2 + PixelOffset;
    if (pix.x >= (int)RTSize.x || pix.y >= (int)RTSize.y) return;
    if (pix.x < (int)Viewport.x || pix.y < (int)Viewport.y ||
        pix.x >= (int)(Viewport.x + Viewport.z) || pix.y >= (int)(Viewport.y + Viewport.w))
        return;

    float2 fullUV = (float2(pix) + 0.5) / RTSize;
    float  depth  = DepthTex.SampleLevel(PointSamp, fullUV, 0);
    if (depth >= 1.0 || depth < 0.0001) return;   // sky / no geometry

    float2 vuv      = (float2(pix) - Viewport.xy + 0.5) / Viewport.zw;
    float3 worldPos = ReconstructWorld(vuv, depth);

    float3 gp = (worldPos - GridOrigin) / GridSpacing;
    int3   pc = (int3)round(gp);
    if (any(pc < 0) || any(pc > int3((int)GX - 1, (int)GY - 1, (int)GZ - 1))) return;

    float3 probeCenter = GridOrigin + (float3)pc * GridSpacing;
    float3 d = worldPos - probeCenter;
    float  len = length(d);
    if (len < 1e-4) return;
    d /= len;

    float3 L = SceneHDR.Load(int3(pix, 0)).rgb;
    // Multi-bounce feedback: add the diffuse the probes already light this surface with (last frame),
    // so each frame's capture folds in another bounce. albedo·E/π is the reflected indirect; Feedback<1
    // and albedo<1 keep it convergent. Feedback = 0 reproduces the original single-bounce capture.
    if (Feedback > 0.0) {
        float3 albedo = AlbedoRT.Load(int3(pix, 0)).rgb;
        float3 n      = normalize(NormalRT.Load(int3(pix, 0)).rgb * 2.0 - 1.0);
        L += albedo * (ProbeIrradiance(worldPos, n) / PI) * Feedback;
    }
    L = min(L, RAD_CLAMP);

    // SH9 basis at d.
    float x = d.x, y = d.y, z = d.z;
    float Y[9];
    Y[0] = 0.282095;
    Y[1] = 0.488603 * y;
    Y[2] = 0.488603 * z;
    Y[3] = 0.488603 * x;
    Y[4] = 1.092548 * x * y;
    Y[5] = 1.092548 * y * z;
    Y[6] = 0.315392 * (3.0 * z * z - 1.0);
    Y[7] = 1.092548 * x * z;
    Y[8] = 0.546274 * (x * x - y * y);

    uint base = (uint)(pc.z * (int)GX * (int)GY + pc.y * (int)GX + pc.x) * 28u;
    [unroll]
    for (int k = 0; k < 9; ++k) {
        int3 v = (int3)round(L * Y[k] * SH_SCALE);
        InterlockedAdd(Accum[base + (uint)(k * 3 + 0)], v.x);
        InterlockedAdd(Accum[base + (uint)(k * 3 + 1)], v.y);
        InterlockedAdd(Accum[base + (uint)(k * 3 + 2)], v.z);
    }
    InterlockedAdd(Accum[base + 27u], 1);
}
