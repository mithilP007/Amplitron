#pragma once

#include "gui/command.h"
#include <vector>
#include <memory>

namespace GuitarAmp {

class CommandHistory {
public:
    static constexpr int DEFAULT_MAX_DEPTH = 50;

    explicit CommandHistory(int max_depth = DEFAULT_MAX_DEPTH)
        : max_depth_(max_depth) {}

    // Execute a command and push it onto the undo stack.
    // Clears the redo stack (new action invalidates redo branch).
    // Attempts coalescing with the top of the undo stack.
    void execute(std::unique_ptr<Command> cmd);

    // Push a command that was already executed (e.g. knob changes applied
    // directly by the widget). Records for undo without calling execute().
    void push_executed(std::unique_ptr<Command> cmd);

    // Undo the most recent command
    bool undo();

    // Redo the most recently undone command
    bool redo();

    // Clear all history (e.g. when loading a preset)
    void clear();

    // Query
    bool can_undo() const { return !undo_stack_.empty(); }
    bool can_redo() const { return !redo_stack_.empty(); }
    int undo_size() const { return static_cast<int>(undo_stack_.size()); }
    int redo_size() const { return static_cast<int>(redo_stack_.size()); }
    int max_depth() const { return max_depth_; }
    void set_max_depth(int depth) { max_depth_ = depth; trim(); }

    // Peek at the top command description (for UI tooltip)
    const char* undo_description() const;
    const char* redo_description() const;

private:
    void trim();

    std::vector<std::unique_ptr<Command>> undo_stack_;
    std::vector<std::unique_ptr<Command>> redo_stack_;
    int max_depth_;
};

} // namespace GuitarAmp
