struct GPUParticle {
    float3 pos;        float age;
    float3 vel;        float lifetime;
    float4 colorStart;
    float4 colorEnd;
    float4 color;
    float  sizeBase;   float size;
    float  rot;        float rotRate;
};

StructuredBuffer<GPUParticle> g_particles : register(t0);

cbuffer PassCB : register(b0) {
    float4x4 ViewProj;
    float3   CamRight; float _p0;
    float3   CamUp;    float _p1;
};

struct VSOut {
    float4 pos   : SV_Position;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
};

static const float2 QuadUV[6] = {
    float2(0,0), float2(1,0), float2(0,1),
    float2(0,1), float2(1,0), float2(1,1)
};
static const float2 QuadCorner[6] = {
    float2(-1, 1), float2( 1, 1), float2(-1,-1),
    float2(-1,-1), float2( 1, 1), float2( 1,-1)
};

VSOut main(uint vertID : SV_VertexID, uint instID : SV_InstanceID) {
    VSOut o;
    GPUParticle p = g_particles[instID];

    // Dead or zero-size particles become degenerate
    float sz = (p.lifetime > 0) ? p.size * 0.5 : 0.0;

    float2 corner = QuadCorner[vertID];
    float  s = sin(p.rot);
    float  c = cos(p.rot);
    float2 rotC = float2(corner.x * c - corner.y * s,
                         corner.x * s + corner.y * c);

    float3 worldPos = p.pos
                    + CamRight * (rotC.x * sz)
                    + CamUp    * (rotC.y * sz);

    o.pos   = mul(float4(worldPos, 1.0), ViewProj);
    o.uv    = QuadUV[vertID];
    o.color = p.color;
    return o;
}
