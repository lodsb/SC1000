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
#include <cstdlib>
#include <cstdio>

#include "../core/global.h"

#include "../util/status.h"
#include "../thread/rig.h"

#include "../input/controller.h"

#include "cues.h"
#include "deck.h"
#include "playlist.h"
#include "track.h"

/*
 * An empty record, is used briefly until a record is loaded
 * to a deck
 */

/*
 * Initialise a deck
 *
 * A deck is a logical grouping of the various components which
 * reflects the user's view on a deck in the system.
 *
 * Pre: deck->device is valid
 */

int deck_init(struct deck *d, struct sc_settings* settings)
{
	d->ncontrol = 0;
	d->punch = NO_PUNCH;
	d->protect = false;
	assert(settings->importer != nullptr);
	d->importer = settings->importer;
	d->shifted = false;

	player_init(&d->player, TARGET_SAMPLE_RATE, track_acquire_empty(), settings);
	cues_reset(&d->cues);

	d->playlist = nullptr;
	d->current_folder_idx = 0;
	d->current_file_idx = 0;
	d->files_present = false;

	d->angle_offset = 0;
	d->encoder_angle = 0xffff;
	d->new_encoder_angle = 0xffff;

	d->loop_track = nullptr;

	return 0;
}

void deck_clear(struct deck *d)
{
	player_clear(&d->player);
	delete d->playlist;
	d->playlist = nullptr;

	if (d->loop_track)
	{
		track_release(d->loop_track);
		d->loop_track = nullptr;
	}
}

bool deck_is_locked(const struct deck *d)
{
	return (d->protect && player_is_active(&d->player));
}

void deck_recue(struct deck *d)
{
	if (deck_is_locked(d))
	{
		status_printf(STATUS_WARN, "Stop deck to recue");
		return;
	}

	player_recue(&d->player);
}

void deck_clone(struct deck *d, const struct deck *from)
{
	player_clone(&d->player, &from->player);
}

/*
 * Clear the cue point, ready to be set again
 */

void deck_unset_cue(struct deck *d, unsigned int label)
{
	cues_unset(&d->cues, label);
}

/*
 * Seek the current playback position to a cue point position,
 * or set the cue point if unset
 */

void deck_cue(struct deck *d, unsigned int label)
{
	double p;

	p = cues_get(&d->cues, label);
	if (p == CUE_UNSET) {
		cues_set(&d->cues, label, player_get_elapsed(&d->player));
		cues_save_to_file(&d->cues, d->player.track->path);
	}
	else
		player_seek_to(&d->player, p);
}

/*
 * Seek to a cue point ready to return from it later. Overrides an
 * existing punch operation.
 */

void deck_punch_in(struct deck *d, unsigned int label)
{
	double p, e;

	e = player_get_elapsed(&d->player);
	p = cues_get(&d->cues, label);
	if (p == CUE_UNSET)
	{
		cues_set(&d->cues, label, e);
		return;
	}

	if (d->punch != NO_PUNCH)
		e -= d->punch;

	player_seek_to(&d->player, p);
	d->punch = p - e;
}

/*
 * Return from a cue point
 */

void deck_punch_out(struct deck *d)
{
	double e;

	if (d->punch == NO_PUNCH)
		return;

	e = player_get_elapsed(&d->player);
	player_seek_to(&d->player, e - d->punch);
	d->punch = NO_PUNCH;
}

void deck_load_folder(struct deck *d, char *folder_name)
{
	// Build index of all audio files on the USB stick
	delete d->playlist;
	d->playlist = new Playlist();

	if (d->playlist->load(folder_name) && d->playlist->total_files() > 0)
	{
		printf("Folder '%s' Indexed with %zu files: \n", folder_name, d->playlist->total_files());
		d->files_present = true;
		d->current_folder_idx = 0;
		d->current_file_idx = 0;

		printf("deck_load_folder\n");

		// Load first beat
		sc_file* file = d->playlist->get_file(0, 0);
		player_set_track(&d->player, track_acquire_by_import(d->importer, file->full_path));
		printf("deck_load_folder set track ok\n");
		cues_load_from_file(&d->cues, d->player.track->path);
		printf("deck_load_folder set cues_load_from_file ok\n");
	}
	else
	{
		d->files_present = false;
	}
}

static void load_track(struct deck *d, struct track *track, struct sc_settings* settings)
{
	struct player *pl = &d->player;
	cues_save_to_file(&d->cues, pl->track->path);
	player_set_track(pl, track);
	pl->target_position = 0;
	pl->position = 0;
	pl->offset = 0;
	pl->use_loop = false;  // Switch back to file track (loop is preserved for recall)
	cues_load_from_file(&d->cues, pl->track->path);
	pl->fader_pitch = 1.0;
	pl->bend_pitch = 1.0;
	pl->note_pitch = 1.0;
	if (!d->player.just_play)
	{
		// If touch sensor is enabled, set the "zero point" to the current encoder angle
		if (settings->platter_enabled)
		{
			d->angle_offset = 0 - d->encoder_angle;
		}
		else // If touch sensor is disabled, set the "zero point" to encoder zero point so sticker is exactly on each time sample is loaded
		{
			d->angle_offset = static_cast<int32_t>(pl->position * settings->platter_speed) - d->encoder_angle;
		}
	}
}

void deck_next_file(struct deck *d, struct sc_settings* settings)
{
	if (d->files_present && d->playlist->has_next_file(d->current_folder_idx, d->current_file_idx))
	{
		printf("files present\n");
		d->current_file_idx++;
		sc_file* file = d->playlist->get_file(d->current_folder_idx, d->current_file_idx);
		load_track(d, track_acquire_by_import(d->importer, file->full_path), settings);
	} else {
		printf("file not present\n");
	}
}

void deck_prev_file(struct deck *d, struct sc_settings* settings)
{
	if (d->files_present && d->playlist->has_prev_file(d->current_folder_idx, d->current_file_idx))
	{
		d->current_file_idx--;
		sc_file* file = d->playlist->get_file(d->current_folder_idx, d->current_file_idx);
		load_track(d, track_acquire_by_import(d->importer, file->full_path), settings);
	}
}

void deck_next_folder(struct deck *d, struct sc_settings* settings)
{
	if (d->files_present && d->playlist->has_next_folder(d->current_folder_idx))
	{
		d->current_folder_idx++;
		d->current_file_idx = 0;
		sc_file* file = d->playlist->get_file(d->current_folder_idx, d->current_file_idx);
		load_track(d, track_acquire_by_import(d->importer, file->full_path), settings);
	}
}

void deck_prev_folder(struct deck *d, struct sc_settings* settings)
{
	if (d->files_present && d->playlist->has_prev_folder(d->current_folder_idx))
	{
		d->current_folder_idx--;
		d->current_file_idx = 0;
		sc_file* file = d->playlist->get_file(d->current_folder_idx, d->current_file_idx);
		load_track(d, track_acquire_by_import(d->importer, file->full_path), settings);
	}
}

void deck_random_file(struct deck *d, struct sc_settings* settings)
{
	if (d->files_present) {
		unsigned int num_files = static_cast<unsigned int>(d->playlist->total_files());
		unsigned int r = static_cast<unsigned int>(rand()) % num_files;
		printf("Playing file %d/%d\n", r, num_files);
		sc_file* file = d->playlist->get_file_at_index(r);
		if (file != nullptr) {
			load_track(d, track_acquire_by_import(d->importer, file->full_path), settings);
		}
	}
}

void deck_record(struct deck *d)
{
	d->player.recording_started = !d->player.recording_started;
}

bool deck_recall_loop(struct deck *d, struct sc_settings* settings)
{
	if (!d->loop_track || d->loop_track->length == 0)
	{
		return false;
	}

	// Load the loop track onto the player
	track_acquire(d->loop_track);
	player_set_track(&d->player, d->loop_track);

	// Reset position to start
	d->player.position = 0;
	d->player.target_position = 0;
	d->player.offset = 0;

	// Reset platter angle offset if platter is enabled
	if (settings->platter_enabled)
	{
		d->angle_offset = 0 - d->encoder_angle;
	}
	else
	{
		d->angle_offset = static_cast<int32_t>(d->player.position * settings->platter_speed) - d->encoder_angle;
	}

	return true;
}

bool deck_has_loop(const struct deck *d)
{
	return d->loop_track != nullptr && d->loop_track->length > 0;
}
