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

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#include "../app/sc_settings.h"

#include "player.h"
#include "track.h"

/* Bend playback speed to compensate for the difference between our
 * current position and that given by the timecode */

#define SYNC_TIME (1.0 / 2) /* time taken to reach sync */
#define SYNC_PITCH 0.05		/* don't sync at low pitches */
#define SYNC_RC 0.05		/* filter to 1.0 when no timecodes available */

/* If the difference between our current position and that given by
 * the timecode is greater than this value, recover by jumping
 * straight to the position given by the timecode. */

#define SKIP_THRESHOLD (1.0 / 8) /* before dropping audio */

#define SQ(x) ((x) * (x))
#define TARGET_UNKNOWN INFINITY

/*
 * Post: player is initialised
 */

void player_init(struct player *pl, unsigned int sample_rate,
				     struct track *track, struct sc_settings* settings)
{
	assert(track != NULL);
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
	pl->stopped = 0;
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

double player_get_position(struct player *pl)
{
	return pl->position;
}

double player_get_elapsed(struct player *pl)
{
	return pl->position - pl->offset;
}

double player_get_remain(struct player *pl)
{
	return (double)pl->track->length / pl->track->rate + pl->offset - pl->position;
}

bool player_is_active(const struct player *pl)
{
	return (fabs(pl->pitch) > 0.01);
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
	assert(track != NULL);
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
 * Synchronise to the position and speed given by the timecoder
 *
 * Return: 0 on success or -1 if the timecoder is not currently valid
 */


/*
 * Seek to the given position
 */

void player_seek_to(struct player *pl, double seconds)
{
	pl->offset = pl->position - seconds;
	printf("Seek'n %f %f %f\n", seconds, pl->position, pl->offset);
}

unsigned long samplesSoFar = 0;

/*
 * Get a block of PCM audio data to send to the soundcard
 *
 * This is the main function which retrieves audio for playback.  The
 * clock of playback is decoupled from the clock of the timecode
 * signal.
 *
 * Post: buffer at pcm is filled with the given number of samples
 */


bool nearly_equal( double val1, double val2, double tolerance )
{
	if (fabs(val1 - val2) < tolerance)
		return true;
	else
		return false;
}