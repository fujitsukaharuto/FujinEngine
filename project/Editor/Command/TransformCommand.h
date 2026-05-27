#pragma once
#include "ICommand.h"
#include "Engine/Math/Math.h"

namespace Fujin {

class TransformComponent;

class TransformCommand : public ICommand {
public:
    struct State {
        Vector3    position;
        Quaternion rotation;
        Vector3    scale;
    };

    TransformCommand(TransformComponent* tc, const State& before, const State& after)
        : m_tc(tc), m_before(before), m_after(after) {}

    void Execute() override { Apply(m_after);  }
    void Undo()    override { Apply(m_before); }

private:
    void Apply(const State& s);

    TransformComponent* m_tc;
    State m_before;
    State m_after;
};

} // namespace Fujin
