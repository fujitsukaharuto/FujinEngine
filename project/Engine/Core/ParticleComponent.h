#pragma once
#include "Component.h"
#include "Engine/Renderer/Effect/Emitter.h"
#include <vector>
#include <string>

namespace Fujin {

class ParticleComponent : public Component {
public:
    std::string EffectPath;    // .fx.json path (empty = in-memory only)
    bool        AutoPlay = true;

    // Runtime emitter instances
    std::vector<Emitter>&       GetEmitters()       { return m_emitters; }
    const std::vector<Emitter>& GetEmitters() const { return m_emitters; }

    void AddEmitter(const EmitterDesc& desc);
    void Play();
    void Stop();
    void Reset();

    // Load/save .fx.json
    bool LoadEffect(const std::string& path);
    bool SaveEffect(const std::string& path) const;

    const char* GetTypeName() const override { return "ParticleComponent"; }
    void ToJson(nlohmann::json& j)   const override;
    void FromJson(const nlohmann::json& j) override;

private:
    std::vector<Emitter> m_emitters;
};

} // namespace Fujin
