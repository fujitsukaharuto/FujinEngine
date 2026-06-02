static const float PI = 3.14159265358979;

cbuffer PerObject : register(b0) {
    row_major float4x4 WorldViewProj;
    row_major float4x4 World;
    float3 AlbedoColor; float  Metallic;
    float  Roughness;   float  AO; float Opacity; float _pad;
};

cbuffer Camera : register(b1) {
    float3 CameraPos;     float _c0;
    float3 CameraForward; float _c1;
};

struct LightData {
    float3 Position;  float Type;
    float3 Direction; float Range;
    float3 Color;     float Intensity;
    float  SpotAngle; float3 _lpad;
};

cbuffer Lights : register(b2) {
    uint      LightCount;
    float3    _lpad1;
    LightData Lights[16];
};

cbuffer Shadow : register(b3) {
    row_major float4x4 LightViewProj[4];
    float4             CascadeSplits;
};

Texture2D              AlbedoTex      : register(t0);
Texture2D              NormalTex      : register(t1);
Texture2D              OrmTex         : register(t2);
Texture2DArray         ShadowMap      : register(t3);
TextureCube            IrradianceMap  : register(t4);
TextureCube            PrefilteredEnv : register(t5);
Texture2D              BRDFLUT        : register(t6);

SamplerState           LinearWrap  : register(s0);
SamplerComparisonState ShadowSamp  : register(s1);
SamplerState           IBLSamp     : register(s2);
SamplerState           BRDFSamp    : register(s3);

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

    float4 lightClip = mul(LightViewProj[cascade], float4(wpos, 1.0));
    float3 proj      = lightClip.xyz / lightClip.w;
    float2 shadowUV  = float2(proj.x * 0.5 + 0.5, -proj.y * 0.5 + 0.5);

    if (any(shadowUV < 0.0) || any(shadowUV > 1.0) || proj.z < 0.0 || proj.z > 1.0)
        return 1.0;

    return SampleShadowPCF(cascade, shadowUV, proj.z - 0.0003);
}

struct VSOut {
    float4 Sv      : SV_POSITION;
    float3 WPos    : TEXCOORD1;
    float3 WNorm   : NORMAL;
    float3 WTangent: TANGENT;
    float2 UV      : TEXCOORD0;
};

float4 main(VSOut i, bool isFrontFace : SV_IsFrontFace) : SV_Target {
    float4 albedoSample = AlbedoTex.Sample(LinearWrap, i.UV);
    float3 albedo = albedoSample.rgb * AlbedoColor;
    float  alpha  = albedoSample.a * Opacity;

    float3 N = normalize(isFrontFace ? i.WNorm : -i.WNorm);
    float3 normal;
    if (dot(i.WTangent, i.WTangent) > 0.001f) {
        float3 T     = normalize(i.WTangent - dot(i.WTangent, N) * N);
        float3 B     = cross(N, T);
        float3x3 TBN = float3x3(T, B, N);
        float3 nts   = NormalTex.Sample(LinearWrap, i.UV).xyz * 2.0 - 1.0;
        normal = normalize(mul(nts, TBN));
    } else {
        normal = N;
    }

    float3 orm      = OrmTex.Sample(LinearWrap, i.UV).rgb;
    float  ao       = orm.r * AO;
    float  rough    = max(orm.g * Roughness, 0.04);
    float  metallic = orm.b * Metallic;

    float3 V  = normalize(CameraPos - i.WPos);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 Lo = (float3)0.0;

    for (uint li = 0; li < LightCount; ++li) {
        LightData light = Lights[li];
        float3 L; float att = 1.0;
        if (light.Type < 0.5) {
            L = normalize(-light.Direction);
        } else {
            float3 toL = light.Position - i.WPos;
            float  d   = length(toL);
            L   = toL / (d + 0.0001);
            float a = saturate(1.0 - d / max(light.Range, 0.001));
            att = a * a;
        }
        float NdotL = saturate(dot(normal, L));
        float NdotV = saturate(dot(normal, V)) + 0.0001;
        if (NdotL > 0.0) {
            float3 H    = normalize(V + L);
            float NdotH = saturate(dot(normal, H));
            float HdotV = saturate(dot(H, V));
            float  D    = D_GGX(NdotH, rough);
            float  G    = G_Smith(NdotV, NdotL, rough);
            float3 F    = F_Schlick(HdotV, F0);
            float3 spec = D * G * F / (4.0 * NdotV * NdotL + 0.0001);
            float3 kD   = (1.0 - F) * (1.0 - metallic);
            float3 diff = kD * albedo / PI;
            float3 rad  = light.Color * light.Intensity * att;
            float shadow = 1.0;
            if (light.Type < 0.5)
                shadow = ComputeShadow(i.WPos);
            Lo += (diff + spec) * rad * NdotL * shadow;
        }
    }

    float3 F_ibl     = F_Schlick(saturate(dot(normal, V)), F0);
    float3 kD_ibl    = (1.0 - F_ibl) * (1.0 - metallic);
    float3 irrad     = IrradianceMap.Sample(IBLSamp, normal).rgb;
    float3 ibl_diff  = kD_ibl * irrad * albedo;
    float3 R         = reflect(-V, normal);
    float2 brdf      = BRDFLUT.Sample(BRDFSamp, float2(saturate(dot(normal, V)), max(rough, 0.001))).rg;
    float3 prefEnv   = PrefilteredEnv.SampleLevel(IBLSamp, R, rough * 4.0).rgb;
    float3 ibl_spec  = prefEnv * (F0 * brdf.x + brdf.y);
    float3 ambient   = (ibl_diff + ibl_spec) * ao;

    return float4(ambient + Lo, alpha);
}
