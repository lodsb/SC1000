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

#include <cassert>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <cstdint>

#include "../core/sc_settings.h"

#include "player.h"
#include "track.h"

//
// C++ Member function implementations
//

void player::init(unsigned int sample_rate, struct track* tr, struct sc_settings* settings)
{
	assert(tr != nullptr);
	assert(sample_rate != 0);

	spin_init(&lock);

	sample_dt = 1.0 / sample_rate;
	track = tr;

	// Position state
	pos_state.current = 0.0;
	pos_state.offset = 0.0;
	pos_state.target = 0.0;
	pos_state.last_difference = 0.0;

	// Pitch state
	pitch_state.current = 0.0;
	pitch_state.sync = 1.0;
	pitch_state.note = 1.0;
	pitch_state.fader = 1.0;
	pitch_state.bend = 1.0;
	pitch_state.last_external = 1.0;
	pitch_state.motor_speed = 1.0;

	// Volume state
	volume_state.knob = settings->initial_volume;
	volume_state.fader_target = 0.0;  // Start silent until ADC values are read
	volume_state.fader_current = 0.0;
	volume_state.playback = 0.0;      // Audio engine smoothing state

	// Initialize DeckInput volume from settings
	input.volume_knob = settings->initial_volume;

	platter_state.reset();
	stopped = false;
}

void player::clear()
{
	spin_clear(&lock);
	track_release(track);
}

void player::set_track(struct track* tr)
{
	struct track* x;
	assert(tr != nullptr);
	assert(tr->refcount > 0);
	spin_lock(&lock); /* Synchronise with the playback thread */
	x = track;
	track = tr;
	spin_unlock(&lock);
	track_release(x); /* discard the old track */
}

