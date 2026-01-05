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

#include <cmath>
#include <cstdint>

#include "cues.h"
#include "player.h"

#ifdef __cplusplus
class Playlist;
class Controller;
#else
struct Playlist;
typedef struct Playlist Playlist;
struct Controller;
typedef struct Controller Controller;
#endif

struct sc_settings;
struct track;
struct sc1000;

#define NO_PUNCH (HUGE_VAL)

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
   // current_file_idx: -1 = loop track (position 0), 0+ = file tracks (position 1+)
   Playlist* playlist;
   size_t current_folder_idx;
   int current_file_idx;
   bool files_present;
   int deck_no;  // 0 = beat, 1 = scratch

   int32_t angle_offset; // Offset between encoder angle and track position, reset every time the platter is touched
   int encoder_angle, new_encoder_angle;

   // Loop recording - persisted loop track for recall
   struct track* loop_track;

#ifdef __cplusplus
   // C++ member functions
   int init(struct sc_settings* settings);
   void clear();
   bool is_locked() const;
   void recue();
   void clone(const deck& from);
   void unset_cue(unsigned int label);
   void cue(unsigned int label);
   void punch_in(unsigned int label);
   void punch_out();
   void load_folder(char* folder_name);
   void next_file(struct sc1000* engine, struct sc_settings* settings);
   void prev_file(struct sc1000* engine, struct sc_settings* settings);
   void next_folder(struct sc1000* engine, struct sc_settings* settings);
   void prev_folder(struct sc1000* engine, struct sc_settings* settings);
   void random_file(struct sc1000* engine, struct sc_settings* settings);
   void record();
   bool recall_loop(struct sc_settings* settings);
   bool has_loop() const;

   // Loop navigation helpers
   bool is_at_loop() const { return current_file_idx == -1; }
   void goto_loop(struct sc1000* engine, struct sc_settings* settings);
#endif
};


