#pragma once
#include "ParticleTypes.h"
#include <vector>

namespace Fujin {

class Emitter {
public:
    void Initialize(const EmitterDesc& desc);
    void Update(float dt, const Vector3& worldPos);
    void Play()      { m_playing = true; }
    void Stop()      { m_playing = false; }
    void Reset();
    void FireBurst() { m_burstDone = false; ++m_burstFireCount; }

    bool     IsPlaying()          const { return m_playing; }
    int      GetActiveCount()     const { return m_activeCount; }
    uint32_t GetResetCount()      const { return m_resetCount; }
    uint32_t GetBurstFireCount()  const { return m_burstFireCount; }

    const std::vector<Particle>& GetParticles() const { return m_particles; }
          EmitterDesc&           GetDesc()             { return m_desc; }
    const EmitterDesc&           GetDesc()       const { return m_desc; }

private:
    EmitterDesc           m_desc;
    std::vector<Particle> m_particles;
    float    m_spawnAccum   = 0.0f;
    float    m_elapsed      = 0.0f;
    int      m_activeCount  = 0;
    uint32_t m_resetCount      = 0;
    uint32_t m_burstFireCount  = 0;
    bool     m_playing         = true;
    bool     m_burstDone       = false;

    void    SpawnOne(const Vector3& worldPos);
    Vector3 SampleSpawnPos(const Vector3& worldPos) const;
    Vector3 SampleVelocity() const;
};

} // namespace Fujin
