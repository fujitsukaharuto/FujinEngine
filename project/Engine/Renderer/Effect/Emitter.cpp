#include "Emitter.h"
#include <cstdlib>
#include <cmath>
#include <algorithm>

namespace Fujin {

static constexpr float PI = 3.14159265f;

static float Randf() {
    return static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
}

// ── Curl noise (CPU port of Resource/Shaders/Noise.hlsli CurlNoise_fast) ──────
// Kept bit-for-bit equivalent to the GPU path so CPU and GPU emitters swirl identically.
static inline float Fract(float x) { return x - std::floor(x); }
static float HashN(Vector3 p) {
    p = Vector3(Fract(p.x * 0.3183099f + 0.1f), Fract(p.y * 0.3183099f + 0.1f), Fract(p.z * 0.3183099f + 0.1f));
    p = p * 17.0f;
    return Fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
static Vector3 Noise3Fast(Vector3 p) {
    return Vector3(HashN(p + Vector3(37.0f, 17.0f, 13.0f)),
                   HashN(p + Vector3(11.0f, 47.0f, 19.0f)),
                   HashN(p + Vector3(23.0f,  7.0f, 53.0f))) * 2.0f - Vector3(1.0f, 1.0f, 1.0f);
}
static Vector3 CurlNoiseFast(Vector3 p) {
    const float e = 0.2f;
    Vector3 dx(e, 0, 0), dy(0, e, 0), dz(0, 0, e);
    Vector3 px0 = Noise3Fast(p - dx), px1 = Noise3Fast(p + dx);
    Vector3 py0 = Noise3Fast(p - dy), py1 = Noise3Fast(p + dy);
    Vector3 pz0 = Noise3Fast(p - dz), pz1 = Noise3Fast(p + dz);
    Vector3 curl((py1.z - py0.z) - (pz1.y - pz0.y),
                 (pz1.x - pz0.x) - (px1.z - px0.z),
                 (px1.y - px0.y) - (py1.x - py0.x));
    float len = std::sqrt(curl.x * curl.x + curl.y * curl.y + curl.z * curl.z);
    return (len > 1e-5f) ? curl / len : Vector3(0, 0, 0);
}
static float RandRange(float lo, float hi) {
    return lo + Randf() * (hi - lo);
}

// ---- public -----------------------------------------------------------------

void Emitter::Initialize(const EmitterDesc& desc) {
    m_desc  = desc;
    m_particles.assign(static_cast<size_t>(desc.MaxParticles), Particle{});
    m_spawnAccum = 0.0f;
    m_elapsed    = 0.0f;
    m_activeCount = 0;
    m_playing    = true;
    m_burstDone  = false;
}

void Emitter::Reset() {
    for (auto& p : m_particles) p.Active = false;
    m_spawnAccum  = 0.0f;
    m_elapsed     = 0.0f;
    m_activeCount = 0;
    m_burstDone   = false;
    ++m_resetCount;
}

void Emitter::Update(float dt, const Vector3& worldPos) {
    if (!m_playing) return;
    m_elapsed += dt;

    if (m_desc.Spawn.BurstMode) {
        // Burst mode: fire once (or once per loop cycle)
        if (!m_burstDone && m_desc.Spawn.BurstCount > 0) {
            for (int i = 0; i < m_desc.Spawn.BurstCount; ++i)
                SpawnOne(worldPos);
            m_burstDone = true;
        }
    } else {
        // Continuous mode: spawn at RatePerSecond
        if (m_desc.Loop || m_elapsed <= m_desc.Duration) {
            m_spawnAccum += dt * m_desc.Spawn.RatePerSecond;
            while (m_spawnAccum >= 1.0f) {
                m_spawnAccum -= 1.0f;
                SpawnOne(worldPos);
            }
        }
    }

    // Update
    m_activeCount = 0;
    const auto& upd = m_desc.Update;
    const auto& ini = m_desc.Init;
    for (auto& p : m_particles) {
        if (!p.Active) continue;

        p.Age += dt;
        if (p.Age >= p.Lifetime) { p.Active = false; continue; }
        float t = p.Age / p.Lifetime;

        // Physics
        p.Velocity += upd.Gravity * dt;
        p.Velocity *= std::max(0.0f, 1.0f - upd.Drag * dt);
        p.Position += p.Velocity * dt;

        // Turbulence = divergence-free curl noise (matches the GPU update CS exactly).
        if (upd.Turbulence) {
            float te = m_elapsed * 0.3f;
            Vector3 sp = p.Position * upd.TurbFrequency + Vector3(te, te, te);
            p.Velocity += CurlNoiseFast(sp) * (upd.TurbStrength * dt);
        }

        // Point Attractor / Repulsor
        if (upd.UseAttractor) {
            float dx = upd.AttractorPos.x - p.Position.x;
            float dy = upd.AttractorPos.y - p.Position.y;
            float dz = upd.AttractorPos.z - p.Position.z;
            float dist = std::sqrtf(dx*dx + dy*dy + dz*dz);
            if (dist < upd.AttractorRadius && dist > 0.0001f) {
                float falloff = 1.0f - dist / upd.AttractorRadius;
                float f = upd.AttractorStrength * falloff * dt / dist;
                p.Velocity += Vector3(dx * f, dy * f, dz * f);
            }
        }

        // Rotation
        p.Rotation += p.RotRate * dt;

        // Color (2-stop or 3-stop gradient)
        if (upd.FadeColor) {
            const auto& cs = ini.ColorStart;
            const auto& ce = ini.ColorEnd;
            if (ini.UseColorMid) {
                const auto& cm = ini.ColorMid;
                if (t < 0.5f) {
                    float t2 = t * 2.0f;
                    p.Color = Vector4(cs.x + (cm.x-cs.x)*t2, cs.y + (cm.y-cs.y)*t2,
                                      cs.z + (cm.z-cs.z)*t2, cs.w + (cm.w-cs.w)*t2);
                } else {
                    float t2 = (t - 0.5f) * 2.0f;
                    p.Color = Vector4(cm.x + (ce.x-cm.x)*t2, cm.y + (ce.y-cm.y)*t2,
                                      cm.z + (ce.z-cm.z)*t2, cm.w + (ce.w-cm.w)*t2);
                }
            } else {
                p.Color = Vector4(
                    cs.x + (ce.x - cs.x) * t,
                    cs.y + (ce.y - cs.y) * t,
                    cs.z + (ce.z - cs.z) * t,
                    cs.w + (ce.w - cs.w) * t);
            }
        }

        // Size over life: curve (8-point) takes priority over the linear shrink.
        if (upd.UseSizeCurve) {
            float idx = std::max(0.0f, std::min(t, 1.0f)) * 7.0f;
            int   i0  = (int)idx; int i1 = (i0 < 7) ? i0 + 1 : 7;
            float f   = idx - (float)i0;
            float m   = upd.SizeCurve[i0] * (1.0f - f) + upd.SizeCurve[i1] * f;
            p.Size    = p.SizeBase * std::max(0.0f, m);
        } else if (upd.ShrinkSize) {
            float s = 1.0f - t * (1.0f - upd.SizeEndMult);
            p.Size  = p.SizeBase * std::max(0.0f, s);
        }

        ++m_activeCount;
    }

    // Auto-stop for non-looping emitters once duration passed and all particles dead
    if (!m_desc.Loop && m_elapsed > m_desc.Duration && m_activeCount == 0)
        m_playing = false;
}

// ---- private ----------------------------------------------------------------

void Emitter::SpawnOne(const Vector3& worldPos) {
    // Find a free slot
    for (auto& p : m_particles) {
        if (p.Active) continue;

        const auto& ini = m_desc.Init;
        p.Active    = true;
        p.Age       = 0.0f;
        p.Lifetime  = RandRange(ini.LifeMin, ini.LifeMax);
        p.SizeBase  = RandRange(ini.SizeMin, ini.SizeMax);
        p.Size      = p.SizeBase;
        p.Rotation  = Randf() * 360.0f;
        p.RotRate   = RandRange(ini.RotRateMin, ini.RotRateMax);
        p.Color     = ini.ColorStart;
        p.Position  = SampleSpawnPos(worldPos);
        p.Velocity  = SampleVelocity();
        return;
    }
}

Vector3 Emitter::SampleSpawnPos(const Vector3& worldPos) const {
    const auto& sp = m_desc.Spawn;
    switch (sp.Shape) {
    case EmitterShape::Sphere: {
        float u = Randf(), v = Randf();
        float theta = 2.0f * PI * u;
        float phi   = std::acos(2.0f * v - 1.0f);
        float r     = sp.ShapeRadius * std::cbrt(Randf());
        return worldPos + Vector3(
            r * std::sin(phi) * std::cos(theta),
            r * std::cos(phi),
            r * std::sin(phi) * std::sin(theta));
    }
    case EmitterShape::Box:
        return worldPos + Vector3(
            RandRange(-sp.ShapeExtent.x, sp.ShapeExtent.x),
            RandRange(-sp.ShapeExtent.y, sp.ShapeExtent.y),
            RandRange(-sp.ShapeExtent.z, sp.ShapeExtent.z));
    case EmitterShape::Cone: {
        float t = Randf() * sp.ShapeRadius;
        float phi = Randf() * 2.0f * PI;
        float halfAngle = sp.ConeAngleDeg * PI / 180.0f;
        float r = t * std::tan(halfAngle);
        // Along emit dir, spread in tangent plane
        Vector3 up   = sp.EmitDir.GetSafeNormal();
        Vector3 tang = (std::abs(up.x) < 0.9f)
            ? Vector3::Cross(up, Vector3(1,0,0)).GetSafeNormal()
            : Vector3::Cross(up, Vector3(0,1,0)).GetSafeNormal();
        Vector3 bitan = Vector3::Cross(up, tang);
        return worldPos + up * t + (tang * std::cos(phi) + bitan * std::sin(phi)) * r;
    }
    default: // Point
        return worldPos;
    }
}

Vector3 Emitter::SampleVelocity() const {
    const auto& ini = m_desc.Init;
    return Vector3(
        RandRange(ini.VelMin.x, ini.VelMax.x),
        RandRange(ini.VelMin.y, ini.VelMax.y),
        RandRange(ini.VelMin.z, ini.VelMax.z));
}

} // namespace Fujin
