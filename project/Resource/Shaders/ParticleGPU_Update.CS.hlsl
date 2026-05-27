struct GPUParticle {
    float3 pos;        float age;
    float3 vel;        float lifetime;
    float4 colorStart;
    float4 colorEnd;
    float4 color;
    float  sizeBase;   float size;
    float  rot;        float rotRate;
};

RWStructuredBuffer<GPUParticle> g_particles : register(u0);

cbuffer UpdateParams : register(b0) {
    float3 Gravity;           // row 0
    float  DT;
    float  Drag;              // row 1
    float  SizeEndMult;
    uint   MaxParticles;
    uint   FadeColor;
    uint   ShrinkSize;        // row 2
    float  Elapsed;
    uint   Turbulence;
    float  TurbStrength;
    float  TurbFrequency;     // row 3
    uint   UseAttractor;
    uint   UseColorMid;
    float  AttractorStrength;
    float3 AttractorPos;      // row 4
    float  AttractorRadius;
    float4 ColorMid;          // row 5
};

#include "Noise.hlsli"

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint idx = id.x;
    if (idx >= MaxParticles) return;

    GPUParticle p = g_particles[idx];
    if (p.lifetime <= 0) return;

    p.age += DT;
    if (p.age >= p.lifetime) {
        p.lifetime = 0;
        g_particles[idx] = p;
        return;
    }

    float t = p.age / p.lifetime;

    p.vel += Gravity * DT;
    p.vel *= max(0.0, 1.0 - Drag * DT);
    p.pos += p.vel * DT;
    p.rot += p.rotRate * DT;

    if (Turbulence) {
        float3 n = CurlNoise_fast(p.pos * TurbFrequency + Elapsed * 0.3);
        p.vel += n * TurbStrength * DT;
    }

    if (UseAttractor) {
        float3 diff = AttractorPos - p.pos;
        float dist = length(diff);
        if (dist < AttractorRadius && dist > 0.0001) {
            float falloff = 1.0 - dist / AttractorRadius;
            p.vel += (diff / dist) * AttractorStrength * falloff * DT;
        }
    }

    if (FadeColor) {
        if (UseColorMid) {
            p.color = (t < 0.5)
                ? lerp(p.colorStart, ColorMid, t * 2.0)
                : lerp(ColorMid, p.colorEnd, (t - 0.5) * 2.0);
        } else {
            p.color = lerp(p.colorStart, p.colorEnd, t);
        }
    }

    float sizeT = 1.0 - t * (1.0 - SizeEndMult);
    p.size = ShrinkSize ? p.sizeBase * max(0.0, sizeT) : p.sizeBase;

    g_particles[idx] = p;
}
