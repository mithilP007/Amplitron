#pragma once
#include <vector>
#include <string>
#include <functional>
#include <map>
#include <mutex>
#include <memory>

#ifdef AMPLITRON_HAS_MIDI
#include <rtmidi/RtMidi.h>
#endif

namespace amplitron {

struct MidiMessage {
    int status;      // 0x80-0xFF
    int data1;       // Note/CC number
    int data2;       // Velocity/value
    double timestamp;
};

struct MidiMapping {
    int cc_number;           // 0-127
    int effect_index;          // Which pedal
    int parameter_index;       // Which knob
    float min_value;           // Parameter min
    float max_value;           // Parameter max
    bool is_toggle;            // True for bypass toggle
};

class MidiInput {
public:
    MidiInput();
    ~MidiInput();

    // Device management
    bool open(int port_index);
    void close();
    bool is_open() const;
    std::vector<std::string> get_available_ports() const;

    // Callback for processed messages
    using MidiCallback = std::function<void(const MidiMessage& msg)>;
    void set_callback(MidiCallback cb);

    // Mapping management
    void add_mapping(const MidiMapping& mapping);
    void remove_mapping(int cc_number);
    void clear_mappings();
    std::vector<MidiMapping> get_mappings() const;
    
    // MIDI learn mode
    void start_learn(int effect_index, int parameter_index, bool is_toggle);
    void stop_learn();
    bool is_learning() const;

    // Save/load mappings
    bool save_mappings(const std::string& filepath) const;
    bool load_mappings(const std::string& filepath);

    // Process pending messages (call from audio thread or main thread)
    void process_messages();

    // Static helpers
    static bool is_cc_message(int status) { return (status & 0xF0) == 0xB0; }
    static bool is_note_on(int status) { return (status & 0xF0) == 0x90; }
    static int get_channel(int status) { return status & 0x0F; }

private:
#ifdef AMPLITRON_HAS_MIDI
    std::unique_ptr<RtMidiIn> midi_in_;
#endif
    bool is_open_;
    MidiCallback callback_;
    
    std::vector<MidiMapping> mappings_;
    mutable std::mutex mappings_mutex_;
    
    // Learn mode
    bool learning_;
    int learn_effect_index_;
    int learn_parameter_index_;
    bool learn_is_toggle_;
    
    // Message queue (lock-free SPSC for audio thread safety)
    static constexpr size_t QUEUE_SIZE = 256;
    MidiMessage message_queue_[QUEUE_SIZE];
    size_t queue_write_idx_;
    size_t queue_read_idx_;
    std::mutex queue_mutex_;
    
    void push_message(const MidiMessage& msg);
    bool pop_message(MidiMessage& msg);
    
#ifndef AMPLITRON_HAS_MIDI
    void midi_callback(double timeStamp, std::vector<unsigned char>* message);
#endif
};

} // namespace amplitron
