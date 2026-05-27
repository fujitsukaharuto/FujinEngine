struct GPUParticle {
    float3 pos;        float age;
    float3 vel;        float lifetime;
    float4 colorStart;
    float4 colorEnd;
    float4 color;
    float  sizeBase;   float size;
    float  rot;        float rotRate;
};

struct GPUSpawnData {
    float3 pos;        float lifetime;
    float3 vel;        float sizeBase;
    float4 colorStart;
    float4 colorEnd;
    float  rot;        float rotRate;
    float2 _pad;
}; // 80 bytes

RWStructuredBuffer<GPUParticle> g_particles : register(u0);
RWByteAddressBuffer             g_writeHead  : register(u1);
StructuredBuffer<GPUSpawnData>  g_spawnData  : register(t0);

cbuffer SpawnParams : register(b0) {
    uint SpawnCount;
    uint MaxParticles;
    uint2 _pad;
};

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint i = id.x;
    if (i >= SpawnCount) return;

    uint prev;
    g_writeHead.InterlockedAdd(0, 1, prev);
    uint slot = prev % MaxParticles;

    GPUSpawnData s = g_spawnData[i];
    GPUParticle p;
    p.pos        = s.pos;
    p.age        = 0.0;
    p.vel        = s.vel;
    p.lifetime   = s.lifetime;
    p.colorStart = s.colorStart;
    p.colorEnd   = s.colorEnd;
    p.color      = s.colorStart;
    p.sizeBase   = s.sizeBase;
    p.size       = s.sizeBase;
    p.rot        = s.rot;
    p.rotRate    = s.rotRate;
    g_particles[slot] = p;
}
