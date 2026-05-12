#include "audio/audio_engine.h"
#include "audio/audio_backend.h"
#include "audio/midi_input.h"  // <-- ADDED: MIDI input support
#include <iostream>
#include <algorithm>

namespace Amplitron {

AudioEngine::AudioEngine() {
    process_buffer_.resize(MAX_BUFFER_SIZE, 0.0f);
    process_buffer_right_.resize(MAX_BUFFER_SIZE, 0.0f);
    backend_ = create_audio_backend();
    
    // ============================================================
    // INITIALIZE MIDI INPUT — ADDED FOR MIDI INPUT SUPPORT
    // ============================================================
    midi_input_ = std::make_unique<::amplitron::MidiInput>();
}

AudioEngine::~AudioEngine() {
    shutdown();
    
    // ============================================================
    // CLEANUP MIDI INPUT — ADDED FOR MIDI INPUT SUPPORT
    // ============================================================
    if (midi_input_) {
        midi_input_->close();
        midi_input_.reset();
    }
    
    destroy_audio_backend(backend_);
    backend_ = nullptr;
}

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
        if (tuner_tap_) {
            tuner_tap_->set_sample_rate(rate);
            tuner_tap_->reset();
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
            if (tuner_tap_) {
                tuner_tap_->set_sample_rate(prev_rate);
                tuner_tap_->reset();
            }
            start();
        } else {
            last_error_.clear();
        }
    }
}

// ============================================================
// OPTIONAL: MIDI-RELATED HELPER METHODS
// You can add these if needed for advanced MIDI features
// ============================================================

/**
 * @brief Process MIDI messages during audio callback.
 * Call this from your audio backend's callback if you want
 * MIDI to be processed at audio rate (lowest latency).
 */
void AudioEngine::process_midi_during_callback() {
    if (midi_input_ && midi_input_->is_open()) {
        midi_input_->process_messages();
    }
}

/**
 * @brief Get MIDI port names for display in GUI.
 * @return Vector of available MIDI input port names.
 */
std::vector<std::string> AudioEngine::get_midi_ports() const {
    if (midi_input_) {
        return midi_input_->get_available_ports();
    }
    return {};
}

/**
 * @brief Open a specific MIDI port.
 * @param port_index Index from get_midi_ports().
 * @return true if opened successfully.
 */
bool AudioEngine::open_midi_port(int port_index) {
    if (midi_input_) {
        return midi_input_->open(port_index);
    }
    return false;
}

/**
 * @brief Close the currently open MIDI port.
 */
void AudioEngine::close_midi_port() {
    if (midi_input_) {
        midi_input_->close();
    }
}

} // namespace Amplitron
