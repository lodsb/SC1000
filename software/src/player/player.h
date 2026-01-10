/*
 * Copyright (C) 2018 Mark Hills <mark@xwax.org>
 * Copyright (C) 2019 Andrew Tait <rasteri@gmail.com>
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

#include <cstdio>
#include <cstdlib>

#include "../thread/spin.h"
#include "player_state.h"

#define PLAYER_CHANNELS 2

// How many samples each beep stage lasts
#define BEEPSPEED 4800

// Legacy beep constants (use FeedbackState::* in new code)
#define BEEP_NONE -1
#define BEEP_RECORDINGSTART 0
#define BEEP_RECORDINGSTOP 1
#define BEEP_RECORDINGERROR 2

struct track;
struct sc_settings;

struct player
{
    // Timing
    double sample_dt;

    // Thread synchronization
    spin lock;

    // Current track
    struct track* track;

    // === Grouped state (new structure) ===
    PositionState pos_state;
    PitchState pitch_state;
    VolumeState volume_state;
    PlatterState platter_state;
    RecordingState recording_state;
    FeedbackState feedback_state;

    // Playback mode
    PlaybackMode mode = PlaybackMode::STOPPED;
    bool just_play = false;     // Beat deck mode (no scratch control)

    // === Legacy field aliases (for gradual migration) ===
    // These will be removed once all code uses the new structure
    double& position = pos_state.current;
    double& target_position = pos_state.target;
    double& offset = pos_state.offset;
    double& last_difference = pos_state.last_difference;

    double& pitch = pitch_state.current;
    double& sync_pitch = pitch_state.sync;
    double& fader_pitch = pitch_state.fader;
    double& note_pitch = pitch_state.note;
    double& bend_pitch = pitch_state.bend;
    double& motor_speed = pitch_state.motor_speed;
    double& last_external_speed = pitch_state.last_external;

    double& volume = volume_state.set;
    double& set_volume = volume_state.set;
    double& fader_target = volume_state.fader_target;
    double& fader_volume = volume_state.fader_current;

    bool& cap_touch = platter_state.touched;
    bool& cap_touch_old = platter_state.touched_prev;

    bool& recording = recording_state.active;
    bool& recording_started = recording_state.requested;
    bool& recording_active = recording_state.active;
    bool& use_loop = recording_state.use_loop;

    int& playing_beep = feedback_state.beep_type;
    unsigned long& beep_pos = feedback_state.beep_position;

    // stopped is now derived from mode, but keep for compatibility
    bool stopped = false;

    // C++ member functions
    void init(unsigned int sample_rate, struct track* track, struct sc_settings* settings);
    void clear();
    void set_track(struct track* track);
    void clone(const player& from);
    double get_elapsed() const;
    bool is_active() const;
    void seek_to(double seconds);
    void recue();

    // New: centralized state reset for track loading
    void reset_for_track_load();
};

