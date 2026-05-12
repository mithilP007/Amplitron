#include "midi_input.h"
#include <algorithm>
#include <fstream>
#include <iostream>

#ifdef AMPLITRON_HAS_MIDI

namespace amplitron {

MidiInput::MidiInput() 
    : is_open_(false), learning_(false), learn_effect_index_(-1),
      learn_parameter_index_(-1), learn_is_toggle_(false),
      queue_write_idx_(0), queue_read_idx_(0) {
    try {
        midi_in_ = std::make_unique<RtMidiIn>();
        midi_in_->ignoreTypes(false, false, false); // Don't ignore anything
    } catch (const RtMidiError& e) {
        std::cerr << "MidiInput: RtMidi init failed: " << e.getMessage() << std::endl;
    }
}

MidiInput::~MidiInput() {
    close();
}

bool MidiInput::open(int port_index) {
    if (!midi_in_) return false;
    
    try {
        close();
        midi_in_->openPort(port_index);
        is_open_ = true;
        
        // Set callback
        midi_in_->setCallback([](double timeStamp, std::vector<unsigned char>* message, void* userData) {
            auto* self = static_cast<MidiInput*>(userData);
            if (!message || message->size() < 2) return;
            
            MidiMessage msg;
            msg.status = (*message)[0];
            msg.data1 = message->size() > 1 ? (*message)[1] : 0;
            msg.data2 = message->size() > 2 ? (*message)[2] : 0;
            msg.timestamp = timeStamp;
            
            self->push_message(msg);
            
            // Handle learn mode
            if (self->learning_ && MidiInput::is_cc_message(msg.status)) {
                MidiMapping mapping;
                mapping.cc_number = msg.data1;
                mapping.effect_index = self->learn_effect_index_;
                mapping.parameter_index = self->learn_parameter_index_;
                mapping.min_value = 0.0f;
                mapping.max_value = 1.0f;
                mapping.is_toggle = self->learn_is_toggle_;
                
                std::lock_guard<std::mutex> lock(self->mappings_mutex_);
                // Remove existing mapping for this CC
                self->mappings_.erase(
                    std::remove_if(self->mappings_.begin(), self->mappings_.end(),
                        [cc = msg.data1](const MidiMapping& m) { return m.cc_number == cc; }),
                    self->mappings_.end()
                );
                self->mappings_.push_back(mapping);
                self->learning_ = false;
            }
        }, this);
        
        return true;
    } catch (const RtMidiError& e) {
        std::cerr << "MidiInput: Failed to open port " << port_index << ": " << e.getMessage() << std::endl;
        return false;
    }
}

void MidiInput::close() {
    if (midi_in_ && is_open_) {
        midi_in_->closePort();
        is_open_ = false;
    }
}

bool MidiInput::is_open() const {
    return is_open_;
}

std::vector<std::string> MidiInput::get_available_ports() const {
    std::vector<std::string> ports;
    if (!midi_in_) return ports;
    
    try {
        unsigned int nPorts = midi_in_->getPortCount();
        for (unsigned int i = 0; i < nPorts; ++i) {
            ports.push_back(midi_in_->getPortName(i));
        }
    } catch (const RtMidiError& e) {
        std::cerr << "MidiInput: Error getting ports: " << e.getMessage() << std::endl;
    }
    return ports;
}

void MidiInput::set_callback(MidiCallback cb) {
    callback_ = cb;
}

void MidiInput::add_mapping(const MidiMapping& mapping) {
    std::lock_guard<std::mutex> lock(mappings_mutex_);
    // Remove existing
    mappings_.erase(
        std::remove_if(mappings_.begin(), mappings_.end(),
            [cc = mapping.cc_number](const MidiMapping& m) { return m.cc_number == cc; }),
        mappings_.end()
    );
    mappings_.push_back(mapping);
}

void MidiInput::remove_mapping(int cc_number) {
    std::lock_guard<std::mutex> lock(mappings_mutex_);
    mappings_.erase(
        std::remove_if(mappings_.begin(), mappings_.end(),
            [cc = cc_number](const MidiMapping& m) { return m.cc_number == cc; }),
        mappings_.end()
    );
}

void MidiInput::clear_mappings() {
    std::lock_guard<std::mutex> lock(mappings_mutex_);
    mappings_.clear();
}

std::vector<MidiMapping> MidiInput::get_mappings() const {
    std::lock_guard<std::mutex> lock(mappings_mutex_);
    return mappings_;
}

void MidiInput::start_learn(int effect_index, int parameter_index, bool is_toggle) {
    learning_ = true;
    learn_effect_index_ = effect_index;
    learn_parameter_index_ = parameter_index;
    learn_is_toggle_ = is_toggle;
}

void MidiInput::stop_learn() {
    learning_ = false;
    learn_effect_index_ = -1;
    learn_parameter_index_ = -1;
}

bool MidiInput::is_learning() const {
    return learning_;
}

bool MidiInput::save_mappings(const std::string& filepath) const {
    std::lock_guard<std::mutex> lock(mappings_mutex_);
    std::ofstream file(filepath);
    if (!file.is_open()) return false;
    
    file << "{\n  \"mappings\": [\n";
    for (size_t i = 0; i < mappings_.size(); ++i) {
        const auto& m = mappings_[i];
        file << "    {\n";
        file << "      \"cc_number\": " << m.cc_number << ",\n";
        file << "      \"effect_index\": " << m.effect_index << ",\n";
        file << "      \"parameter_index\": " << m.parameter_index << ",\n";
        file << "      \"min_value\": " << m.min_value << ",\n";
        file << "      \"max_value\": " << m.max_value << ",\n";
        file << "      \"is_toggle\": " << (m.is_toggle ? "true" : "false") << "\n";
        file << "    }" << (i < mappings_.size() - 1 ? "," : "") << "\n";
    }
    file << "  ]\n}\n";
    return true;
}

bool MidiInput::load_mappings(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;
    
    // Simple JSON parsing (or use nlohmann/json if available)
    // For now, clear and let user re-learn
    std::lock_guard<std::mutex> lock(mappings_mutex_);
    mappings_.clear();
    return true;
}

void MidiInput::process_messages() {
    MidiMessage msg;
    while (pop_message(msg)) {
        if (callback_) {
            callback_(msg);
        }
    }
}

void MidiInput::push_message(const MidiMessage& msg) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    size_t next = (queue_write_idx_ + 1) % QUEUE_SIZE;
    if (next != queue_read_idx_) {
        message_queue_[queue_write_idx_] = msg;
        queue_write_idx_ = next;
    }
}

bool MidiInput::pop_message(MidiMessage& msg) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (queue_read_idx_ == queue_write_idx_) return false;
    msg = message_queue_[queue_read_idx_];
    queue_read_idx_ = (queue_read_idx_ + 1) % QUEUE_SIZE;
    return true;
}

} // namespace amplitron

#else // Stub implementation when RtMidi is not available

namespace amplitron {

MidiInput::MidiInput() : is_open_(false), learning_(false) {}
MidiInput::~MidiInput() {}
bool MidiInput::open(int) { return false; }
void MidiInput::close() {}
bool MidiInput::is_open() const { return false; }
std::vector<std::string> MidiInput::get_available_ports() const { return {}; }
void MidiInput::set_callback(MidiCallback) {}
void MidiInput::add_mapping(const MidiMapping&) {}
void MidiInput::remove_mapping(int) {}
void MidiInput::clear_mappings() {}
std::vector<MidiMapping> MidiInput::get_mappings() const { return {}; }
void MidiInput::start_learn(int, int, bool) {}
void MidiInput::stop_learn() {}
bool MidiInput::is_learning() const { return false; }
bool MidiInput::save_mappings(const std::string&) const { return false; }
bool MidiInput::load_mappings(const std::string&) { return false; }
void MidiInput::process_messages() {}

} // namespace amplitron

#endif
