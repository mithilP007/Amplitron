#pragma once

#include "common.h"
#include "audio/effect.h"
#include <imgui.h>

namespace GuitarAmp {

class CommandHistory;

class PedalWidget {
public:
    PedalWidget(std::shared_ptr<Effect> effect, int index);

    // Returns true if the pedal should be removed
    bool render();

    int get_index() const { return index_; }
    void set_index(int idx) { index_ = idx; }
    std::shared_ptr<Effect> get_effect() const { return effect_; }
    void set_history(CommandHistory* history) { history_ = history; }

private:
    void render_knob(const char* label, float* value, float min_val, float max_val,
                     float default_val, const char* format = "%.1f");
    void render_toggle(const char* label, bool* value);

    std::shared_ptr<Effect> effect_;
    int index_;
    CommandHistory* history_ = nullptr;

    // Knob drag tracking for undo coalescing
    bool knob_was_active_ = false;
    int active_param_index_ = -1;
    float param_value_before_drag_ = 0.0f;

    // Pedal color scheme based on effect type
    ImVec4 pedal_color_;
    ImVec4 led_color_;

    void assign_colors();
    void commit_param_change(int param_index, float old_val, float new_val);
};

} // namespace GuitarAmp
