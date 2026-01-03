// Action dispatch for SC1000 control events
// Maps input events (GPIO, MIDI) to deck operations
#pragma once

#include "../core/sc_control_mapping.h"

struct deck;
struct sc1000;
struct sc_settings;

namespace sc {
namespace control {

// Global shift state (for shifted button combos)
extern bool shifted;

// Pitch mode: 0=off, 1=beat deck, 2=scratch deck
extern int pitch_mode;

// Execute an action on a specific deck
void perform_action_for_deck(struct deck* d, struct mapping* map,
                             const unsigned char midi_buffer[3],
                             struct sc_settings* settings);

// Dispatch an input event to the appropriate deck
void dispatch_event(struct mapping* map, unsigned char midi_buffer[3],
                    struct sc1000* engine, struct sc_settings* settings);

// Find a mapping for a MIDI event
struct mapping* find_midi_mapping(struct mapping* maps,
                                  unsigned char buf[3],
                                  enum EventType edge);

// Find a mapping for a GPIO event
struct mapping* find_io_mapping(struct mapping* mappings,
                                unsigned char port,
                                unsigned char pin,
                                enum EventType edge);

} // namespace control
} // namespace sc

// C-compatible wrappers (for use from C code)
#ifdef __cplusplus
extern "C" {
#endif

void io_event(struct mapping* map, unsigned char midi_buffer[3],
              struct sc1000* sc1000_engine, struct sc_settings* settings);

struct mapping* find_midi_mapping_c(struct mapping* maps,
                                    unsigned char buf[3],
                                    enum EventType edge);

struct mapping* find_io_mapping_c(struct mapping* mappings,
                                  unsigned char port,
                                  unsigned char pin,
                                  enum EventType edge);

#ifdef __cplusplus
}
#endif
