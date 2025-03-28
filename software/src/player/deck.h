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

#ifndef DECK_H
#define DECK_H

#include <math.h>

#include "cues.h"
#include "player.h"

struct sc_folder;
struct sc_file;
struct controller;
struct sc_settings;


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
   struct controller* control[4];

   // If a shift modifier has been pressed recently
   bool shifted;

   struct sc_folder* first_folder, * current_folder;
   struct sc_file* current_file;
   unsigned int num_files;
   bool files_present;

   int32_t angle_offset; // Offset between encoder angle and track position, reset every time the platter is touched
   int encoder_angle, new_encoder_angle;
};

int deck_init( struct deck* deck,
               struct sc_settings* settings);

void deck_clear( struct deck* deck );

bool deck_is_locked( const struct deck* deck );

void deck_recue( struct deck* deck );

void deck_clone( struct deck* deck, const struct deck* from );

void deck_unset_cue( struct deck* deck, unsigned int label );

void deck_cue( struct deck* deck, unsigned int label );

void deck_punch_in( struct deck* d, unsigned int label );

void deck_punch_out( struct deck* d );

void deck_load_folder( struct deck* d, char* FolderName );

void deck_next_file( struct deck* d, struct sc_settings* settings );

void deck_prev_file( struct deck* d, struct sc_settings* settings );

void deck_next_folder( struct deck* d, struct sc_settings* settings );

void deck_prev_folder( struct deck* d, struct sc_settings* settings );

void deck_random_file( struct deck* d, struct sc_settings* settings );

void deck_record( struct deck* d );

#endif
