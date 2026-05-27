static const float PI = 3.14159265358979;

RWTexture2D<float2> BRDFLUT : register(u0);

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

float3 ImportanceSampleGGX(float2 Xi, float roughness) {
    float a        = roughness * roughness;
    float phi      = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

float G_SchlickGGX_IBL(float NdotX, float roughness) {
    float k = (roughness * roughness) * 0.5;
    return NdotX / (NdotX * (1.0 - k) + k + 0.0001);
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    const uint  N_SAMPLES = 1024u;
    const float SIZE      = 512.0;

    float NdotV     = max((float(tid.x) + 0.5) / SIZE, 0.001);
    float roughness = max((float(tid.y) + 0.5) / SIZE, 0.001);

    float3 V = float3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);

    float scale = 0.0, bias = 0.0;
    for (uint i = 0u; i < N_SAMPLES; ++i) {
        float2 Xi = Hammersley(i, N_SAMPLES);
        float3 H  = ImportanceSampleGGX(Xi, roughness);
        float3 L  = 2.0 * dot(V, H) * H - V;

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0) {
            float G    = G_SchlickGGX_IBL(NdotV, roughness) * G_SchlickGGX_IBL(NdotL, roughness);
            float GVis = G * VdotH / (NdotH * NdotV + 0.0001);
            float Fc   = pow(1.0 - VdotH, 5.0);
            scale += (1.0 - Fc) * GVis;
            bias  += Fc * GVis;
        }
    }

    BRDFLUT[tid.xy] = float2(scale, bias) / float(N_SAMPLES);
}
