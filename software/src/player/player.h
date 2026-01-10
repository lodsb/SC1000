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


#define PLAYER_CHANNELS 2

// How many samples each beep stage lasts
#define BEEPSPEED 4800

#define BEEP_NONE -1
#define BEEP_RECORDINGSTART 0
#define BEEP_RECORDINGSTOP 1
#define BEEP_RECORDINGERROR 2

struct track;
struct sc_settings;

struct player
{
   double sample_dt;

   spin lock;
   struct track* track;

   /* Current playback parameters */

   double position, /* seconds */
   target_position, /* seconds, or TARGET_UNKNOWN */
   offset, /* track start point in timecode */
   last_difference, /* last known position minus target_position */
   pitch, /* from timecoder */
   sync_pitch, /* pitch required to sync to timecode signal */
   volume,
           note_pitch, // Pitch after note change
   fader_pitch, // pitch after fader change
   bend_pitch, // pitch after semitone bend change
   motor_speed, // speed of virtual motor, usually same as nominal_pitch but affected by start/stop
   last_external_speed; // Previous external speed for change detection (instant MIDI response)


   bool just_play;
   double fader_target; // Player should slowly fade to this level
   double fader_volume; // current fader volume
   double set_volume; // volume set by the volume controls on the back of the sc1000 or the volume buttons on the sc500 (or whatever over midi)
   bool cap_touch;
   bool cap_touch_old;
   bool stopped;

   bool recording;
   bool recording_started;
   bool use_loop;           // When true, audio reads from deck's loop_track instead

   int playing_beep;
   unsigned long beep_pos;

#ifdef __cplusplus
   // C++ member functions
   void init(unsigned int sample_rate, struct track* track, struct sc_settings* settings);
   void clear();
   void set_track(struct track* track);
   void clone(const player& from);
   double get_elapsed() const;
   bool is_active() const;
   void seek_to(double seconds);
   void recue();
#endif
};

