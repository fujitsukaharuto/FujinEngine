#pragma once
#include "ICommand.h"
#include <memory>
#include <vector>

namespace Fujin {

class CommandHistory {
public:
    void Push(std::unique_ptr<ICommand> cmd) {
        cmd->Execute();
        m_undoStack.push_back(std::move(cmd));
        m_redoStack.clear();
        if (m_undoStack.size() > MAX_HISTORY)
            m_undoStack.erase(m_undoStack.begin());
    }

    void Undo() {
        if (m_undoStack.empty()) return;
        auto& cmd = m_undoStack.back();
        cmd->Undo();
        m_redoStack.push_back(std::move(cmd));
        m_undoStack.pop_back();
    }

    void Redo() {
        if (m_redoStack.empty()) return;
        auto& cmd = m_redoStack.back();
        cmd->Execute();
        m_undoStack.push_back(std::move(cmd));
        m_redoStack.pop_back();
    }

    bool CanUndo() const { return !m_undoStack.empty(); }
    bool CanRedo() const { return !m_redoStack.empty(); }

private:
    static constexpr size_t MAX_HISTORY = 64;
    std::vector<std::unique_ptr<ICommand>> m_undoStack;
    std::vector<std::unique_ptr<ICommand>> m_redoStack;
};

} // namespace Fujin
