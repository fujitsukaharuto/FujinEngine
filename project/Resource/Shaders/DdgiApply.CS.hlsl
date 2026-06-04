// DDGI apply — adds off-screen one-bounce indirect diffuse from the probe volume.
//   • Per pixel: reconstruct world position + normal, locate the surrounding 2x2x2 probe cell,
//     trilinearly blend the 8 probes, evaluate each probe's SH9 along the normal → indirect irradiance.
//   • indirect = albedo · E/PI · Intensity, ADDED to the scene. Pixels outside the probe volume / on
//     the sky get nothing, so with Intensity 0 it is a pure pass-through and cannot regress the look.
//   • This complements SSGI: SSGI = on-screen near-field detail, DDGI = off-screen / far-field base.
//   • Convention matches SSGI/SSR/TAA: row_major, mul(M,v), ndc=(u*2-1, 1-v*2), viewport-relative uv.
static const float PI = 3.14159265358979;

cbuffer DdgiCB : register(b0) {
    row_major float4x4 InvViewProj;
    float4 Viewport;     // x, y, w, h (pixels within the full RT)
    float2 RTSize;       // full render-target size
    float  Intensity;    // GI strength (0 => disabled look)
    float  _p0;
    float3 GridOrigin;   float _p1;   // world position of probe (0,0,0)
    float3 GridSpacing;  float _p2;   // world distance between adjacent probes
    uint   GX, GY, GZ;   uint  _p3;   // probe count per axis
};

struct ProbeSH { float sh[27]; float pad; };   // sh[coeff*3 + channel], 112-byte stride

Texture2D<float4>          SceneHDR : register(t0);
Texture2D<float>           DepthTex : register(t1);
Texture2D<float4>          NormalRT : register(t2);   // RT1 normal(rgb)+roughness(a)
Texture2D<float4>          AlbedoRT : register(t3);   // RT0 albedo(rgb)+metallic(a)
StructuredBuffer<ProbeSH>  Probes   : register(t4);
RWTexture2D<float4>        Output    : register(u0);

SamplerState LinearSamp : register(s0);
SamplerState PointSamp  : register(s1);

float3 ReconstructWorld(float2 vuv, float depth) {
    float2 ndc   = float2(vuv.x * 2.0 - 1.0, 1.0 - vuv.y * 2.0);
    float4 world = mul(InvViewProj, float4(ndc, depth, 1.0));
    return world.xyz / world.w;
}

// SH9 (L2) irradiance evaluation (Ramamoorthi/Green). Returns E along n; the stored coeffs are the
// radiance SH projection, so for a flat-seeded probe (only DC set) this returns the flat colour.
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

uint ProbeIndex(int3 c) {
    c = clamp(c, int3(0, 0, 0), int3((int)GX - 1, (int)GY - 1, (int)GZ - 1));
    return (uint)(c.z * (int)GX * (int)GY + c.y * (int)GX + c.x);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint2 dims; Output.GetDimensions(dims.x, dims.y);
    if (DTid.x >= dims.x || DTid.y >= dims.y) return;
    int2 pix = int2(DTid.xy);

    float3 scene = SceneHDR.Load(int3(pix, 0)).rgb;
    Output[pix] = float4(scene, 1.0);   // default pass-through

    if (pix.x < (int)Viewport.x || pix.y < (int)Viewport.y ||
        pix.x >= (int)(Viewport.x + Viewport.z) || pix.y >= (int)(Viewport.y + Viewport.w))
        return;
    if (Intensity <= 0.0) return;

    float2 fullUV = (float2(pix) + 0.5) / RTSize;
    float  depth  = DepthTex.SampleLevel(PointSamp, fullUV, 0);
    if (depth >= 1.0 || depth < 0.0001) return;   // sky / no geometry

    float3 albedo = AlbedoRT.SampleLevel(PointSamp, fullUV, 0).rgb;
    float3 normal = normalize(NormalRT.SampleLevel(PointSamp, fullUV, 0).rgb * 2.0 - 1.0);

    float2 vuv      = (float2(pix) - Viewport.xy + 0.5) / Viewport.zw;
    float3 worldPos = ReconstructWorld(vuv, depth);

    // Grid coordinate of this surface (in probe units). Skip pixels fully outside the volume.
    float3 gp = (worldPos - GridOrigin) / GridSpacing;
    if (any(gp < -0.5) || any(gp > float3(GX, GY, GZ) - 0.5)) return;

    // Smoothly fade GI to 0 within ~1.5 probes of the volume boundary so the volume edge does not
    // appear as a hard square on the floor.
    float3 dimsF     = float3(GX, GY, GZ) - 1.0;
    float3 distEdge  = min(gp, dimsF - gp);
    float  edgeFade  = saturate(min(distEdge.x, min(distEdge.y, distEdge.z)) / 1.5);
    if (edgeFade <= 0.0) return;

    int3   base = (int3)floor(gp);
    float3 f    = saturate(gp - (float3)base);

    // Trilinear blend of the 8 surrounding probes.
    float3 irr  = 0.0;
    float  wsum = 0.0;
    [unroll]
    for (int i = 0; i < 8; ++i) {
        int3   off = int3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        float3 tw  = lerp(1.0 - f, f, (float3)off);
        float  w   = tw.x * tw.y * tw.z;
        if (w <= 0.0) continue;
        // Cheap leak reduction (no depth probes): down-weight probes that sit behind the surface, so a
        // probe on the far side of a wall contributes little. Soft "wrap" weight keeps it artifact-free.
        int3   pcoord    = base + off;
        float3 probePos  = GridOrigin + (float3)pcoord * GridSpacing;
        float3 dirToProbe = normalize(probePos - worldPos);
        float  dw = saturate(dot(normal, dirToProbe)) * 0.5 + 0.5;
        w *= dw * dw;
        irr  += w * EvalSH(ProbeIndex(pcoord), normal);
        wsum += w;
    }
    if (wsum > 1e-5) irr /= wsum;

    // albedo·E/π is the diffuse bounce; clamp keeps an over-bright probe from blowing the pixel to
    // white, and edgeFade removes the hard volume boundary.
    float3 indirect = albedo * (irr / PI);
    indirect = min(indirect, 2.0);
    Output[pix] = float4(scene + indirect * Intensity * edgeFade, 1.0);
}
