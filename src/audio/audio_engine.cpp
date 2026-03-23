#include "audio/audio_engine.h"
#include "audio/audio_backend.h"
#include <cstring>
#include <iostream>
#include <algorithm>
#include <chrono>

namespace GuitarAmp {

// =============================================================================
// Construction / destruction
// =============================================================================

AudioEngine::AudioEngine() {
    process_buffer_.resize(MAX_BUFFER_SIZE, 0.0f);
    backend_ = create_audio_backend();
}

AudioEngine::~AudioEngine() {
    shutdown();
    destroy_audio_backend(backend_);
    backend_ = nullptr;
}

// =============================================================================
// Effect chain management (platform-agnostic)
// =============================================================================

void AudioEngine::add_effect(std::shared_ptr<Effect> effect) {
    std::lock_guard<std::mutex> lock(effect_mutex_);
    effect->set_sample_rate(sample_rate_);
    effects_.push_back(std::move(effect));
}

void AudioEngine::remove_effect(int index) {
    std::lock_guard<std::mutex> lock(effect_mutex_);
    if (index >= 0 && index < static_cast<int>(effects_.size())) {
        effects_.erase(effects_.begin() + index);
    }
}

void AudioEngine::restore_effects_state(std::vector<std::shared_ptr<Effect>> new_effects) {
    std::lock_guard<std::mutex> lock(effect_mutex_);
    effects_.clear();
    for (auto& fx : new_effects) {
        fx->set_sample_rate(sample_rate_);
        effects_.push_back(std::move(fx));
    }
}

void AudioEngine::move_effect(int from, int to) {
    std::lock_guard<std::mutex> lock(effect_mutex_);
    int n = static_cast<int>(effects_.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
    auto effect = effects_[from];
    effects_.erase(effects_.begin() + from);
    effects_.insert(effects_.begin() + to, effect);
}

// =============================================================================
// Buffer / sample rate settings (call stop/start which are backend-provided)
// =============================================================================

void AudioEngine::set_buffer_size(int size) {
    size = std::max(MIN_BUFFER_SIZE, std::min(MAX_BUFFER_SIZE, size));
    int prev_size = buffer_size_;
    bool was_running = running_;
    if (was_running) stop();
    buffer_size_ = size;
    if (was_running) {
        if (!start()) {
            last_error_ = "Failed with buffer size " + std::to_string(size) + ". Reverting.";
            std::cerr << "[Amplitron] " << last_error_ << std::endl;
            buffer_size_ = prev_size;
            start();
        } else {
            last_error_.clear();
        }
    }
}

void AudioEngine::set_sample_rate(int rate) {
    int prev_rate = sample_rate_;
    bool was_running = running_;
    if (was_running) stop();
    sample_rate_ = rate;
    {
        std::lock_guard<std::mutex> lock(effect_mutex_);
        for (auto& fx : effects_) {
            fx->set_sample_rate(rate);
            fx->reset();
        }
    }
    if (was_running) {
        if (!start()) {
            last_error_ = "Failed with sample rate " + std::to_string(rate) + " Hz. Reverting.";
            std::cerr << "[Amplitron] " << last_error_ << std::endl;
            sample_rate_ = prev_rate;
            std::lock_guard<std::mutex> lock(effect_mutex_);
            for (auto& fx : effects_) {
                fx->set_sample_rate(prev_rate);
                fx->reset();
            }
            start();
        } else {
            last_error_.clear();
        }
    }
}

// =============================================================================
// Audio processing pipeline (called from platform callback)
// =============================================================================

void AudioEngine::process_audio(const float* input, float* output, int frame_count) {
    auto t_start = std::chrono::steady_clock::now();

    // Safety: ensure process buffer is large enough
    if (frame_count > static_cast<int>(process_buffer_.size())) {
        process_buffer_.resize(frame_count, 0.0f);
    }

    // Drain lock-free command queue (GUI -> Audio)
    drain_commands();

    // Copy input to processing buffer with gain
    float in_gain = input_gain_.load(std::memory_order_relaxed);
    float peak_in = 0.0f;
    for (int i = 0; i < frame_count; ++i) {
        process_buffer_[i] = input[i] * in_gain;
        float abs_val = std::fabs(process_buffer_[i]);
        if (abs_val > peak_in) peak_in = abs_val;
    }
    input_level_.store(peak_in);

    // Process through effect chain
    // Structural changes (add/remove/move) still use try_lock for safety;
    // parameter changes are fully lock-free via the SPSC queue above.
    if (effect_mutex_.try_lock()) {
        for (auto& fx : effects_) {
            if (fx->is_enabled()) {
                fx->process(process_buffer_.data(), frame_count);
            }
        }
        effect_mutex_.unlock();
    }

    // Copy to output with gain
    float out_gain = output_gain_.load(std::memory_order_relaxed);
    float peak_out = 0.0f;
    for (int i = 0; i < frame_count; ++i) {
        output[i] = process_buffer_[i] * out_gain;
        output[i] = clamp(output[i], -1.0f, 1.0f);
        float abs_val = std::fabs(output[i]);
        if (abs_val > peak_out) peak_out = abs_val;
    }
    output_level_.store(peak_out);

    // Record the processed output
    if (recorder_.is_recording()) {
        recorder_.write_samples(output, frame_count);
    }

    // CPU load measurement
    auto t_end = std::chrono::steady_clock::now();
    float duration_us = std::chrono::duration<float, std::micro>(t_end - t_start).count();
    callback_duration_us_.store(duration_us, std::memory_order_relaxed);
    float budget_us = (static_cast<float>(frame_count) / sample_rate_) * 1e6f;
    cpu_load_.store(duration_us / budget_us, std::memory_order_relaxed);
}

// =============================================================================
// Lock-free command drain (called from audio thread)
// =============================================================================

void AudioEngine::drain_commands() {
    AudioCommand cmd;
    while (command_queue_.try_pop(cmd)) {
        switch (cmd.type) {
            case AudioCommand::SetEffectParam:
                if (cmd.effect_index >= 0 &&
                    cmd.effect_index < static_cast<int>(effects_.size())) {
                    auto& params = effects_[cmd.effect_index]->params();
                    if (cmd.param_index >= 0 &&
                        cmd.param_index < static_cast<int>(params.size())) {
                        params[cmd.param_index].value = cmd.value;
                    }
                }
                break;
            case AudioCommand::SetEffectEnabled:
                if (cmd.effect_index >= 0 &&
                    cmd.effect_index < static_cast<int>(effects_.size())) {
                    effects_[cmd.effect_index]->set_enabled(cmd.value > 0.5f);
                }
                break;
            case AudioCommand::SetEffectMix:
                if (cmd.effect_index >= 0 &&
                    cmd.effect_index < static_cast<int>(effects_.size())) {
                    effects_[cmd.effect_index]->set_mix(cmd.value);
                }
                break;
            case AudioCommand::SetInputGain:
                input_gain_.store(cmd.value, std::memory_order_relaxed);
                break;
            case AudioCommand::SetOutputGain:
                output_gain_.store(cmd.value, std::memory_order_relaxed);
                break;
            default:
                break;
        }
    }
}

// =============================================================================
// GUI-thread SPSC push methods
// =============================================================================

void AudioEngine::set_input_gain(float gain) {
    AudioCommand cmd{};
    cmd.type = AudioCommand::SetInputGain;
    cmd.value = gain;
    command_queue_.try_push(cmd);
    input_gain_.store(gain, std::memory_order_relaxed);  // immediate read-back for GUI
}

void AudioEngine::set_output_gain(float gain) {
    AudioCommand cmd{};
    cmd.type = AudioCommand::SetOutputGain;
    cmd.value = gain;
    command_queue_.try_push(cmd);
    output_gain_.store(gain, std::memory_order_relaxed);
}

void AudioEngine::push_param_change(int effect_index, int param_index, float value) {
    AudioCommand cmd{};
    cmd.type = AudioCommand::SetEffectParam;
    cmd.effect_index = effect_index;
    cmd.param_index = param_index;
    cmd.value = value;
    command_queue_.try_push(cmd);
}

void AudioEngine::push_effect_enabled(int effect_index, float enabled) {
    AudioCommand cmd{};
    cmd.type = AudioCommand::SetEffectEnabled;
    cmd.effect_index = effect_index;
    cmd.value = enabled;
    command_queue_.try_push(cmd);
}

void AudioEngine::push_effect_mix(int effect_index, float mix) {
    AudioCommand cmd{};
    cmd.type = AudioCommand::SetEffectMix;
    cmd.effect_index = effect_index;
    cmd.value = mix;
    command_queue_.try_push(cmd);
}

// =============================================================================
// Buffer auto-tuning
// =============================================================================

int AudioEngine::get_suggested_buffer_size() const {
    float load = cpu_load_.load(std::memory_order_relaxed);
    int current = buffer_size_;

    // If consistently overloaded (>80%), suggest next larger buffer
    if (load > 0.80f) {
        if (current < MAX_BUFFER_SIZE) {
            return std::min(current * 2, MAX_BUFFER_SIZE);
        }
    }
    // If consistently underloaded (<30%), suggest next smaller buffer
    if (load < 0.30f) {
        if (current > MIN_BUFFER_SIZE) {
            return std::max(current / 2, MIN_BUFFER_SIZE);
        }
    }
    return current;  // no change
}

} // namespace GuitarAmp
