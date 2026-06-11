#include "SaveSystem.h"
#include "SceneSerializer.h"
#include "Engine/Core/SceneManager.h"
#include "Engine/Core/SaveGame.h"
#include <json.hpp>
#include <fstream>
#include <filesystem>
#include <ctime>
#include <cstdint>

namespace fs = std::filesystem;

namespace Fujin {

static constexpr int kSaveVersion = 1;

std::string SaveSystem::SlotPath(const std::string& slot) {
    return std::string(SavesDir()) + "/" + slot + ".save.json";
}

bool SaveSystem::SaveToSlot(const std::string& slot, const SceneManager& scene, const SaveGame* data) {
    std::error_code ec;
    fs::create_directories(SavesDir(), ec);   // no-op if it already exists

    nlohmann::json root;
    root["version"] = kSaveVersion;

    nlohmann::json meta;
    meta["slot"] = slot;
    const std::time_t now = std::time(nullptr);
    meta["savedAtUnix"] = static_cast<int64_t>(now);
    std::tm tmv{};
    char stamp[64] = {};
    if (localtime_s(&tmv, &now) == 0 && std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tmv))
        meta["savedAtLocal"] = stamp;
    root["meta"] = std::move(meta);

    SceneSerializer::SaveToJson(scene, root["world"]);
    root["data"] = data ? data->Data() : nlohmann::json::object();

    std::ofstream file(SlotPath(slot));
    if (!file) return false;
    file << root.dump(2);
    return true;
}

bool SaveSystem::LoadFromSlot(const std::string& slot, SceneManager& scene, SaveGame* outData) {
    std::ifstream file(SlotPath(slot));
    if (!file) return false;
    nlohmann::json root;
    try { file >> root; } catch (...) { return false; }

    if (!root.contains("world")) return false;
    if (!SceneSerializer::LoadFromJson(scene, root["world"])) return false;
    if (outData) outData->SetData(root.value("data", nlohmann::json::object()));
    return true;
}

bool SaveSystem::SlotExists(const std::string& slot) {
    std::error_code ec;
    return fs::exists(SlotPath(slot), ec);
}

bool SaveSystem::DeleteSlot(const std::string& slot) {
    std::error_code ec;
    return fs::remove(SlotPath(slot), ec);
}

std::vector<std::string> SaveSystem::ListSlots() {
    std::vector<std::string> slots;
    std::error_code ec;
    if (!fs::exists(SavesDir(), ec)) return slots;
    const std::string suffix = ".save.json";
    for (auto& entry : fs::directory_iterator(SavesDir(), ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (name.size() > suffix.size()
            && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
            slots.push_back(name.substr(0, name.size() - suffix.size()));
    }
    return slots;
}

} // namespace Fujin
