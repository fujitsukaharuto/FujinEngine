#include "SceneSerializer.h"
#include "Engine/Core/SceneManager.h"
#include "Engine/Core/Actor.h"
#include <fstream>
#include <json.hpp>

namespace Fujin {

bool SceneSerializer::Save(const SceneManager& scene, const std::string& filePath) {
    nlohmann::json root;
    root["version"] = 1;
    root["nextId"]  = scene.GetNextId();
    root["actors"]  = nlohmann::json::array();
    for (auto& actor : scene.GetActors()) {
        nlohmann::json aj;
        actor->ToJson(aj);
        root["actors"].push_back(std::move(aj));
    }
    std::ofstream file(filePath);
    if (!file) return false;
    file << root.dump(2);
    return true;
}

bool SceneSerializer::Load(SceneManager& scene, const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file) return false;
    nlohmann::json root;
    try { file >> root; } catch (...) { return false; }

    scene.Clear();
    if (root.contains("nextId")) scene.SetNextId(root["nextId"].get<uint64_t>());
    if (!root.contains("actors")) return true;

    std::vector<std::pair<uint64_t, uint64_t>> parentLinks;
    for (auto& aj : root["actors"]) {
        uint64_t    id       = aj.value("id",       uint64_t(0));
        std::string name     = aj.value("name",     std::string("Actor"));
        uint64_t    parentId = aj.value("parentId", uint64_t(0));

        auto actor = std::make_unique<Actor>(name, id);
        actor->FromJson(aj);
        parentLinks.push_back({ id, parentId });
        scene.LoadActor(std::move(actor));
    }
    scene.ResolveParentLinks(parentLinks);
    return true;
}

} // namespace Fujin
