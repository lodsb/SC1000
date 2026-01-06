/*
 * Copyright (C) 2024-2026 Niklas Klügel <lodsb@lodsb.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */


// Action dispatch for SC1000 control events
// Maps input events (GPIO, MIDI) to deck operations
#pragma once

#include "../core/sc_input.h"

struct deck;
struct sc1000;
struct sc_settings;

namespace sc {
namespace control {

class InputState;  // Forward declaration

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

} // namespace control
} // namespace sc
