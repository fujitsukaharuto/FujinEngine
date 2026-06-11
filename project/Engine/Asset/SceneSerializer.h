#pragma once
#include <string>
#include <json.hpp>

namespace Fujin {

class SceneManager;

class SceneSerializer {
public:
    static bool Save(const SceneManager& scene, const std::string& filePath);
    static bool Load(SceneManager& scene, const std::string& filePath);

    // Serialize / deserialize just the world (actors + nextId) to / from a JSON object. Shared by the
    // file Save/Load above and the gameplay SaveSystem, which wraps this in a save-slot envelope so a
    // slot can also carry metadata + an arbitrary SaveGame data bag without duplicating actor logic.
    static void SaveToJson(const SceneManager& scene, nlohmann::json& out);
    static bool LoadFromJson(SceneManager& scene, const nlohmann::json& in);
};

} // namespace Fujin
