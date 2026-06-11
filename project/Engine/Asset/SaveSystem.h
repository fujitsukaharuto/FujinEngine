#pragma once
#include <string>
#include <vector>

namespace Fujin {

class SceneManager;
class SaveGame;

// Gameplay save/load to named slots (the engine's UGameplayStatics::SaveGameToSlot analog). Each slot
// is a JSON file under the Saves/ directory holding: metadata (slot name + timestamp), a world
// snapshot (the current actors/components, captured via SceneSerializer), and an optional SaveGame
// data bag. Because the snapshot is taken from the live SceneManager, saving during Play records the
// current gameplay positions; LoadFromSlot rebuilds the scene from the slot.
class SaveSystem {
public:
    // Write the current scene (+ optional data) to `slot`. Creates Saves/ if needed. Returns false on
    // file I/O failure.
    static bool SaveToSlot(const std::string& slot, const SceneManager& scene, const SaveGame* data = nullptr);

    // Replace the scene with the contents of `slot` (and fill `outData` if given). Returns false if the
    // slot is missing/corrupt; the scene is left untouched in that case until the world parse succeeds.
    static bool LoadFromSlot(const std::string& slot, SceneManager& scene, SaveGame* outData = nullptr);

    static bool                     SlotExists(const std::string& slot);
    static bool                     DeleteSlot(const std::string& slot);
    static std::vector<std::string> ListSlots();                 // slot names (no path/extension)
    static std::string              SlotPath(const std::string& slot);  // Saves/<slot>.save.json

    static const char* SavesDir() { return "Saves"; }
};

} // namespace Fujin
