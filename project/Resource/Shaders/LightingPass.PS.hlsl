static const float PI = 3.14159265358979;

cbuffer Camera : register(b0) {
    row_major float4x4 InvViewProj;
    float3 CameraPos;     float  _pad;
    float3 CameraForward; float  _pad2;
    float  VpOffX; float VpOffY; float VpSclX; float VpSclY;
    float  SSAOStrength;  uint LightCount; uint ClusterGX; uint ClusterGY;
    uint   ClusterGZ;     uint MaxPerCluster; float ClusterNear; float ClusterFar;
};

struct LightData {
    float3 Position;  float Type;
    float3 Direction; float Range;
    float3 Color;     float Intensity;
    float  SpotAngle; float ShadowIndex; float2 _lpad;   // ShadowIndex = -1 if no spot shadow
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

float SampleShadowPCF(uint cascade, float2 uv, float compareDepth) {
    float shadow = 0.0;
    float texel  = 1.0 / 2048.0;
    [unroll] for (int y = -1; y <= 1; ++y)
    [unroll] for (int x = -1; x <= 1; ++x)
        shadow += ShadowMap.SampleCmpLevelZero(
            ShadowSamp, float3(uv + float2(x, y) * texel, float(cascade)), compareDepth);
    return shadow / 9.0;
}

float ComputeShadow(float3 wpos) {
    float viewDepth = dot(wpos - CameraPos, CameraForward);
    uint  cascade   = 3;
    if      (viewDepth < CascadeSplits.x) cascade = 0;
    else if (viewDepth < CascadeSplits.y) cascade = 1;
    else if (viewDepth < CascadeSplits.z) cascade = 2;

    float4 lightClip  = mul(LightViewProj[cascade], float4(wpos, 1.0));
    float3 proj       = lightClip.xyz / lightClip.w;
    float2 shadowUV   = float2(proj.x * 0.5 + 0.5, -proj.y * 0.5 + 0.5);

    if (any(shadowUV < 0.0) || any(shadowUV > 1.0) || proj.z < 0.0 || proj.z > 1.0)
        return 1.0;

    float bias = 0.0003;
    return SampleShadowPCF(cascade, shadowUV, proj.z - bias);
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
            float a = saturate(1.0 - d / max(light.Range, 0.001));
            att = a * a;
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
            if (light.Type < 0.5)
                shadow = ComputeShadow(wpos);                       // directional CSM
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
