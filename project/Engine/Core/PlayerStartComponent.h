#pragma once
#include "Component.h"
#include "PropertyVisitor.h"
#include <memory>

namespace Fujin {

// PlayerStartComponent — marks an actor as a spawn point for the GameMode's default pawn (the
// engine's analog of a UE5 APlayerStart). The owning actor's TransformComponent supplies the spawn
// position/orientation; the GameMode spawns its DefaultPawnSpawner pawn here (and possesses it) when
// the scene has no pawn placed with AutoPossessPlayer. PlayerIndex lets multiple starts coexist —
// the GameMode picks the first start matching the player it's spawning (0 = player one).
class PlayerStartComponent : public Component {
public:
    int PlayerIndex = 0;

    const char* GetTypeName() const override { return "PlayerStartComponent"; }
    // Reflect is the single source of truth (Inspector + default ToJson/FromJson).
    void Reflect(IPropertyVisitor& v) override { v.Int("playerIndex", "Player Index", &PlayerIndex, 0, 7); }
};

inline const bool s_playerStartRegistered = []() {
    ComponentRegistry::Get().Register("PlayerStartComponent",
        []() { return std::make_unique<PlayerStartComponent>(); });
    return true;
}();

} // namespace Fujin
