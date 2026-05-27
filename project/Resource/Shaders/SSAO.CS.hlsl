cbuffer SSAOCB : register(b0) {
    row_major float4x4 InvViewProj;
    row_major float4x4 ViewProj;
    float Radius;
    float Bias;
    float _p0, _p1;
};

Texture2D<float4> NormalRT  : register(t0);   // GBuffer RT1: world normal (rgb) + roughness (a)
Texture2D<float>  DepthTex  : register(t1);   // depth buffer SRV

RWTexture2D<float> Output : register(u0);

SamplerState PointSamp  : register(s0);
SamplerState LinearSamp : register(s1);

// 16-sample upper-hemisphere kernel (all z > 0 in tangent space)
static const float3 g_kernel[16] = {
    float3( 0.000, 0.000, 1.000), float3( 0.500, 0.000, 0.866),
    float3(-0.500, 0.000, 0.866), float3( 0.000, 0.500, 0.866),
    float3( 0.000,-0.500, 0.866), float3( 0.609, 0.271, 0.747),
    float3(-0.609, 0.271, 0.747), float3( 0.609,-0.271, 0.747),
    float3(-0.609,-0.271, 0.747), float3( 0.271, 0.609, 0.747),
    float3(-0.271, 0.609, 0.747), float3( 0.271,-0.609, 0.747),
    float3(-0.271,-0.609, 0.747), float3( 0.857, 0.000, 0.515),
    float3( 0.000, 0.857, 0.515), float3( 0.606, 0.606, 0.515),
};

float3 ReconstructWorld(float2 uv, float depth) {
    float2 ndc   = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 clip  = float4(ndc, depth, 1.0);
    float4 world = mul(InvViewProj, clip);
    return world.xyz / world.w;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint2 dims;
    Output.GetDimensions(dims.x, dims.y);
    if (DTid.x >= dims.x || DTid.y >= dims.y) return;

    float2 uv    = (float2(DTid.xy) + 0.5) / float2(dims);
    float  depth = DepthTex.SampleLevel(PointSamp, uv, 0);

    // Skip sky and uncleared first-frame depth (depth=0 → near plane, no valid geometry)
    if (depth >= 1.0 || depth < 0.0001) { Output[DTid.xy] = 1.0; return; }

    float3 worldPos = ReconstructWorld(uv, depth);
    float3 normal   = normalize(NormalRT.SampleLevel(PointSamp, uv, 0).rgb * 2.0 - 1.0);

    // Build TBN frame in world space
    float3 up    = abs(normal.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tang  = normalize(cross(up, normal));
    float3 bitan = cross(normal, tang);

    float occlusion = 0.0;
    [unroll]
    for (int i = 0; i < 16; ++i) {
        // Kernel sample: tangent-space → world-space
        float3 sampleDir = normalize(tang  * g_kernel[i].x
                                   + bitan * g_kernel[i].y
                                   + normal* g_kernel[i].z);
        // Accelerate samples closer to surface (importance sampling)
        float scale = float(i) / 16.0;
        scale = lerp(0.1, 1.0, scale * scale);
        float3 samplePos = worldPos + sampleDir * Radius * scale;

        // Project sample to screen UV
        float4 clip = mul(ViewProj, float4(samplePos, 1.0));
        float2 sUV  = clip.xy / clip.w;
        sUV = sUV * float2(0.5, -0.5) + 0.5;

        if (any(sUV < 0.0) || any(sUV > 1.0)) continue;

        float  sampledDepth  = DepthTex.SampleLevel(LinearSamp, sUV, 0);
        float  projectedZ    = clip.z / clip.w;  // NDC z of the sample

        // Occluded if actual geometry is closer than our sample
        float3 geoPos        = ReconstructWorld(sUV, sampledDepth);
        float  geoDist       = length(geoPos - worldPos);
        float  rangeCheck    = 1.0 - saturate(geoDist / Radius);

        occlusion += (sampledDepth < projectedZ - Bias) ? rangeCheck : 0.0;
    }

    Output[DTid.xy] = 1.0 - (occlusion / 16.0);
}
