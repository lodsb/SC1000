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

#include <math.h>

#include "cues.h"
#include "player.h"

#ifdef __cplusplus
class Playlist;
#else
struct Playlist;  // Forward declare as struct for C
typedef struct Playlist Playlist;
#endif

#ifdef __cplusplus
class Controller;
#else
typedef struct Controller Controller;
#endif
struct sc_settings;


#define NO_PUNCH (HUGE_VAL)

struct track;  // Forward declaration

struct deck
{
   const char* importer;
   bool protect;

   struct player player;
   struct cues cues;

   /* Punch */

   double punch;

   /* A controller adds itself here */

   size_t ncontrol;
   Controller* control[4];

   // If a shift modifier has been pressed recently
   bool shifted;

   // Playlist navigation (index-based for O(1) access)
   Playlist* playlist;
   size_t current_folder_idx;
   size_t current_file_idx;
   bool files_present;

   int32_t angle_offset; // Offset between encoder angle and track position, reset every time the platter is touched
   int encoder_angle, new_encoder_angle;

   // Loop recording - persisted loop track for recall
   struct track* loop_track;
};


#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

EXTERNC int deck_init( struct deck* deck,
               struct sc_settings* settings);

EXTERNC void deck_clear( struct deck* deck );

EXTERNC bool deck_is_locked( const struct deck* deck );

EXTERNC void deck_recue( struct deck* deck );

EXTERNC void deck_clone( struct deck* deck, const struct deck* from );

EXTERNC void deck_unset_cue( struct deck* deck, unsigned int label );

EXTERNC void deck_cue( struct deck* deck, unsigned int label );

EXTERNC void deck_punch_in( struct deck* d, unsigned int label );

EXTERNC void deck_punch_out( struct deck* d );

EXTERNC void deck_load_folder( struct deck* d, char* folder_name );

EXTERNC void deck_next_file( struct deck* d, struct sc_settings* settings );

EXTERNC void deck_prev_file( struct deck* d, struct sc_settings* settings );

EXTERNC void deck_next_folder( struct deck* d, struct sc_settings* settings );

EXTERNC void deck_prev_folder( struct deck* d, struct sc_settings* settings );

EXTERNC void deck_random_file( struct deck* d, struct sc_settings* settings );

EXTERNC void deck_record( struct deck* d );

// Recall the last recorded loop track
EXTERNC bool deck_recall_loop( struct deck* d, struct sc_settings* settings );

// Check if a loop track is available
EXTERNC bool deck_has_loop( const struct deck* d );

#undef EXTERNC

