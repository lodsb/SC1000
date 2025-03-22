/*
 * Copyright (C) 2018 Mark Hills <mark@xwax.org>
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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "../thread/spin.h"

#include "track.h"
#include "settings.h"


#define PLAYER_CHANNELS 2

// How many samples each beep stage lasts
#define BEEPSPEED 4800

#define BEEP_NONE -1
#define BEEP_RECORDINGSTART 0
#define BEEP_RECORDINGSTOP 1
#define BEEP_RECORDINGERROR 2


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
   motor_speed; // speed of virtual motor, usually same as nominal_pitch but affected by start/stop

   /* Timecode control */

   bool timecode_control,
           recalibrate; /* re-sync offset at next opportunity */
   bool just_play;
   double fader_target; // Player should slowly fade to this level
   double fader_volume; // current fader volume
   double set_volume; // volume set by the volume controls on the back of the sc1000 or the volume buttons on the sc500 (or whatever over midi)
   bool cap_touch;
   bool cap_touch_old;
   bool stopped;

   bool recording;
   bool recording_started;

   int playing_beep;
   unsigned long beep_pos;

   FILE* recording_file;
   char recording_file_name[256];
};

void player_init( struct player* pl, unsigned int sample_rate,
                  struct track* track, struct sc_settings* settings);

void player_clear( struct player* pl );

void player_set_internal_playback( struct player* pl );

void player_set_track( struct player* pl, struct track* track );

void player_clone( struct player* pl, const struct player* from );

double player_get_position( struct player* pl );

double player_get_elapsed( struct player* pl );

double player_get_remain( struct player* pl );

bool player_is_active( const struct player* pl );

void player_seek_to( struct player* pl, double seconds );

void player_recue( struct player* pl );

void player_collect_add( struct player *pl1, struct player *pl2, signed short *pcm, unsigned long samples, struct sc_settings* settings );

