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

#include "../util/status.h"
#include "../thread/rig.h"
#include "../xwax.h"
#include "../global/global.h"

#include "controller.h"
#include "cues.h"
#include "deck.h"
#include "sc_playlist.h"

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

int deck_init( struct deck *d, struct rt *rt,
               struct sc_settings* settings,
               bool slave)
{
	if (!slave)
	{
		if (rt_add_device(rt, &d->device) == -1)
			return -1;
	}

	d->ncontrol = 0;
	d->punch = NO_PUNCH;
	d->protect = false;
	assert(settings->importer != NULL);
	d->importer = settings->importer;
	d->shifted = 0;

	//timecoder_init(&d->timecoder, timecode, speed, rate, phono);
	player_init(&d->player, TARGET_SAMPLE_RATE, track_acquire_empty(), settings);
	cues_reset(&d->cues);

	/* The timecoder and player are driven by requests from
	 * the audio device */

	//device_connect_timecoder(&d->device, &d->timecoder);

	d->first_folder = NULL;
	d->current_file = NULL;
	d->current_folder = NULL;
	d->num_files = 0;
	d->files_present = 0;

	d->angle_offset = 0;
	d->encoder_angle = 0xffff;
	d->new_encoder_angle = 0xffff;

	device_connect_player(&d->device, &d->player);
	return 0;
}

void deck_clear(struct deck *d )
{
	/* FIXME: remove from rig and rt */
	player_clear(&d->player);
	//timecoder_clear(&d->timecoder);
	device_clear(&d->device);
}

bool deck_is_locked(const struct deck *d )
{
	return (d->protect && player_is_active(&d->player));
}

/*
 * Load a record from the library to a deck
 */
/*
void deck_load(struct deck *d, struct record *record) {
	struct track *t;

	if (deck_is_locked(d)) {
		status_printf(STATUS_WARN, "Stop deck to load a different track");
		return;
	}

	t = track_acquire_by_import(d->importer, record->pathname);
	if (t == NULL )
		return;

	d->record = record;
	player_set_track(&d->player, t); // passes reference 
}
*/
void deck_recue(struct deck *d )
{
	if (deck_is_locked(d))
	{
		status_printf(STATUS_WARN, "Stop deck to recue");
		return;
	}

	player_recue(&d->player);
}

void deck_clone( struct deck *d, const struct deck *from )
{
	player_clone(&d->player, &from->player);
}

/*
 * Clear the cue point, ready to be set again
 */

void deck_unset_cue( struct deck *d, unsigned int label )
{
	cues_unset(&d->cues, label);
}

/*
 * Seek the current playback position to a cue point position,
 * or set the cue point if unset
 */

void deck_cue( struct deck *d, unsigned int label )
{
	double p;

	p = cues_get(&d->cues, label);
	if (p == CUE_UNSET){
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

void deck_punch_in( struct deck *d, unsigned int label )
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

void deck_punch_out(struct deck *d )
{
	double e;

	if (d->punch == NO_PUNCH)
		return;

	e = player_get_elapsed(&d->player);
	player_seek_to(&d->player, e - d->punch);
	d->punch = NO_PUNCH;
}

void deck_load_folder( struct deck *d, char *FolderName )
{
	// Build index of all audio files on the USB stick
	if ( (d->first_folder = load_file_structure(FolderName, &d->num_files)) != NULL && d->num_files > 0)
	{
		printf("Folder '%s' Indexed with %d files: \n", FolderName, d->num_files);
		d->files_present = 1;
	}
	if (d->files_present)
	{
		//DumpFileStructure(d->FirstFolder);
		d->current_folder = d->first_folder;
		d->current_file = d->current_folder->first_file;

      printf("deck_load_folder\n");

		// Load first beat
		player_set_track(&d->player, track_acquire_by_import(d->importer, d->current_file->full_path));
      printf("deck_load_folder set track ok\n");
		cues_load_from_file(&d->cues, d->player.track->path);
      printf("deck_load_folder set cues_load_from_file ok\n");
	}
}

void load_track( struct deck *d, struct track *track, struct sc_settings* settings )
{
	struct player *pl = &d->player;
	cues_save_to_file(&d->cues, pl->track->path);
	player_set_track(pl, track);
	pl->target_position = 0;
	pl->position = 0;
	pl->offset = 0;
	cues_load_from_file(&d->cues, pl->track->path);
	pl->fader_pitch = 1.0;
	pl->bend_pitch = 1.0;
	pl->note_pitch = 1.0;
	if (!d->player.justPlay)
	{
		// If touch sensor is enabled, set the "zero point" to the current encoder angle
		if (settings->platter_enabled)
			d->angle_offset = 0 - d->encoder_angle;

		else // If touch sensor is disabled, set the "zero point" to encoder zero point so sticker is exactly on each time sample is loaded
			d->angle_offset = (pl->position * settings->platter_speed) - d->encoder_angle;
	}
}

void deck_next_file(struct deck *d, struct sc_settings* settings )
{
	if ( d->files_present && d->current_file->next != NULL)
	{
		printf("files present\n");
		d->current_file = d->current_file->next;
		load_track(d, track_acquire_by_import(d->importer, d->current_file->full_path), settings);
	} else {
		printf("file not present\n");
	}
	
}

void deck_prev_file(struct deck *d, struct sc_settings* settings )
{
	if ( d->files_present && d->current_file->prev != NULL)
	{
		d->current_file = d->current_file->prev;
		load_track(d, track_acquire_by_import(d->importer, d->current_file->full_path), settings);
	}
	
}

void deck_next_folder(struct deck *d, struct sc_settings* settings )
{
	if ( d->files_present && d->current_folder->next != NULL)
	{
		d->current_folder = d->current_folder->next;
		d->current_file = d->current_folder->first_file;
		load_track(d, track_acquire_by_import(d->importer, d->current_file->full_path), settings);
	}
}
void deck_prev_folder(struct deck *d, struct sc_settings* settings)
{
	if ( d->files_present && d->current_folder->prev != NULL)
	{
		d->current_folder = d->current_folder->prev;
		d->current_file = d->current_folder->first_file;
		load_track(d, track_acquire_by_import(d->importer, d->current_file->full_path), settings);
	}
}

void deck_random_file(struct deck *d, struct sc_settings* settings)
{
	if (d->files_present){
		int r = rand() % d->num_files;
		printf("Playing file %d/%d\n", r, d->num_files);
		load_track(d, track_acquire_by_import(d->importer, get_file_at_index(r, d->first_folder)->full_path), settings);
	}
}

void deck_record(struct deck *d )
{
	d->player.recordingStarted = !d->player.recordingStarted;
}
