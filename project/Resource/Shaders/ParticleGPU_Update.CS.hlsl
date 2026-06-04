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
    float4x4 ViewProj;        // rows 6-9 (matches ParticleGPU.VS: mul(float4(pos,1), ViewProj))
    float4 Viewport;          // row 10: x,y,w,h pixels within the full RT
    float2 RTSize;  float2 _cpad;   // row 11
    uint Collision; float Restitution; float Friction; float CollPush;  // row 12
    uint UseSizeCurve; float3 _sccpad;  // row 13
    float4 SizeCurve[2];                // rows 14-15 (8 floats, tightly packed)
};

float SampleSizeCurve(float t) {
    float idx = saturate(t) * 7.0;
    int i0 = (int)floor(idx); int i1 = min(i0 + 1, 7); float f = idx - i0;
    float v0 = SizeCurve[i0 >> 2][i0 & 3];
    float v1 = SizeCurve[i1 >> 2][i1 & 3];
    return lerp(v0, v1, f);
}

// Screen-space (depth-buffer) collision — same approach as Niagara's default GPU collision.
Texture2D<float>  SceneDepth  : register(t0);
Texture2D<float4> SceneNormal : register(t1);   // GBuffer RT1: world normal (rgb*2-1)
SamplerState      PointSamp   : register(s0);

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

    // Screen-space depth collision: project to screen, compare against scene depth; if the particle
    // is just behind the visible surface, bounce off the GBuffer normal (restitution + friction).
    if (Collision) {
        float4 clip = mul(float4(p.pos, 1.0), ViewProj);
        if (clip.w > 1e-4) {
            float3 ndc   = clip.xyz / clip.w;
            float2 vpUV  = ndc.xy * float2(0.5, -0.5) + 0.5;          // [0,1] within viewport
            if (all(vpUV >= 0.0) && all(vpUV <= 1.0) && ndc.z > 0.0 && ndc.z < 1.0) {
                float2 fullUV  = (Viewport.xy + vpUV * Viewport.zw) / RTSize;
                float  sceneZ  = SceneDepth.SampleLevel(PointSamp, fullUV, 0);
                // Behind the surface (penetrating) but not by a huge NDC margin (thin shell).
                if (sceneZ < 1.0 && ndc.z > sceneZ && (ndc.z - sceneZ) < 0.02) {
                    float3 n  = normalize(SceneNormal.SampleLevel(PointSamp, fullUV, 0).xyz * 2.0 - 1.0);
                    float  vn = dot(p.vel, n);
                    if (vn < 0.0) {                                   // moving into the surface
                        float3 vNormal  = vn * n;
                        float3 vTangent = p.vel - vNormal;
                        p.vel = vTangent * (1.0 - Friction) - vNormal * Restitution;
                    }
                    p.pos += n * CollPush;                            // nudge out of the surface
                }
            }
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

    if (UseSizeCurve) {
        p.size = p.sizeBase * max(0.0, SampleSizeCurve(t));
    } else {
        float sizeT = 1.0 - t * (1.0 - SizeEndMult);
        p.size = ShrinkSize ? p.sizeBase * max(0.0, sizeT) : p.sizeBase;
    }

    g_particles[idx] = p;
}
