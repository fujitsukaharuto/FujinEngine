static const float PI = 3.14159265358979;

cbuffer Constants : register(b0) {
    float Roughness;
    float MipSize;
    float _pad0, _pad1;
};

Texture2D<float4>        EnvEquirect : register(t0);
SamplerState             EnvSamp     : register(s0);
RWTexture2DArray<float4> Output      : register(u0);

float RadicalInverse_VdC(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint N) {
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

float2 DirToEquirect(float3 d) {
    float u = atan2(d.z, d.x) / (2.0 * PI) + 0.5;
    float v = acos(clamp(d.y, -1.0, 1.0)) / PI;
    return float2(u, v);
}

float3 GetCubeDir(uint face, float2 uv) {
    switch (face) {
        case 0:  return normalize(float3( 1.0, -uv.y, -uv.x));
        case 1:  return normalize(float3(-1.0, -uv.y,  uv.x));
        case 2:  return normalize(float3( uv.x,  1.0,  uv.y));
        case 3:  return normalize(float3( uv.x, -1.0, -uv.y));
        case 4:  return normalize(float3( uv.x, -uv.y,  1.0));
        default: return normalize(float3(-uv.x, -uv.y, -1.0));
    }
}

float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness) {
    float a        = roughness * roughness;
    float phi      = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    float3 H = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    float3 up      = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, N));
    float3 bitan   = cross(N, tangent);
    return normalize(tangent * H.x + bitan * H.y + N * H.z);
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    float2 ndcUV = (float2(tid.xy) + 0.5) / MipSize * 2.0 - 1.0;
    float3 N     = GetCubeDir(tid.z, ndcUV);
    float3 V     = N; // R = V = N for split-sum approximation

    const uint  N_SAMPLES = 512u;
    float3 prefilteredColor = float3(0, 0, 0);
    float  totalWeight      = 0.0;
    float  r                = max(Roughness, 0.001);

    for (uint i = 0u; i < N_SAMPLES; ++i) {
        float2 Xi = Hammersley(i, N_SAMPLES);
        float3 H  = ImportanceSampleGGX(Xi, N, r);
        float3 L  = normalize(2.0 * dot(V, H) * H - V);
        float  NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            prefilteredColor += EnvEquirect.SampleLevel(EnvSamp, DirToEquirect(L), 0.0).rgb * NdotL;
            totalWeight      += NdotL;
        }
    }

    Output[uint3(tid.xy, tid.z)] = float4(prefilteredColor / max(totalWeight, 0.0001), 1.0);
}
