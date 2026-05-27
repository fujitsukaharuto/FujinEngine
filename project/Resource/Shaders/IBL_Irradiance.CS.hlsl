static const float PI = 3.14159265358979;

Texture2D<float4>        EnvEquirect : register(t0);
SamplerState             EnvSamp     : register(s0);
RWTexture2DArray<float4> Irradiance  : register(u0);

float2 DirToEquirect(float3 d) {
    float u = atan2(d.z, d.x) / (2.0 * PI) + 0.5;
    float v = acos(clamp(d.y, -1.0, 1.0)) / PI;
    return float2(u, v);
}

// Maps face index + NDC [-1,1] UV to cube direction (DX12 convention)
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

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
    const float SIZE  = 32.0;
    float2 ndcUV = (float2(tid.xy) + 0.5) / SIZE * 2.0 - 1.0;
    float3 N     = GetCubeDir(tid.z, ndcUV);

    float3 up    = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 right = normalize(cross(up, N));
    up           = cross(N, right);

    float3 irr    = float3(0, 0, 0);
    float  weight = 0.0;

    const float DELTA = 0.025;
    for (float phi = 0.0; phi < 2.0 * PI; phi += DELTA) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += DELTA) {
            float sinT = sin(theta), cosT = cos(theta);
            float3 sDir = sinT * cos(phi) * right + sinT * sin(phi) * up + cosT * N;
            irr    += EnvEquirect.SampleLevel(EnvSamp, DirToEquirect(normalize(sDir)), 0.0).rgb
                      * cosT * sinT;
            weight += cosT * sinT;
        }
    }

    Irradiance[uint3(tid.xy, tid.z)] = float4(PI * irr / max(weight, 0.0001), 1.0);
}
