// Action dispatch for SC1000 control events
// Maps input events (GPIO, MIDI) to deck operations
#pragma once

#include "../core/sc_input.h"
#include <vector>

struct deck;
struct sc1000;
struct sc_settings;

namespace sc {
namespace control {

// Action state - encapsulates globals for shift key and pitch mode
// These are inherently global (represent hardware input state)
// Collected here for clarity and potential future thread-safety improvements
struct ActionState {
    static bool shifted;      // Shift key pressed
    static int pitch_mode;    // 0=off, 1=beat deck, 2=scratch deck
};

// Backward-compatible aliases
inline bool& shifted = ActionState::shifted;
inline int& pitch_mode = ActionState::pitch_mode;

// Execute an action on a specific deck
void perform_action_for_deck(deck* d, mapping* map,
                             const unsigned char midi_buffer[3],
                             sc1000* engine, sc_settings* settings);

// Dispatch an input event to the appropriate deck
void dispatch_event(mapping* map, unsigned char midi_buffer[3],
                    sc1000* engine, sc_settings* settings);

// Find a mapping for a MIDI event
// Returns pointer to mapping in vector, or nullptr if not found
mapping* find_midi_mapping(std::vector<mapping>& maps,
                           unsigned char buf[3],
                           EventType edge);

// Find a mapping for a GPIO event
// Returns pointer to mapping in vector, or nullptr if not found
mapping* find_io_mapping(std::vector<mapping>& mappings,
                         unsigned char port,
                         unsigned char pin,
                         EventType edge);

} // namespace control
} // namespace sc
