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

#ifndef ALSA_H
#define ALSA_H

#include "../core/sc1000.h"
#include <stdbool.h>

struct track;

int alsa_init( struct sc1000* sc1000_engine, struct sc_settings* settings);

void alsa_clear_config_cache(void);

// Loop recording control (deck_no: 0=beat, 1=scratch)
bool alsa_start_recording(struct sc1000* engine, int deck_no);
void alsa_stop_recording(struct sc1000* engine, int deck_no);
bool alsa_is_recording(struct sc1000* engine, int deck_no);
struct track* alsa_get_loop_track(struct sc1000* engine, int deck_no);
struct track* alsa_peek_loop_track(struct sc1000* engine, int deck_no);  // RT-safe: no ref count change
bool alsa_has_capture(struct sc1000* engine);
bool alsa_has_loop(struct sc1000* engine, int deck_no);
void alsa_reset_loop(struct sc1000* engine, int deck_no);

#endif
