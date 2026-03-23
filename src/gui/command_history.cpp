#include "gui/command_history.h"

namespace GuitarAmp {

void CommandHistory::execute(std::unique_ptr<Command> cmd) {
    // Execute the command first
    cmd->execute();

    // Try coalescing with the top of the undo stack
    if (!undo_stack_.empty() && undo_stack_.back()->merge_with(*cmd)) {
        // Merged into existing top — no new entry needed
    } else {
        undo_stack_.push_back(std::move(cmd));
        trim();
    }

    // New action invalidates the redo branch
    redo_stack_.clear();
}

void CommandHistory::push_executed(std::unique_ptr<Command> cmd) {
    // Try coalescing with the top of the undo stack
    if (!undo_stack_.empty() && undo_stack_.back()->merge_with(*cmd)) {
        // Merged into existing top — no new entry needed
    } else {
        undo_stack_.push_back(std::move(cmd));
        trim();
    }

    // New action invalidates the redo branch
    redo_stack_.clear();
}

bool CommandHistory::undo() {
    if (undo_stack_.empty()) return false;

    auto cmd = std::move(undo_stack_.back());
    undo_stack_.pop_back();

    cmd->undo();
    redo_stack_.push_back(std::move(cmd));
    return true;
}

bool CommandHistory::redo() {
    if (redo_stack_.empty()) return false;

    auto cmd = std::move(redo_stack_.back());
    redo_stack_.pop_back();

    cmd->execute();
    undo_stack_.push_back(std::move(cmd));
    return true;
}

void CommandHistory::clear() {
    undo_stack_.clear();
    redo_stack_.clear();
}

const char* CommandHistory::undo_description() const {
    if (undo_stack_.empty()) return nullptr;
    return undo_stack_.back()->description();
}

const char* CommandHistory::redo_description() const {
    if (redo_stack_.empty()) return nullptr;
    return redo_stack_.back()->description();
}

void CommandHistory::trim() {
    while (static_cast<int>(undo_stack_.size()) > max_depth_) {
        undo_stack_.erase(undo_stack_.begin());
    }
}

} // namespace GuitarAmp
