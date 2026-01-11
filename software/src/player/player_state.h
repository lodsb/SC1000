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
// These structs organize player state into logical groups with clear ownership:
//
// OWNERSHIP PATTERN:
//   - "Input" fields: Written by input thread, read by audio engine
//   - "Synced from audio engine" fields: Owned by DeckProcessingState in audio engine,
//     synced back to player after each buffer for external code (display, cues, etc.)
//
// The audio engine (see src/engine/deck_processing_state.h) maintains its own
// copy of processing state to avoid race conditions. It syncs to/from player
// at buffer boundaries.
//
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
    // === Synced from audio engine (read-only for external code) ===
    double current = 0;         // Actual playback position (seconds)

    // === Input (written by input thread, read by audio engine) ===
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
    // === Synced from audio engine (read-only for external code) ===
    double current = 0;         // Final pitch after smoothing
    double motor_speed = 1.0;   // Virtual motor speed (brakes when stopped)

    // === Input (written by input thread, read by audio engine) ===
    double sync = 1.0;          // Pitch required to sync to timecode signal
    double fader = 1.0;         // From hardware/MIDI pitch fader
    double note = 1.0;          // From MIDI note (equal temperament transposition)
    double bend = 1.0;          // From MIDI pitch bend

    // === Audio engine internal (synced internally, not used externally) ===
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
// Separated into input targets (written by input thread) and
// processing state (synced from audio engine) to avoid race conditions.
struct VolumeState {
    // === Input (written by input thread, read by audio engine) ===
    double knob = 1.0;          // Volume pot ADC value or MIDI CC (0-1)
    double fader_target = 1.0;  // Crossfader position after cut logic (0-1)

    // === Synced from audio engine (read-only for external code) ===
    double fader_current = 1.0; // Crossfader after smoothing (approaches fader_target)
    double playback = 0.0;      // Current playback volume (for inter-buffer smoothing)

    void reset() {
        knob = 1.0;
        fader_target = 1.0;
        fader_current = 1.0;
        playback = 0.0;
    }
};

// Platter/touch sensor state
struct PlatterState {
    // === Input (written by input thread, read by audio engine) ===
    bool touched = false;       // Current touch state

    // === Synced from audio engine (read-only for external code, can be reset externally) ===
    bool touched_prev = false;  // Previous frame touch state (reset on track load)

    bool just_touched() const { return touched && !touched_prev; }
    bool just_released() const { return !touched && touched_prev; }

    // Note: update() is now handled by audio engine, kept for compatibility
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
