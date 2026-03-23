#pragma once

#include "common.h"
#include "audio/audio_engine.h"
#include "audio/effect.h"
#include <chrono>

namespace GuitarAmp {

// Base class for all undoable commands (Gang of Four Command Pattern)
class Command {
public:
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual const char* description() const = 0;

    // For coalescing: two commands can merge if they affect the same target
    // within a short time window. Returns true if this command absorbed `other`.
    virtual bool merge_with(const Command& /*other*/) { return false; }

    auto timestamp() const { return timestamp_; }

protected:
    std::chrono::steady_clock::time_point timestamp_ = std::chrono::steady_clock::now();
};

// ---------------------------------------------------------------------------
// AddEffectCommand
// ---------------------------------------------------------------------------
class AddEffectCommand : public Command {
public:
    AddEffectCommand(AudioEngine& engine, std::shared_ptr<Effect> effect)
        : engine_(engine), effect_(std::move(effect)) {}

    void execute() override {
        engine_.add_effect(effect_);
    }

    void undo() override {
        auto& fx = engine_.effects();
        // Find and remove the effect we added (should be at the end)
        for (int i = static_cast<int>(fx.size()) - 1; i >= 0; --i) {
            if (fx[i] == effect_) {
                engine_.remove_effect(i);
                return;
            }
        }
    }

    const char* description() const override { return "Add Effect"; }

    std::shared_ptr<Effect> effect() const { return effect_; }

private:
    AudioEngine& engine_;
    std::shared_ptr<Effect> effect_;
};

// ---------------------------------------------------------------------------
// RemoveEffectCommand
// ---------------------------------------------------------------------------
class RemoveEffectCommand : public Command {
public:
    RemoveEffectCommand(AudioEngine& engine, int index)
        : engine_(engine), index_(index) {
        // Capture the effect before removal
        auto& fx = engine_.effects();
        if (index >= 0 && index < static_cast<int>(fx.size())) {
            effect_ = fx[index];
        }
    }

    void execute() override {
        engine_.remove_effect(index_);
    }

    void undo() override {
        if (effect_) {
            // Re-insert at original position
            auto& fx = engine_.effects();
            int pos = std::min(index_, static_cast<int>(fx.size()));
            engine_.add_effect(effect_);
            // add_effect appends; move it to the correct position
            int last = static_cast<int>(engine_.effects().size()) - 1;
            if (last != pos) {
                engine_.move_effect(last, pos);
            }
        }
    }

    const char* description() const override { return "Remove Effect"; }

    int index() const { return index_; }
    std::shared_ptr<Effect> effect() const { return effect_; }

private:
    AudioEngine& engine_;
    int index_;
    std::shared_ptr<Effect> effect_;
};

// ---------------------------------------------------------------------------
// ReorderEffectCommand
// ---------------------------------------------------------------------------
class ReorderEffectCommand : public Command {
public:
    ReorderEffectCommand(AudioEngine& engine, int from, int to)
        : engine_(engine), from_(from), to_(to) {}

    void execute() override {
        engine_.move_effect(from_, to_);
    }

    void undo() override {
        engine_.move_effect(to_, from_);
    }

    const char* description() const override { return "Reorder Effect"; }

    int from() const { return from_; }
    int to() const { return to_; }

private:
    AudioEngine& engine_;
    int from_;
    int to_;
};

// ---------------------------------------------------------------------------
// ParameterChangeCommand (supports coalescing)
// Uses shared_ptr<Effect> directly — robust against effect reordering.
// ---------------------------------------------------------------------------
class ParameterChangeCommand : public Command {
public:
    ParameterChangeCommand(std::shared_ptr<Effect> effect,
                           int param_index, float old_value, float new_value)
        : effect_(std::move(effect)),
          param_index_(param_index), old_value_(old_value), new_value_(new_value) {}

    void execute() override {
        auto& params = effect_->params();
        if (param_index_ >= 0 && param_index_ < static_cast<int>(params.size())) {
            params[param_index_].value = new_value_;
        }
    }

    void undo() override {
        auto& params = effect_->params();
        if (param_index_ >= 0 && param_index_ < static_cast<int>(params.size())) {
            params[param_index_].value = old_value_;
        }
    }

    const char* description() const override { return "Change Parameter"; }

    bool merge_with(const Command& other) override {
        auto* pc = dynamic_cast<const ParameterChangeCommand*>(&other);
        if (!pc) return false;
        if (pc->effect_.get() != effect_.get() || pc->param_index_ != param_index_)
            return false;

        // Coalesce if within 500ms
        auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
            pc->timestamp_ - timestamp_);
        if (dt.count() > 500) return false;

        // Absorb: keep our old_value_, update new_value_ and timestamp
        new_value_ = pc->new_value_;
        timestamp_ = pc->timestamp_;
        return true;
    }

    std::shared_ptr<Effect> effect() const { return effect_; }
    int param_index() const { return param_index_; }
    float old_value() const { return old_value_; }
    float new_value() const { return new_value_; }

private:
    std::shared_ptr<Effect> effect_;
    int param_index_;
    float old_value_;
    float new_value_;
};

// ---------------------------------------------------------------------------
// LoadPresetCommand
// ---------------------------------------------------------------------------
class LoadPresetCommand : public Command {
public:
    // Captures the full state before and after a preset load.
    // before_effects / after_effects hold shared_ptrs + param snapshots.
    struct EffectSnapshot {
        std::shared_ptr<Effect> effect;
        bool enabled;
        float mix;
        std::vector<float> param_values;
    };

    LoadPresetCommand(AudioEngine& engine,
                      std::vector<EffectSnapshot> before_state,
                      float before_input_gain, float before_output_gain,
                      std::vector<EffectSnapshot> after_state,
                      float after_input_gain, float after_output_gain)
        : engine_(engine),
          before_state_(std::move(before_state)),
          before_input_gain_(before_input_gain),
          before_output_gain_(before_output_gain),
          after_state_(std::move(after_state)),
          after_input_gain_(after_input_gain),
          after_output_gain_(after_output_gain) {}

    void execute() override {
        apply_state(after_state_, after_input_gain_, after_output_gain_);
    }

    void undo() override {
        apply_state(before_state_, before_input_gain_, before_output_gain_);
    }

    const char* description() const override { return "Load Preset"; }

private:
    void apply_state(const std::vector<EffectSnapshot>& state,
                     float input_gain, float output_gain) {
        // Clear current effects
        auto& fx = engine_.effects();
        while (!fx.empty()) {
            engine_.remove_effect(static_cast<int>(fx.size()) - 1);
        }

        // Restore effects
        for (auto& snap : state) {
            snap.effect->set_enabled(snap.enabled);
            snap.effect->set_mix(snap.mix);
            auto& params = snap.effect->params();
            for (int i = 0; i < static_cast<int>(params.size()) &&
                            i < static_cast<int>(snap.param_values.size()); ++i) {
                params[i].value = snap.param_values[i];
            }
            engine_.add_effect(snap.effect);
        }

        engine_.set_input_gain(input_gain);
        engine_.set_output_gain(output_gain);
    }

    AudioEngine& engine_;
    std::vector<EffectSnapshot> before_state_;
    float before_input_gain_;
    float before_output_gain_;
    std::vector<EffectSnapshot> after_state_;
    float after_input_gain_;
    float after_output_gain_;
};

} // namespace GuitarAmp
