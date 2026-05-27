static const float PI = 3.14159265358979;

cbuffer Camera : register(b0) {
    row_major float4x4 InvViewProj;
    float3 CameraPos;     float  _pad;
    float3 CameraForward; float  _pad2;
    float  VpOffX; float VpOffY; float VpSclX; float VpSclY;
    float  SSAOStrength;  float3 _pad3;
};

struct LightData {
    float3 Position;  float Type;
    float3 Direction; float Range;
    float3 Color;     float Intensity;
    float  SpotAngle; float3 _lpad;
};

cbuffer Lights : register(b1) {
    uint      LightCount;
    float3    _lpad1;
    LightData Lights[16];
};

cbuffer Shadow : register(b2) {
    row_major float4x4 LightViewProj[4];
    float4             CascadeSplits;
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

    for (uint i = 0; i < LightCount; ++i) {
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
                shadow = ComputeShadow(wpos);

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
