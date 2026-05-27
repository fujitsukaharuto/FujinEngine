#include "TransformCommand.h"
#include "Engine/Core/TransformComponent.h"

namespace Fujin {

void TransformCommand::Apply(const State& s) {
    if (!m_tc) return;
    m_tc->Position = s.position;
    m_tc->Rotation = s.rotation;
    m_tc->Scale    = s.scale;
}

} // namespace Fujin
