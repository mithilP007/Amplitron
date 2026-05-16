#include "test_framework.h"
#include "audio/audio_engine.h"
#include "audio/effects/overdrive.h"
#include "preset_manager.h"
#include <cstring>
#include <memory>
#include <vector>

using namespace Amplitron;

// =============================================================================
// Headless Mirror of GuiPresets State Capture (Copied from gui_presets.cpp)
// =============================================================================

static PresetData capture_current_state(AudioEngine& engine) {
    PresetData preset;
    preset.input_gain = engine.get_input_gain();
    preset.output_gain = engine.get_output_gain();

    for (auto& fx : engine.effects()) {
        PresetData::EffectData fd;
        fd.type = fx->name();
        fd.enabled = fx->is_enabled();
        fd.mix = fx->get_mix();
        for (auto& p : fx->params()) {
            fd.params.push_back({p.name, p.value});
        }
        preset.effects.push_back(std::move(fd));
    }
    return preset;
}

static bool equal_effect_data(const PresetData::EffectData& a, const PresetData::EffectData& b) {
    if (a.type != b.type || a.enabled != b.enabled || a.mix != b.mix) return false;
    if (a.params.size() != b.params.size()) return false;
    for (size_t i = 0; i < a.params.size(); ++i) {
        if (a.params[i] != b.params[i]) return false;
    }
    return true;
}

static bool equal_preset_data(const PresetData& a, const PresetData& b) {
    if (a.input_gain != b.input_gain || a.output_gain != b.output_gain) return false;
    if (a.effects.size() != b.effects.size()) return false;
    for (size_t i = 0; i < a.effects.size(); ++i) {
        if (!equal_effect_data(a.effects[i], b.effects[i])) return false;
    }
    return true;
}

// =============================================================================
// Isolated State Tracking Assertions
// =============================================================================

TEST(presets_dirty_flag_input_gain) {
    AudioEngine engine;
    engine.initialize();

    // 1. Establish the "clean" state baseline snapshot
    PresetData saved_state = capture_current_state(engine);
    ASSERT_TRUE(equal_preset_data(saved_state, capture_current_state(engine))); // Should match baseline

    // 2. Modify input gain to simulate an unsaved adjustment
    engine.set_input_gain(engine.get_input_gain() + 0.123f);
    
    // Check "is_dirty" evaluation logic explicitly
    bool is_dirty = !equal_preset_data(saved_state, capture_current_state(engine));
    ASSERT_TRUE(is_dirty);

    // 3. Reset the baseline to simulate a "mark_clean" event (Save/Load flow)
    saved_state = capture_current_state(engine);
    is_dirty = !equal_preset_data(saved_state, capture_current_state(engine));
    ASSERT_FALSE(is_dirty);

    engine.shutdown();
}

TEST(presets_dirty_on_add_effect) {
    AudioEngine engine;
    engine.initialize();

    // 1. Establish the "clean" state baseline snapshot
    PresetData saved_state = capture_current_state(engine);
    ASSERT_TRUE(equal_preset_data(saved_state, capture_current_state(engine)));

    // 2. Add an effect component to simulate an unsaved chain alteration
    auto od = std::make_shared<Overdrive>();
    engine.add_effect(od);
    
    // Check "is_dirty" evaluation logic explicitly
    bool is_dirty = !equal_preset_data(saved_state, capture_current_state(engine));
    ASSERT_TRUE(is_dirty);

    // 3. Reset the baseline to simulate a "mark_clean" event
    saved_state = capture_current_state(engine);
    is_dirty = !equal_preset_data(saved_state, capture_current_state(engine));
    ASSERT_FALSE(is_dirty);

    engine.shutdown();
}