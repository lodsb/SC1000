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

#include <cmath>
#include <cstdint>
#include <memory>

#include "cues.h"
#include "deck_state.h"
#include "player.h"

#ifdef __cplusplus
#include "playlist.h"
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
   std::string importer;
   bool protect;

   struct player player;
   Cues cues;

   /* Punch */
   double punch;

   /* A controller adds itself here */
   size_t ncontrol;
   Controller* control[4];

   // If a shift modifier has been pressed recently
   bool shifted;

   // === Grouped state (new structure) ===
   NavigationState nav_state;
   EncoderState encoder_state;
   LoopState loop_state;

   // Playlist (owned)
   std::unique_ptr<Playlist> playlist;
   int deck_no;  // 0 = beat, 1 = scratch

   // === Legacy field aliases (for gradual migration) ===
   size_t& current_folder_idx = nav_state.folder_idx;
   int& current_file_idx = nav_state.file_idx;
   bool& files_present = nav_state.files_present;

   int32_t& angle_offset = encoder_state.offset;
   int& encoder_angle = encoder_state.angle;
   int& new_encoder_angle = encoder_state.angle_raw;

   struct track*& loop_track = loop_state.track;

#ifdef __cplusplus
   // C++ member functions
   ~deck();  // Destructor defined in .cpp where Playlist is complete
   int init(struct sc_settings* settings);
   void clear();
   bool is_locked() const;
   void recue();
   void clone(const deck& from);
   void unset_cue(unsigned int label);
   void cue(unsigned int label);
   void punch_in(unsigned int label);
   void punch_out();
   void load_folder(const char* folder_name);
   void next_file(struct sc1000* engine, struct sc_settings* settings);
   void prev_file(struct sc1000* engine, struct sc_settings* settings);
   void next_folder(struct sc1000* engine, struct sc_settings* settings);
   void prev_folder(struct sc1000* engine, struct sc_settings* settings);
   void random_file(struct sc1000* engine, struct sc_settings* settings);
   void record();
   bool recall_loop(struct sc_settings* settings);
   bool has_loop() const;

   // Loop navigation helpers
   bool is_at_loop() const { return nav_state.is_at_loop(); }
   void goto_loop(struct sc1000* engine, struct sc_settings* settings);
#endif
};


