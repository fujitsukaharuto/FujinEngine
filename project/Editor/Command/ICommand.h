#pragma once

namespace Fujin {

struct ICommand {
    virtual ~ICommand() = default;
    virtual void Execute() = 0;
    virtual void Undo()    = 0;
};

} // namespace Fujin
