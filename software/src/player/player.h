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
#include "deck_input.h"

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

    // === NEW: Unified input state ===
    // All input fields in one place. Audio engine reads this.
    sc::DeckInput input;

    // === LEGACY: Old grouped state (being migrated to DeckInput) ===
    // These will be removed once migration is complete.
    PositionState pos_state;
    PitchState pitch_state;
    VolumeState volume_state;
    PlatterState platter_state;

    // Playback mode
    PlaybackMode mode = PlaybackMode::STOPPED;
    bool just_play = false;     // Beat deck mode (no scratch control)
    bool stopped = false;       // Motor stopped (braking)

    // C++ member functions
    void init(unsigned int sample_rate, struct track* track, struct sc_settings* settings);
    void clear();
    void set_track(struct track* track);

    // Centralized state reset for track loading
    void reset_for_track_load();
};

