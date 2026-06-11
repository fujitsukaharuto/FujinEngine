#pragma once
#include <string>
#include <json.hpp>
#include "Engine/Math/Math.h"   // Vector3

namespace Fujin {

// Arbitrary gameplay data persisted alongside the world in a save slot (the engine's USaveGame
// analog). A typed key/value bag backed by JSON: gameplay code stuffs scores, flags, timestamps,
// the active level name, etc., then reads them back after SaveSystem::LoadFromSlot. The world itself
// (actors + components) is captured separately by SaveSystem, so this only holds data that isn't
// already an actor/component.
class SaveGame {
public:
    void SetInt(const std::string& k, int v)                    { m_data[k] = v; }
    void SetFloat(const std::string& k, float v)                { m_data[k] = v; }
    void SetBool(const std::string& k, bool v)                  { m_data[k] = v; }
    void SetString(const std::string& k, const std::string& v)  { m_data[k] = v; }
    void SetVec3(const std::string& k, const Vector3& v)        { m_data[k] = { v.x, v.y, v.z }; }

    int         GetInt(const std::string& k, int def = 0) const                 { return m_data.value(k, def); }
    float       GetFloat(const std::string& k, float def = 0.0f) const          { return m_data.value(k, def); }
    bool        GetBool(const std::string& k, bool def = false) const           { return m_data.value(k, def); }
    std::string GetString(const std::string& k, const std::string& def = "") const { return m_data.value(k, def); }
    Vector3     GetVec3(const std::string& k, const Vector3& def = Vector3()) const {
        auto it = m_data.find(k);
        if (it != m_data.end() && it->is_array() && it->size() == 3)
            return Vector3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        return def;
    }

    bool Has(const std::string& k) const { return m_data.contains(k); }
    void Remove(const std::string& k)    { m_data.erase(k); }
    void Clear()                         { m_data = nlohmann::json::object(); }

    // Raw access for SaveSystem (de)serialization.
    const nlohmann::json& Data() const          { return m_data; }
    void SetData(const nlohmann::json& j)        { m_data = j.is_object() ? j : nlohmann::json::object(); }

private:
    nlohmann::json m_data = nlohmann::json::object();
};

} // namespace Fujin
