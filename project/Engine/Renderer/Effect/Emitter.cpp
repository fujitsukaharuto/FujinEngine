#include "Emitter.h"
#include <cstdlib>
#include <cmath>
#include <algorithm>

namespace Fujin {

static constexpr float PI = 3.14159265f;

static float Randf() {
    return static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
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

    // Burst at start (always fires once, regardless of duration)
    if (!m_burstDone && m_desc.Spawn.BurstCount > 0) {
        for (int i = 0; i < m_desc.Spawn.BurstCount; ++i)
            SpawnOne(worldPos);
        m_burstDone = true;
    }

    // Continuous spawn: stop when non-looping and duration has elapsed
    if (m_desc.Loop || m_elapsed <= m_desc.Duration) {
        m_spawnAccum += dt * m_desc.Spawn.RatePerSecond;
        while (m_spawnAccum >= 1.0f) {
            m_spawnAccum -= 1.0f;
            SpawnOne(worldPos);
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

        // Turbulence: sin-based position-sampled noise field
        if (upd.Turbulence) {
            float px = p.Position.x * upd.TurbFrequency;
            float py = p.Position.y * upd.TurbFrequency;
            float pz = p.Position.z * upd.TurbFrequency;
            float e  = m_elapsed;
            float nx = std::sin(px * 1.72f + py * 2.31f + e * 0.83f) * std::cos(pz * 1.13f + e * 1.21f);
            float ny = std::sin(py * 2.17f + pz * 1.53f + e * 1.07f) * std::cos(px * 1.29f + e * 0.71f);
            float nz = std::sin(pz * 1.89f + px * 0.93f + e * 0.97f) * std::cos(py * 1.71f + e * 1.33f);
            p.Velocity += Vector3(nx, ny, nz) * (upd.TurbStrength * dt);
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

        // Size over life
        if (upd.ShrinkSize) {
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
