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

/*
 * Post: player is initialised
 */

void player_init(struct player *pl, unsigned int sample_rate,
                 struct track *track, struct sc_settings* settings)
{
	assert(track != nullptr);
	assert(sample_rate != 0);

	spin_init(&pl->lock);

	pl->sample_dt = 1.0 / sample_rate;
	pl->track = track;

	pl->position = 0.0;
	pl->offset = 0.0;
	pl->target_position = 0.0;
	pl->last_difference = 0.0;

	pl->pitch = 0.0;
	pl->sync_pitch = 1.0;
	pl->volume = 0.0;
	pl->set_volume = settings->initial_volume;

	pl->note_pitch = 1.0;
	pl->fader_pitch = 1.0;
	pl->bend_pitch = 1.0;
	pl->stopped = false;
	pl->recording = false;
	pl->recording_started = false;
	pl->beep_pos = 0;
	pl->playing_beep = -1;
}

/*
 * Pre: player is initialised
 * Post: no resources are allocated by the player
 */

void player_clear(struct player *pl)
{
	spin_clear(&pl->lock);
	track_release(pl->track);
}

double player_get_elapsed(struct player *pl)
{
	return pl->position - pl->offset;
}

bool player_is_active(const struct player *pl)
{
	return (std::fabs(pl->pitch) > 0.01);
}

/*
 * Cue to the zero position of the track
 */

void player_recue(struct player *pl)
{
	pl->offset = pl->position;
}

/*
 * Set the track used for the playback
 *
 * Pre: caller holds reference on track
 * Post: caller does not hold reference on track
 */

void player_set_track(struct player *pl, struct track *track)
{
	struct track *x;
	assert(track != nullptr);
	assert(track->refcount > 0);
	spin_lock(&pl->lock); /* Synchronise with the playback thread */
	x = pl->track;
	pl->track = track;
	spin_unlock(&pl->lock);
	track_release(x); /* discard the old track */
}

/*
 * Set the playback of one player to match another, used
 * for "instant doubles" and beat juggling
 */

void player_clone(struct player *pl, const struct player *from)
{
	double elapsed;
	struct track *x, *t;

	elapsed = from->position - from->offset;
	pl->offset = pl->position - elapsed;

	t = from->track;
	track_acquire(t);

	spin_lock(&pl->lock);
	x = pl->track;
	pl->track = t;
	spin_unlock(&pl->lock);

	track_release(x);
}

/*
 * Seek to the given position
 */

void player_seek_to(struct player *pl, double seconds)
{
	pl->offset = pl->position - seconds;
}
