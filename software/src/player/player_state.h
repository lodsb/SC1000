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

#pragma once

//
// Player state substates - grouped for clarity and maintainability
//
// These structs organize the ~30 fields in player into logical groups.
// Each group has a reset() method for centralized state initialization.
//

// Playback mode state machine
// Replaces boolean soup (stopped, cap_touch combinations)
enum class PlaybackMode {
    STOPPED,      // Motor braking toward 0, position frozen
    PLAYING,      // Motor at speed, slipmat simulation when platter released
    SCRATCHING,   // Position-locked to platter input
};

// Position tracking state
struct PositionState {
    double current = 0;         // Actual playback position (seconds)
    double target = 0;          // Where platter says we should be (seconds)
    double offset = 0;          // Track start point offset
    double last_difference = 0; // Last known current - target (for sync)

    void reset() {
        current = 0;
        target = 0;
        offset = 0;
        last_difference = 0;
    }
};

// Pitch control state
// All pitch factors are multiplicative: final = base * fader * note * bend
struct PitchState {
    double current = 0;         // Final pitch after smoothing (used by audio engine)
    double sync = 1.0;          // Pitch required to sync to timecode signal
    double motor_speed = 1.0;   // Virtual motor speed (brakes when stopped)
    double fader = 1.0;         // From hardware/MIDI pitch fader
    double note = 1.0;          // From MIDI note (equal temperament transposition)
    double bend = 1.0;          // From MIDI pitch bend
    double last_external = 1.0; // Previous external speed for change detection

    // Combined external pitch (everything except motor/sync)
    double external_speed() const {
        return note * fader * bend;
    }

    void reset() {
        current = 0;
        sync = 1.0;
        motor_speed = 1.0;
        fader = 1.0;
        note = 1.0;
        bend = 1.0;
        last_external = 1.0;
    }

    void reset_external() {
        fader = 1.0;
        note = 1.0;
        bend = 1.0;
    }
};

// Volume control state
struct VolumeState {
    double set = 1.0;           // Target volume from knobs/buttons/MIDI
    double fader_target = 1.0;  // Crossfader target (after cut logic)
    double fader_current = 1.0; // Crossfader after smoothing

    void reset() {
        set = 1.0;
        fader_target = 1.0;
        fader_current = 1.0;
    }
};

// Platter/touch sensor state
struct PlatterState {
    bool touched = false;       // Current touch state
    bool touched_prev = false;  // Previous frame touch state

    bool just_touched() const { return touched && !touched_prev; }
    bool just_released() const { return !touched && touched_prev; }

    void update() { touched_prev = touched; }

    void reset() {
        touched = false;
        touched_prev = false;
    }
};

// Recording state
struct RecordingState {
    bool requested = false;     // User toggled record button
    bool active = false;        // Actually recording audio
    bool use_loop = false;      // Playback from loop buffer instead of file

    void reset() {
        requested = false;
        active = false;
        // Note: use_loop is intentionally NOT reset here
        // It's controlled by navigation (goto_loop, load_track, etc.)
    }

    void reset_all() {
        requested = false;
        active = false;
        use_loop = false;
    }
};

// Audio feedback state (beeps for user feedback)
struct FeedbackState {
    static constexpr int NONE = -1;
    static constexpr int RECORDING_START = 0;
    static constexpr int RECORDING_STOP = 1;
    static constexpr int RECORDING_ERROR = 2;

    int beep_type = NONE;
    unsigned long beep_position = 0;

    void play(int type) {
        beep_type = type;
        beep_position = 0;
    }

    void stop() {
        beep_type = NONE;
        beep_position = 0;
    }

    bool is_playing() const { return beep_type != NONE; }
};
