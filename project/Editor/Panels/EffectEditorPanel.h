#pragma once
#include "Engine/Core/Actor.h"

namespace Fujin {

class ParticleComponent;
class Emitter;

class EffectEditorPanel {
public:
    // Normal docked panel (shows when no actor is in effect-edit mode)
    void Draw(Actor* actor);

    // Large floating window opened via "Edit Effect" button in Details.
    // open is set to false when the user clicks Close.
    void DrawFull(Actor* actor, bool& open);

private:
    int  m_selectedEmitter = -1;
    int  m_selectedGroup   = -1;  // 0=Settings 1=Spawn 2=Init 3=Update 4=Render

    void DrawTopBar(Actor* actor, ParticleComponent* pc, bool showClose, bool& open);
    void DrawOverview(ParticleComponent* pc);
    void DrawEmitterCard(int idx, Emitter& em, bool selected, int& outRemove);
    void DrawStack(Emitter& em);
    void DrawParameters(Emitter& em);
    void DrawSaveLoadBar(ParticleComponent* pc);
    void DrawContents(Actor* actor, ParticleComponent* pc, bool showClose, bool& open);
};

} // namespace Fujin
