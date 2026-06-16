static const float PI = 3.14159265358979;

cbuffer Camera : register(b0) {
    row_major float4x4 InvViewProj;
    float3 CameraPos;     float  _pad;
    float3 CameraForward; float  _pad2;
    float  VpOffX; float VpOffY; float VpSclX; float VpSclY;
    float  SSAOStrength;  uint LightCount; uint ClusterGX; uint ClusterGY;
    uint   ClusterGZ;     uint MaxPerCluster; float ClusterNear; float ClusterFar;
    row_major float4x4 ViewProj;        // world→clip, for screen-space contact shadows
    float  ContactLength;               // world-space ray length (0 strength = disabled)
    float  ContactStrength;
    uint   ContactSteps;
    float  ContactThickness;            // occluder depth window (world units)
};

struct LightData {
    float3 Position;  float Type;
    float3 Direction; float Range;
    float3 Color;     float Intensity;
    float  SpotAngle; float ShadowIndex;                 // ShadowIndex = -1 if no spot shadow
    float  Falloff;   float _lpad;                        // Falloff>0.5 = bright-core inverse-square (particle lights)
};

// All scene lights (no fixed cap), plus the per-cluster light index list built on the CPU.
// Each cluster occupies (1 + MaxPerCluster) uints: [count, idx0, idx1, ...].
StructuredBuffer<LightData> Lights        : register(t9);
StructuredBuffer<uint>      ClusterLights : register(t10);

cbuffer Shadow : register(b2) {
    row_major float4x4 LightViewProj[4];
    float4             CascadeSplits;
};

cbuffer SpotShadow : register(b3) {
    row_major float4x4 SpotVP[4];
    uint               SpotCount;
    uint3              _spadpad;
};

Texture2D              RT0           : register(t0);
Texture2D              RT1           : register(t1);
Texture2D              RT2           : register(t2);
Texture2D              Depth         : register(t3);
Texture2DArray         ShadowMap     : register(t4);
TextureCube            IrradianceMap : register(t5);
TextureCube            PrefilteredEnv: register(t6);
Texture2D              BRDFLUT       : register(t7);
Texture2D              SSAOTex       : register(t8);
Texture2DArray         SpotShadowMap : register(t11);
TextureCubeArray       PointShadowCube : register(t12);
SamplerState           PointSamp     : register(s0);
SamplerComparisonState ShadowSamp    : register(s1);
SamplerState           IBLSamp       : register(s2);
SamplerState           BRDFSamp      : register(s3);

float3 ReconstructWorld(float2 uv, float depth) {
    // Normalize uv to [0,1] within the viewport panel, then convert to NDC.
    float2 vpUV = (uv - float2(VpOffX, VpOffY)) / float2(VpSclX, VpSclY);
    float2 ndc  = float2(vpUV.x * 2.0 - 1.0, 1.0 - vpUV.y * 2.0);
    float4 clip  = float4(ndc, depth, 1.0);
    float4 world = mul(InvViewProj, clip);
    return world.xyz / world.w;
}

float D_GGX(float NdotH, float r) {
    float a = r * r; float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + 0.0001);
}

float G_Smith(float NdotV, float NdotL, float r) {
    float k = (r + 1.0) * (r + 1.0) / 8.0;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

float3 F_Schlick(float cosA, float3 F0) {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosA), 5.0);
}

// Interleaved gradient noise (Jimenez 2014): cheap per-pixel hash in [0,1). Used to rotate sparse
// sample kernels per-pixel so they read as smooth noise instead of banding/staircase patterns.
float InterleavedGradientNoise(float2 p) {
    return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

static const float SHADOW_TEXEL = 1.0 / 4096.0;   // matches ShadowPass::SHADOW_MAP_SIZE

// 16-tap Poisson disk — rotated per-pixel so the sparse taps read as a smooth penumbra (no banding).
static const float2 POISSON16[16] = {
    float2(-0.94201624, -0.39906216), float2( 0.94558609, -0.76890725),
    float2(-0.09418410, -0.92938870), float2( 0.34495938,  0.29387760),
    float2(-0.91588581,  0.45771432), float2(-0.81544232, -0.87912464),
    float2(-0.38277543,  0.27676845), float2( 0.97484398,  0.75648379),
    float2( 0.44323325, -0.97511554), float2( 0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023), float2( 0.79197514,  0.19090188),
    float2(-0.24188840,  0.99706507), float2(-0.81409955,  0.91437590),
    float2( 0.19984126,  0.78641367), float2( 0.14383161, -0.14100790)
};

// PCSS-style soft directional shadow:
//   1. Blocker search — average depth of occluders in a search disk.
//   2. Penumbra estimate — grows with receiver↔blocker distance (contact-hardening, the UE5 look).
//   3. Variable-radius PCF — 16 rotated Poisson taps sized by the penumbra.
// Light "size" is a fixed angular proxy (LIGHT_SIZE); rotation uses interleaved-gradient noise.
float ComputeShadow(float3 wpos, float NdotL) {
    float viewDepth = dot(wpos - CameraPos, CameraForward);
    uint  cascade   = 3;
    if      (viewDepth < CascadeSplits.x) cascade = 0;
    else if (viewDepth < CascadeSplits.y) cascade = 1;
    else if (viewDepth < CascadeSplits.z) cascade = 2;

    float4 lightClip = mul(LightViewProj[cascade], float4(wpos, 1.0));
    float3 proj      = lightClip.xyz / lightClip.w;
    float2 uv        = float2(proj.x * 0.5 + 0.5, -proj.y * 0.5 + 0.5);
    if (any(uv < 0.0) || any(uv > 1.0) || proj.z < 0.0 || proj.z > 1.0)
        return 1.0;

    float zR   = proj.z;
    float bias = max(0.0012 * (1.0 - NdotL), 0.0004);   // slope-scaled depth bias

    // Per-pixel rotation of the Poisson kernel (breaks the staircase the fixed kernel would show).
    float  ang = InterleavedGradientNoise(uv * 4096.0) * 6.2831853;
    float  cs = cos(ang), sn = sin(ang);
    float2x2 rot = float2x2(cs, -sn, sn, cs);

    const float LIGHT_SIZE     = 4.0;   // penumbra scale (larger = softer)
    const float SEARCH_RADIUS  = LIGHT_SIZE * 2.0 * SHADOW_TEXEL;

    // 1. Blocker search.
    float blockerSum = 0.0; int blockers = 0;
    [unroll] for (int i = 0; i < 16; ++i) {
        float2 o = mul(POISSON16[i], rot) * SEARCH_RADIUS;
        float  z = ShadowMap.SampleLevel(PointSamp, float3(uv + o, (float)cascade), 0).r;
        if (z < zR - bias) { blockerSum += z; ++blockers; }
    }
    if (blockers == 0) return 1.0;                       // fully lit
    float avgBlocker = blockerSum / (float)blockers;

    // 2. Penumbra (relative receiver↔blocker separation), mapped to a PCF radius in texels.
    float penumbra     = saturate((zR - avgBlocker) / max(avgBlocker, 1e-4));
    float filterRadius = (1.0 + penumbra * LIGHT_SIZE * 8.0) * SHADOW_TEXEL;

    // 3. Variable-radius PCF.
    float sh = 0.0;
    [unroll] for (int j = 0; j < 16; ++j) {
        float2 o = mul(POISSON16[j], rot) * filterRadius;
        sh += ShadowMap.SampleCmpLevelZero(ShadowSamp, float3(uv + o, (float)cascade), zR - bias);
    }
    return sh / 16.0;
}

// Screen-space contact shadow: march a short ray from the surface toward the light, project each
// step back to screen, and compare against the stored depth. If a closer surface lies between the
// point and the light (within a thin depth window), the pixel is occluded. Adds the fine grounding
// contact shadows CSM is too coarse to capture. Returns 1 (lit) when disabled or no occluder.

float ContactShadow(float3 wpos, float3 N, float3 L, float2 pixCoord) {
    if (ContactStrength <= 0.0 || ContactLength <= 0.0) return 1.0;

    uint   steps   = max(ContactSteps, 1u);
    float3 rayStep = L * (ContactLength / (float)steps);
    // Bias the start along the normal to avoid self-occlusion on the originating surface.
    float3 p = wpos + N * (ContactThickness * 0.5);
    // Dither the start by up to one step using per-pixel noise: this breaks the marching phase
    // alignment that otherwise produces a hard sawtooth/staircase shadow edge (turns it into fine
    // noise that reads as a soft penumbra, and TAA resolves it away when enabled).
    p += rayStep * InterleavedGradientNoise(pixCoord);

    [loop]
    for (uint i = 0; i < steps; ++i) {
        p += rayStep;
        float4 clip = mul(ViewProj, float4(p, 1.0));
        if (clip.w <= 1e-5) break;
        float2 ndc  = clip.xy / clip.w;
        float2 vpUV = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
        // viewport-uv → full render-target uv (scene is rendered into a sub-rect).
        float2 uv = float2(VpOffX, VpOffY) + vpUV * float2(VpSclX, VpSclY);
        if (any(uv < 0.0) || any(uv > 1.0)) break;

        float sd = Depth.SampleLevel(PointSamp, uv, 0).r;
        if (sd >= 1.0) continue;                       // sky → no occluder
        float3 sampWorld = ReconstructWorld(uv, sd);

        float pZ = dot(p - CameraPos, CameraForward);          // ray point view depth
        float sZ = dot(sampWorld - CameraPos, CameraForward);  // scene surface view depth
        float diff = pZ - sZ;                                  // >0: scene surface is closer to camera
        if (diff > ContactThickness * 0.25 && diff < ContactThickness * 2.0)
            return 1.0 - ContactStrength;                      // occluded
    }
    return 1.0;
}

// Spot light shadow: project into the spot's perspective map (atlas slice) and PCF sample.
float ComputeSpotShadow(uint slice, float3 wpos) {
    float4 lc = mul(SpotVP[slice], float4(wpos, 1.0));
    if (lc.w <= 0.0) return 1.0;                       // behind the light → unshadowed
    float3 proj = lc.xyz / lc.w;
    float2 uv   = float2(proj.x * 0.5 + 0.5, -proj.y * 0.5 + 0.5);
    if (any(uv < 0.0) || any(uv > 1.0) || proj.z < 0.0 || proj.z > 1.0)
        return 1.0;
    float bias  = 0.0008;
    float texel = 1.0 / 1024.0;
    float sh    = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y)
    [unroll] for (int x = -1; x <= 1; ++x)
        sh += SpotShadowMap.SampleCmpLevelZero(
            ShadowSamp, float3(uv + float2(x, y) * texel, (float)slice), proj.z - bias);
    return sh / 9.0;
}

// Point light shadow. The cube stores per-face perspective NDC depth (near=0.05, far=range), which
// is highly non-linear — comparing it against a constant NDC bias makes shadows pop/flicker as the
// light↔object distance changes. Instead we sample the raw depth, invert it back to LINEAR view
// distance (= distance along the dominant axis), and compare in world units with a distance-scaled
// bias. 5-tap PCF (centre + 4 tangential offsets) softens the edge.
float ComputePointShadow(uint cubeIndex, float3 wpos, float3 lightPos, float range) {
    float3 toFrag = wpos - lightPos;
    float3 a      = abs(toFrag);
    float  localZ = max(a.x, max(a.y, a.z));      // linear view depth of the dominant face
    if (localZ <= 1e-4) return 1.0;
    const float nearZ = 0.05;
    float farZ = max(range, nearZ + 0.01);
    float m22  = farZ / (farZ - nearZ);
    float m23  = -(farZ * nearZ) / (farZ - nearZ);

    // Tangent basis perpendicular to the sampling direction for PCF offsets.
    float3 up = (a.x <= a.y && a.x <= a.z) ? float3(1, 0, 0)
              : (a.y <= a.z)               ? float3(0, 1, 0) : float3(0, 0, 1);
    float3 t = normalize(cross(up, toFrag));
    float3 b = normalize(cross(toFrag, t));
    float  offs = localZ * 0.01;
    float  bias = max(0.03 * localZ, 0.05);       // world-space, distance-independent enough

    float3 taps[5] = { float3(0,0,0), t*offs, -t*offs, b*offs, -b*offs };
    float sh = 0.0;
    [unroll] for (int i = 0; i < 5; ++i) {
        float storedNDC = PointShadowCube.SampleLevel(
            PointSamp, float4(toFrag + taps[i], (float)cubeIndex), 0).r;
        float storedLin = m23 / (storedNDC - m22);   // → far when the texel is empty (storedNDC=1)
        sh += (localZ > storedLin + bias) ? 0.0 : 1.0;
    }
    return sh / 5.0;
}

float4 main(float4 sv : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target {
    float  depth   = Depth.Sample(PointSamp, uv).r;
    if (depth >= 1.0) return float4(0.08, 0.08, 0.1, 1.0);

    float4 rt0     = RT0.Sample(PointSamp, uv);
    float4 rt1     = RT1.Sample(PointSamp, uv);
    float4 rt2     = RT2.Sample(PointSamp, uv);
    float3 albedo  = rt0.rgb;
    float  metal   = rt0.a;
    float3 normal  = normalize(rt1.rgb * 2.0 - 1.0);
    float  rough   = max(rt1.a, 0.04);
    float  ao      = rt2.r * lerp(1.0, SSAOTex.Sample(PointSamp, uv).r, SSAOStrength);
    float3 wpos    = ReconstructWorld(uv, depth);
    float3 V       = normalize(CameraPos - wpos);
    float3 F0      = lerp(float3(0.04, 0.04, 0.04), albedo, metal);
    float3 Lo      = float3(0.0, 0.0, 0.0);

    // Cluster lookup: tile in viewport XY × logarithmic VIEW-Z slice (matches the CPU layout).
    float2 vpUV = (uv - float2(VpOffX, VpOffY)) / float2(VpSclX, VpSclY);
    uint cx = min((uint)(saturate(vpUV.x) * ClusterGX), ClusterGX - 1u);
    uint cy = min((uint)(saturate(vpUV.y) * ClusterGY), ClusterGY - 1u);
    float viewZ  = max(dot(wpos - CameraPos, CameraForward), ClusterNear);
    float slicef = log(viewZ / ClusterNear) / log(ClusterFar / ClusterNear) * ClusterGZ;
    uint  cz     = min((uint)max(slicef, 0.0), ClusterGZ - 1u);
    uint cluster = (cz * ClusterGY + cy) * ClusterGX + cx;
    uint base    = cluster * (1u + MaxPerCluster);
    uint count   = ClusterLights[base];

    for (uint k = 0; k < count; ++k) {
        uint i = ClusterLights[base + 1u + k];
        LightData light = Lights[i];
        float3 L; float att = 1.0;
        if (light.Type < 0.5) {
            L = normalize(-light.Direction);
        } else {
            float3 toL = light.Position - wpos;
            float  d   = length(toL);
            L   = toL / (d + 0.0001);
            float range = max(light.Range, 0.001);
            if (light.Falloff > 0.5) {
                // Bright-core inverse-square with a smooth window to 0 at range (UE/Niagara look).
                // Range-normalised so it peaks at 1 at the centre (no HDR blow-out vs the old model),
                // but with a far punchier core — what makes particle lights read as glowing emitters.
                float dr  = saturate(d / range);
                float win = 1.0 - dr * dr * dr * dr;
                win = win * win;
                float invSq = 1.0 / (1.0 + 8.0 * (d * d) / (range * range));
                att = win * invSq;
            } else {
                // Authored point/spot lights keep the original normalised falloff (no regression).
                float a = saturate(1.0 - d / range);
                att = a * a;
            }
            // Spot cone falloff (Type 2). SpotAngle is the full cone aperture (degrees).
            if (light.Type > 1.5) {
                float3 spotDir  = normalize(light.Direction);
                float  cosAng   = dot(-L, spotDir);                 // light→surface vs spot forward
                float  halfRad  = radians(light.SpotAngle * 0.5);
                float  cosOuter = cos(halfRad);
                float  cosInner = cos(halfRad * 0.85);              // soft edge
                float  cone     = saturate((cosAng - cosOuter) / max(cosInner - cosOuter, 1e-4));
                att *= cone * cone;
            }
        }
        float NdotL = saturate(dot(normal, L));
        float NdotV = saturate(dot(normal, V)) + 0.0001;
        if (NdotL > 0.0) {
            float3 H     = normalize(V + L);
            float  NdotH = saturate(dot(normal, H));
            float  HdotV = saturate(dot(H, V));
            float  D     = D_GGX(NdotH, rough);
            float  G     = G_Smith(NdotV, NdotL, rough);
            float3 F     = F_Schlick(HdotV, F0);
            float3 spec  = D * G * F / (4.0 * NdotV * NdotL + 0.0001);
            float3 kD    = (1.0 - F) * (1.0 - metal);
            float3 diff  = kD * albedo / PI;
            float3 rad   = light.Color * light.Intensity * att;

            float shadow = 1.0;
            if (light.Type < 0.5) {
                shadow = ComputeShadow(wpos, NdotL);                // directional CSM (PCSS soft)
                shadow = min(shadow, ContactShadow(wpos, normal, L, sv.xy)); // + screen-space contact
            }
            else if (light.ShadowIndex >= 0.0) {
                if (light.Type > 1.5)                               // spot
                    shadow = ComputeSpotShadow((uint)(light.ShadowIndex + 0.5), wpos);
                else                                                // point
                    shadow = ComputePointShadow((uint)(light.ShadowIndex + 0.5), wpos,
                                                 light.Position, light.Range);
            }

            Lo += (diff + spec) * rad * NdotL * shadow;
        }
    }

    // IBL ambient (split-sum approximation)
    float3 F_ibl      = F_Schlick(saturate(dot(normal, V)), F0);
    float3 kD_ibl     = (1.0 - F_ibl) * (1.0 - metal);
    float3 irradiance = IrradianceMap.Sample(IBLSamp, normal).rgb;
    float3 ibl_diff   = kD_ibl * irradiance * albedo;

    float3 R           = reflect(-V, normal);
    float2 brdf        = BRDFLUT.Sample(BRDFSamp, float2(saturate(dot(normal, V)), max(rough, 0.001))).rg;
    float3 prefiltEnv  = PrefilteredEnv.SampleLevel(IBLSamp, R, rough * 4.0).rgb;
    float3 ibl_spec    = prefiltEnv * (F0 * brdf.x + brdf.y);

    float3 ambient = (ibl_diff + ibl_spec) * ao;
    // Output linear HDR — tonemapping and gamma done in TonemapFXAA pass
    return float4(ambient + Lo, 1.0);
}
