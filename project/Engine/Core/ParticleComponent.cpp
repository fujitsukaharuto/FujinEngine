#include "ParticleComponent.h"
#include <fstream>
#include <filesystem>

namespace Fujin {

static const bool s_particleRegistered = []() {
    ComponentRegistry::Get().Register("ParticleComponent", []() {
        return std::make_unique<ParticleComponent>();
    });
    return true;
}();

void ParticleComponent::AddEmitter(const EmitterDesc& desc) {
    m_emitters.emplace_back().Initialize(desc);
}

void ParticleComponent::Play() {
    for (auto& e : m_emitters) e.Play();
}

void ParticleComponent::Stop() {
    for (auto& e : m_emitters) e.Stop();
}

void ParticleComponent::Reset() {
    for (auto& e : m_emitters) e.Reset();
}

bool ParticleComponent::LoadEffect(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    if (j.is_discarded()) return false;
    m_emitters.clear();
    if (j.contains("emitters") && j["emitters"].is_array()) {
        for (auto& ej : j["emitters"]) {
            EmitterDesc desc;
            desc.FromJson(ej);
            m_emitters.emplace_back().Initialize(desc);
        }
    }
    EffectPath = path;
    return true;
}

bool ParticleComponent::SaveEffect(const std::string& path) const {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    nlohmann::json j;
    j["emitters"] = nlohmann::json::array();
    for (auto& e : m_emitters) {
        nlohmann::json ej;
        e.GetDesc().ToJson(ej);
        j["emitters"].push_back(ej);
    }
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << j.dump(4);
    return true;
}

void ParticleComponent::ToJson(nlohmann::json& j) const {
    j["effectPath"] = EffectPath;
    j["autoPlay"]   = AutoPlay;
}

void ParticleComponent::FromJson(const nlohmann::json& j) {
    EffectPath = j.value("effectPath", "");
    AutoPlay   = j.value("autoPlay",   true);
    if (!EffectPath.empty())
        LoadEffect(EffectPath);
}

} // namespace Fujin
