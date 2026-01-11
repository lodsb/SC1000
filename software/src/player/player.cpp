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

	// Initialize input state
	input = sc::DeckInput{};  // Reset to defaults
	input.volume_knob = settings->initial_volume;

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

