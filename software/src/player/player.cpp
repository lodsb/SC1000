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

	position = 0.0;
	offset = 0.0;
	target_position = 0.0;
	last_difference = 0.0;

	pitch = 0.0;
	sync_pitch = 1.0;
	volume = 0.0;
	set_volume = settings->initial_volume;

	note_pitch = 1.0;
	fader_pitch = 1.0;
	bend_pitch = 1.0;
	fader_target = 0.0;  // Start silent until ADC values are read
	fader_volume = 0.0;
	stopped = false;
	recording = false;
	recording_started = false;
	use_loop = false;
	beep_pos = 0;
	playing_beep = -1;
}

void player::clear()
{
	spin_clear(&lock);
	track_release(track);
}

double player::get_elapsed() const
{
	return position - offset;
}

bool player::is_active() const
{
	return (std::fabs(pitch) > 0.01);
}

void player::recue()
{
	offset = position;
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

void player::clone(const player& from)
{
	double elapsed;
	struct track* x;
	struct track* t;

	elapsed = from.position - from.offset;
	offset = position - elapsed;

	t = from.track;
	track_acquire(t);

	spin_lock(&lock);
	x = track;
	track = t;
	spin_unlock(&lock);

	track_release(x);
}

void player::seek_to(double seconds)
{
	offset = position - seconds;
}

//
// Legacy C API wrappers
//

void player_init(struct player* pl, unsigned int sample_rate,
                 struct track* track, struct sc_settings* settings)
{
	pl->init(sample_rate, track, settings);
}

void player_clear(struct player* pl)
{
	pl->clear();
}

double player_get_elapsed(struct player* pl)
{
	return pl->get_elapsed();
}

bool player_is_active(const struct player* pl)
{
	return pl->is_active();
}

void player_recue(struct player* pl)
{
	pl->recue();
}

void player_set_track(struct player* pl, struct track* track)
{
	pl->set_track(track);
}

void player_clone(struct player* pl, const struct player* from)
{
	pl->clone(*from);
}

void player_seek_to(struct player* pl, double seconds)
{
	pl->seek_to(seconds);
}
