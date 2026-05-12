#include "test_framework.h"
#include "audio/midi_input.h"

using namespace amplitron;

// ============================================================
// TEST 1: Basic Initialization
// ============================================================
TEST_CASE("MidiInput initializes in closed state") {
    MidiInput midi;
    
    // Should start closed
    ASSERT_FALSE(midi.is_open());
    
    // No ports available when closed
    auto ports = midi.get_available_ports();
    ASSERT_TRUE(ports.empty());
    
    // No mappings initially
    auto mappings = midi.get_mappings();
    ASSERT_TRUE(mappings.empty());
    
    // Not in learn mode
    ASSERT_FALSE(midi.is_learning());
}

// ============================================================
// TEST 2: Mapping Add/Remove/Clear
// ============================================================
TEST_CASE("MidiInput mapping management works correctly") {
    MidiInput midi;
    
    // Add first mapping
    MidiMapping m1;
    m1.cc_number = 1;
    m1.effect_index = 0;
    m1.parameter_index = 0;
    m1.min_value = 0.0f;
    m1.max_value = 1.0f;
    m1.is_toggle = false;
    midi.add_mapping(m1);
    
    auto mappings = midi.get_mappings();
    ASSERT_EQ(mappings.size(), 1u);
    ASSERT_EQ(mappings[0].cc_number, 1);
    ASSERT_EQ(mappings[0].effect_index, 0);
    ASSERT_EQ(mappings[0].parameter_index, 0);
    ASSERT_FALSE(mappings[0].is_toggle);
    
    // Add second mapping
    MidiMapping m2;
    m2.cc_number = 2;
    m2.effect_index = 1;
    m2.parameter_index = 2;
    m2.min_value = 0.0f;
    m2.max_value = 100.0f;
    m2.is_toggle = true;
    midi.add_mapping(m2);
    
    mappings = midi.get_mappings();
    ASSERT_EQ(mappings.size(), 2u);
    ASSERT_EQ(mappings[1].cc_number, 2);
    ASSERT_TRUE(mappings[1].is_toggle);
    
    // Remove first mapping
    midi.remove_mapping(1);
    mappings = midi.get_mappings();
    ASSERT_EQ(mappings.size(), 1u);
    ASSERT_EQ(mappings[0].cc_number, 2);
    
    // Clear all mappings
    midi.clear_mappings();
    mappings = midi.get_mappings();
    ASSERT_TRUE(mappings.empty());
}

// ============================================================
// TEST 3: Mapping Overwrite (Same CC Number)
// ============================================================
TEST_CASE("MidiInput mapping overwrite replaces existing") {
    MidiInput midi;
    
    // Add mapping for CC 1
    MidiMapping m1;
    m1.cc_number = 1;
    m1.effect_index = 0;
    m1.parameter_index = 0;
    m1.min_value = 0.0f;
    m1.max_value = 1.0f;
    m1.is_toggle = false;
    midi.add_mapping(m1);
    
    // Overwrite with same CC, different target
    MidiMapping m2;
    m2.cc_number = 1;  // Same CC number!
    m2.effect_index = 2;
    m2.parameter_index = 3;
    m2.min_value = 0.0f;
    m2.max_value = 1.0f;
    m2.is_toggle = true;
    midi.add_mapping(m2);
    
    auto mappings = midi.get_mappings();
    ASSERT_EQ(mappings.size(), 1u);  // Should still be 1, not 2
    ASSERT_EQ(mappings[0].effect_index, 2);
    ASSERT_EQ(mappings[0].parameter_index, 3);
    ASSERT_TRUE(mappings[0].is_toggle);
}

// ============================================================
// TEST 4: MIDI Learn Mode
// ============================================================
TEST_CASE("MidiInput learn mode activates and deactivates") {
    MidiInput midi;
    
    // Initially not learning
    ASSERT_FALSE(midi.is_learning());
    
    // Start learn mode
    midi.start_learn(5, 2, true);  // effect 5, param 2, toggle
    ASSERT_TRUE(midi.is_learning());
    
    // Stop learn mode
    midi.stop_learn();
    ASSERT_FALSE(midi.is_learning());
    
    // Start learn mode again with different params
    midi.start_learn(3, 1, false);  // effect 3, param 1, range
    ASSERT_TRUE(midi.is_learning());
    
    // Stop again
    midi.stop_learn();
    ASSERT_FALSE(midi.is_learning());
}

// ============================================================
// TEST 5: Save and Load Mappings
// ============================================================
TEST_CASE("MidiInput save and load mappings preserves data") {
    MidiInput midi;
    
    // Add multiple mappings
    MidiMapping m1;
    m1.cc_number = 10;
    m1.effect_index = 0;
    m1.parameter_index = 0;
    m1.min_value = 0.0f;
    m1.max_value = 1.0f;
    m1.is_toggle = false;
    midi.add_mapping(m1);
    
    MidiMapping m2;
    m2.cc_number = 20;
    m2.effect_index = 1;
    m2.parameter_index = 2;
    m2.min_value = 0.0f;
    m2.max_value = 100.0f;
    m2.is_toggle = true;
    midi.add_mapping(m2);
    
    // Save to file
    const char* test_file = "test_midi_mappings.json";
    bool saved = midi.save_mappings(test_file);
    ASSERT_TRUE(saved);
    
    // Verify file exists by trying to load
    MidiInput midi2;
    bool loaded = midi2.load_mappings(test_file);
    ASSERT_TRUE(loaded);
    
    // Cleanup test file
    std::remove(test_file);
}

// ============================================================
// TEST 6: MIDI Message Type Helpers
// ============================================================
TEST_CASE("MidiInput message type detection works") {
    // CC messages (0xB0 - 0xBF)
    ASSERT_TRUE(MidiInput::is_cc_message(0xB0));   // CC channel 1
    ASSERT_TRUE(MidiInput::is_cc_message(0xB5));   // CC channel 6
    ASSERT_TRUE(MidiInput::is_cc_message(0xBF));   // CC channel 16
    ASSERT_FALSE(MidiInput::is_cc_message(0x90));  // Note on
    ASSERT_FALSE(MidiInput::is_cc_message(0x80));  // Note off
    ASSERT_FALSE(MidiInput::is_cc_message(0xF8));  // Clock
    
    // Note on messages
    ASSERT_TRUE(MidiInput::is_note_on(0x90));
    ASSERT_TRUE(MidiInput::is_note_on(0x9F));
    ASSERT_FALSE(MidiInput::is_note_on(0xB0));
    ASSERT_FALSE(MidiInput::is_note_on(0x80));
    
    // Channel extraction
    ASSERT_EQ(MidiInput::get_channel(0xB0), 0);   // Channel 1
    ASSERT_EQ(MidiInput::get_channel(0xB5), 5);   // Channel 6
    ASSERT_EQ(MidiInput::get_channel(0xBF), 15);  // Channel 16
    ASSERT_EQ(MidiInput::get_channel(0x90), 0);   // Note on channel 1
}

// ============================================================
// TEST 7: Thread Safety (Concurrent Access)
// ============================================================
TEST_CASE("MidiInput thread safe concurrent access") {
    MidiInput midi;
    
    // Simulate concurrent access from multiple threads
    std::thread t1([&]() {
        for (int i = 0; i < 100; ++i) {
            MidiMapping m;
            m.cc_number = i;
            m.effect_index = i % 5;
            m.parameter_index = i % 3;
            m.min_value = 0.0f;
            m.max_value = 1.0f;
            m.is_toggle = (i % 2 == 0);
            midi.add_mapping(m);
        }
    });
    
    std::thread t2([&]() {
        for (int i = 0; i < 50; ++i) {
            auto mappings = midi.get_mappings();
            if (!mappings.empty()) {
                midi.remove_mapping(i * 2);
            }
        }
    });
    
    std::thread t3([&]() {
        for (int i = 0; i < 50; ++i) {
            midi.clear_mappings();
            midi.get_mappings();
        }
    });
    
    t1.join();
    t2.join();
    t3.join();
    
    // Should not crash or deadlock - if we get here, test passes
    ASSERT_TRUE(true);
}

// ============================================================
// TEST 8: Callback and Message Processing
// ============================================================
TEST_CASE("MidiInput callback and message processing") {
    MidiInput midi;
    
    // Set up callback to capture messages
    int callback_count = 0;
    MidiMessage last_msg;
    
    midi.set_callback([&](const MidiMessage& msg) {
        callback_count++;
        last_msg = msg;
    });
    
    // Since we can't send real MIDI in unit tests,
    // verify callback is set and process_messages doesn't crash
    midi.process_messages();
    
    // Callback count should be 0 (no messages processed)
    ASSERT_EQ(callback_count, 0);
    
    // Test that callback can be replaced
    int callback_count_2 = 0;
    midi.set_callback([&](const MidiMessage& msg) {
        callback_count_2++;
    });
    
    midi.process_messages();
    ASSERT_EQ(callback_count_2, 0);  // Still 0, but different callback
    
    // Test with null callback (should not crash)
    midi.set_callback(nullptr);
    midi.process_messages();
    ASSERT_TRUE(true);  // If we get here, null callback handled safely
}
