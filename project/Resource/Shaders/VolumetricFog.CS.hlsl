// Exponential height fog + directional inscattering (UE5 "Exponential Height Fog" equivalent).
//   • Per pixel: reconstruct the world position from depth, integrate exponential height-fog density
//     along the view ray (closed form), and blend a fog colour into the HDR scene.
//   • Directional inscattering: the fog colour shifts toward the sun colour as the view ray aligns
//     with the sun, giving an atmospheric sun glow. No shadow sampling (cheap, 1 sample/pixel).
//   • With FogMaxOpacity 0 (or the pass disabled) it's a pure pass-through, so it can't regress.
//   • Convention matches SSR/SSAO/TAA: row_major matrices, mul(M,v), ndc=(u*2-1, 1-v*2), viewport
//     sub-rectangle of the full RTs.
cbuffer FogCB : register(b0) {
    row_major float4x4 InvViewProj;
    float3 CameraPos;      float _p0;
    float4 Viewport;       // x, y, w, h (pixels within the full RT)
    float2 RTSize;         float2 _p1;
    float3 SunDir;         float _p2;   // direction light travels (sun → scene)
    float3 SunColor;       float _p3;
    float3 FogColor;       float FogDensity;
    float3 InscatterColor; float HeightFalloff;
    float  FogHeight;      float MaxOpacity; float InscatterExp; float _p4;
    // Volumetric god rays (directional CSM). GodRaySteps==0 → no marching (pure height fog).
    row_major float4x4 LightViewProj[4];
    float4 CascadeSplits;
    float3 CameraForward;  float _p5;
    float  GodRayIntensity; float GodRayMaxDist; int GodRaySteps; float GodRayG;
};

Texture2D<float4>      SceneHDR  : register(t0);   // lit scene colour (HDR)
Texture2D<float>       DepthTex  : register(t1);   // depth buffer (NDC z, [0,1])
Texture2DArray<float>  ShadowMap : register(t2);   // CSM cascades (R32_FLOAT), in ALL_SHADER state
RWTexture2D<float4>    Output    : register(u0);

SamplerState LinearSamp : register(s0);
SamplerState PointSamp  : register(s1);

float3 ReconstructWorld(float2 vuv, float depth) {
    float2 ndc   = float2(vuv.x * 2.0 - 1.0, 1.0 - vuv.y * 2.0);
    float4 world = mul(InvViewProj, float4(ndc, depth, 1.0));
    return world.xyz / world.w;
}

// 1 = lit by the sun, 0 = in shadow. Cascade selection + projection match LightingPass ComputeShadow.
float SunShadow(float3 wpos) {
    float viewDepth = dot(wpos - CameraPos, CameraForward);
    uint c = 3;
    if      (viewDepth < CascadeSplits.x) c = 0;
    else if (viewDepth < CascadeSplits.y) c = 1;
    else if (viewDepth < CascadeSplits.z) c = 2;
    float4 lc  = mul(LightViewProj[c], float4(wpos, 1.0));
    float3 p   = lc.xyz / lc.w;
    float2 uv  = float2(p.x * 0.5 + 0.5, -p.y * 0.5 + 0.5);
    if (any(uv < 0.0) || any(uv > 1.0) || p.z < 0.0 || p.z > 1.0) return 1.0;  // outside cascade → lit
    float stored = ShadowMap.SampleLevel(PointSamp, float3(uv, (float)c), 0);
    return (p.z - 0.0008 <= stored) ? 1.0 : 0.0;
}

// Henyey-Greenstein phase: forward scattering toward the sun gives crisp shafts.
float PhaseHG(float cosT, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosT;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(denom, 1e-4), 1.5));
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
    if (MaxOpacity <= 0.0) return;

    float2 fullUV = (float2(pix) + 0.5) / RTSize;
    float  depth  = DepthTex.SampleLevel(PointSamp, fullUV, 0);
    float2 vuv    = (float2(pix) - Viewport.xy + 0.5) / Viewport.zw;

    // View ray direction (independent of depth) + distance to the surface (large for sky).
    float3 rayPt = ReconstructWorld(vuv, 0.5);
    float3 V     = normalize(rayPt - CameraPos);
    float  dist;
    if (depth >= 1.0 || depth < 0.0001) {
        dist = 100000.0;                       // sky → saturate distance fog toward the horizon
    } else {
        float3 wpos = ReconstructWorld(vuv, depth);
        dist = length(wpos - CameraPos);
    }

    // Closed-form integral of D(y)=FogDensity*exp(-HeightFalloff*(y-FogHeight)) along the ray.
    float baseDensity = FogDensity * exp(-HeightFalloff * (CameraPos.y - FogHeight));
    float vy = V.y;
    float fterm;
    if (abs(vy) > 1e-4)
        fterm = (1.0 - exp(-HeightFalloff * vy * dist)) / (HeightFalloff * vy);
    else
        fterm = dist;
    float optical = max(baseDensity * fterm, 0.0);
    float fog = saturate(1.0 - exp(-optical)) * MaxOpacity;

    // Fog colour. The analytic directional inscatter (sun-tinted glow) is NOT occlusion-aware, so it
    // would wrongly light up the sun's direction even when an object blocks the sun. Use it only when
    // volumetric god rays are OFF; when ON, the marched in-scatter provides occlusion-aware sun glow.
    float3 fogCol;
    if (GodRaySteps > 0) {
        fogCol = FogColor;
    } else {
        float s = pow(saturate(dot(V, -normalize(SunDir))), max(InscatterExp, 0.001));
        fogCol  = lerp(FogColor, InscatterColor * SunColor, s);
    }
    float3 outc = lerp(scene, fogCol, fog);

    // Volumetric god rays: march the view ray, accumulate sun in-scatter only where the sun reaches,
    // weighted by transmittance so the result saturates with distance (no unbounded brightening).
    if (GodRaySteps > 0) {
        float marchDist = min(dist, GodRayMaxDist);
        float dStep     = marchDist / (float)GodRaySteps;
        float dither    = frac(sin(dot(float2(pix), float2(12.9898, 78.233))) * 43758.5453);
        float tt        = dStep * dither;     // jitter start to break up banding
        float accum     = 0.0;
        float trans     = 1.0;                // remaining transmittance camera→step
        [loop]
        for (int i = 0; i < GodRaySteps; ++i) {
            float3 sp  = CameraPos + V * tt;
            float  lit = SunShadow(sp);
            float  d   = max(FogDensity * exp(-HeightFalloff * (sp.y - FogHeight)), 0.0);
            // Per-step scattered fraction = 1-exp(-σ·ds), bounded [0,1]. Using the linear σ·ds instead
            // overshoots when a step is optically thick (e.g. high FogHeight → huge density) → white-out.
            float stepOptical = d * dStep;
            float stepScatter = 1.0 - exp(-stepOptical);
            accum += trans * lit * stepScatter;
            trans *= exp(-stepOptical);       // extinction → total in-scatter ≤ 1, can't blow up
            tt    += dStep;
        }
        float cosT = dot(V, -normalize(SunDir));
        outc += SunColor * (GodRayIntensity * PhaseHG(cosT, GodRayG) * accum);
    }

    Output[pix] = float4(outc, 1.0);
}
