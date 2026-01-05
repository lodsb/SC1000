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

#include "../core/global.h"
#include "../core/sc1000.h"
#include "../platform/alsa.h"

#include "../util/log.h"
#include "../util/status.h"
#include "../thread/rig.h"

#include "../input/controller.h"

#include "cues.h"
#include "deck.h"
#include "playlist.h"
#include "track.h"

//
// Helper function (internal)
//

static void load_track_internal(struct deck* d, struct track* track, struct sc_settings* settings)
{
	struct player* pl = &d->player;
	cues_save_to_file(&d->cues, pl->track->path);
	pl->set_track(track);
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
		if (settings->platter_enabled)
		{
			d->angle_offset = 0 - d->encoder_angle;
		}
		else
		{
			d->angle_offset = static_cast<int32_t>(pl->position * settings->platter_speed) - d->encoder_angle;
		}
	}
}

//
// C++ Member function implementations
//

int deck::init(struct sc_settings* settings)
{
	ncontrol = 0;
	punch = NO_PUNCH;
	protect = false;
	assert(settings->importer != nullptr);
	importer = settings->importer;
	shifted = false;

	player.init(TARGET_SAMPLE_RATE, track_acquire_empty(), settings);
	cues_reset(&cues);

	playlist = nullptr;
	current_folder_idx = 0;
	current_file_idx = 0;
	files_present = false;
	deck_no = -1;  // Will be set by sc1000 init

	angle_offset = 0;
	encoder_angle = 0xffff;
	new_encoder_angle = 0xffff;

	loop_track = nullptr;

	return 0;
}

void deck::clear()
{
	player.clear();
	delete playlist;
	playlist = nullptr;

	if (loop_track)
	{
		track_release(loop_track);
		loop_track = nullptr;
	}
}

bool deck::is_locked() const
{
	return (protect && player.is_active());
}

void deck::recue()
{
	if (is_locked())
	{
		status_printf(STATUS_WARN, "Stop deck to recue");
		return;
	}

	player.recue();
}

void deck::clone(const deck& from)
{
	player.clone(from.player);
}

void deck::unset_cue(unsigned int label)
{
	cues_unset(&cues, label);
}

void deck::cue(unsigned int label)
{
	double p = cues_get(&cues, label);
	if (p == CUE_UNSET) {
		cues_set(&cues, label, player.get_elapsed());
		cues_save_to_file(&cues, player.track->path);
	}
	else {
		player.seek_to(p);
	}
}

void deck::punch_in(unsigned int label)
{
	double e = player.get_elapsed();
	double p = cues_get(&cues, label);
	if (p == CUE_UNSET)
	{
		cues_set(&cues, label, e);
		return;
	}

	if (punch != NO_PUNCH)
		e -= punch;

	player.seek_to(p);
	punch = p - e;
}

void deck::punch_out()
{
	if (punch == NO_PUNCH)
		return;

	double e = player.get_elapsed();
	player.seek_to(e - punch);
	punch = NO_PUNCH;
}

void deck::load_folder(char* folder_name)
{
	delete playlist;
	playlist = new Playlist();

	if (playlist->load(folder_name) && playlist->total_files() > 0)
	{
		LOG_INFO("Folder '%s' indexed with %zu files", folder_name, playlist->total_files());
		files_present = true;
		current_folder_idx = 0;
		current_file_idx = 0;

		LOG_DEBUG("deck_load_folder");

		sc_file* file = playlist->get_file(0, 0);
		player.set_track(track_acquire_by_import(importer, file->full_path));
		LOG_DEBUG("deck_load_folder set track ok");
		cues_load_from_file(&cues, player.track->path);
		LOG_DEBUG("deck_load_folder set cues_load_from_file ok");
	}
	else
	{
		files_present = false;
	}
}

void deck::next_file(struct sc1000* engine, struct sc_settings* settings)
{
	LOG_DEBUG("deck %d next_file called, files_present=%d, current_file_idx=%d, use_loop=%d",
	          deck_no, files_present, current_file_idx, player.use_loop);

	if (!files_present) return;

	if (current_file_idx == -1)
	{
		// At loop, go to first file
		current_file_idx = 0;
		player.use_loop = false;
		sc_file* file = playlist->get_file(current_folder_idx, 0);
		if (file != nullptr)
		{
			load_track_internal(this, track_acquire_by_import(importer, file->full_path), settings);
			LOG_DEBUG("deck %d next_file: loaded file 0", deck_no);
		}
		else
		{
			LOG_DEBUG("deck %d next_file: no file at folder %zu index 0", deck_no, current_folder_idx);
		}
	}
	else if (playlist->has_next_file(current_folder_idx, static_cast<size_t>(current_file_idx)))
	{
		current_file_idx++;
		sc_file* file = playlist->get_file(current_folder_idx, static_cast<size_t>(current_file_idx));
		if (file != nullptr)
		{
			load_track_internal(this, track_acquire_by_import(importer, file->full_path), settings);
			LOG_DEBUG("deck %d next_file: loaded file %d", deck_no, current_file_idx);
		}
	}
}

void deck::prev_file(struct sc1000* engine, struct sc_settings* settings)
{
	LOG_DEBUG("deck %d prev_file called, files_present=%d, current_file_idx=%d, use_loop=%d",
	          deck_no, files_present, current_file_idx, player.use_loop);

	if (!files_present) return;

	if (current_file_idx == -1)
	{
		// Already at loop, do nothing
		LOG_DEBUG("deck %d prev_file: already at loop, staying", deck_no);
		return;
	}
	else if (current_file_idx == 0)
	{
		// At first file, go to loop if exists
		bool has_loop = alsa_has_loop(engine, deck_no);
		LOG_DEBUG("deck %d prev_file: at file 0, has_loop=%d", deck_no, has_loop);
		if (has_loop)
		{
			goto_loop(engine, settings);
			LOG_DEBUG("deck %d prev_file: went to loop", deck_no);
		}
		// else: no loop, stay at position 0
	}
	else
	{
		// Normal prev behavior
		current_file_idx--;
		player.use_loop = false;
		sc_file* file = playlist->get_file(current_folder_idx, static_cast<size_t>(current_file_idx));
		if (file != nullptr)
		{
			load_track_internal(this, track_acquire_by_import(importer, file->full_path), settings);
			LOG_DEBUG("deck %d prev_file: loaded file %d", deck_no, current_file_idx);
		}
	}
}

void deck::next_folder(struct sc1000* engine, struct sc_settings* settings)
{
	if (!files_present) return;

	// If at loop, stay at loop (folder change doesn't affect it)
	if (current_file_idx == -1)
	{
		if (playlist->has_next_folder(current_folder_idx))
		{
			current_folder_idx++;
			LOG_DEBUG("Deck %d: next_folder to %zu (staying at loop)", deck_no, current_folder_idx);
		}
		return;  // Stay at loop
	}

	// Normal folder navigation
	if (playlist->has_next_folder(current_folder_idx))
	{
		current_folder_idx++;
		current_file_idx = 0;
		sc_file* file = playlist->get_file(current_folder_idx, 0);
		load_track_internal(this, track_acquire_by_import(importer, file->full_path), settings);
		LOG_DEBUG("Deck %d: next_folder to %zu, file 0", deck_no, current_folder_idx);
	}
}

void deck::prev_folder(struct sc1000* engine, struct sc_settings* settings)
{
	if (!files_present) return;

	// If at loop, stay at loop (folder change doesn't affect it)
	if (current_file_idx == -1)
	{
		if (playlist->has_prev_folder(current_folder_idx))
		{
			current_folder_idx--;
			LOG_DEBUG("Deck %d: prev_folder to %zu (staying at loop)", deck_no, current_folder_idx);
		}
		return;  // Stay at loop
	}

	// Normal folder navigation
	if (playlist->has_prev_folder(current_folder_idx))
	{
		current_folder_idx--;
		current_file_idx = 0;
		sc_file* file = playlist->get_file(current_folder_idx, 0);
		load_track_internal(this, track_acquire_by_import(importer, file->full_path), settings);
		LOG_DEBUG("Deck %d: prev_folder to %zu, file 0", deck_no, current_folder_idx);
	}
}

void deck::random_file(struct sc1000* engine, struct sc_settings* settings)
{
	if (files_present)
	{
		unsigned int num_files = static_cast<unsigned int>(playlist->total_files());
		unsigned int r = static_cast<unsigned int>(rand()) % num_files;
		LOG_DEBUG("Deck %d: random_file %d/%d", deck_no, r, num_files);
		sc_file* file = playlist->get_file_at_index(r);
		if (file != nullptr)
		{
			// Random file exits loop mode
			player.use_loop = false;
			// We don't update current_file_idx here since random doesn't fit folder navigation
			// Just load the track
			load_track_internal(this, track_acquire_by_import(importer, file->full_path), settings);
		}
	}
}

void deck::record()
{
	player.recording_started = !player.recording_started;
}

bool deck::recall_loop(struct sc_settings* settings)
{
	if (!loop_track || loop_track->length == 0)
	{
		return false;
	}

	track_acquire(loop_track);
	player.set_track(loop_track);

	player.position = 0;
	player.target_position = 0;
	player.offset = 0;

	if (settings->platter_enabled)
	{
		angle_offset = 0 - encoder_angle;
	}
	else
	{
		angle_offset = static_cast<int32_t>(player.position * settings->platter_speed) - encoder_angle;
	}

	return true;
}

bool deck::has_loop() const
{
	return loop_track != nullptr && loop_track->length > 0;
}

void deck::goto_loop(struct sc1000* engine, struct sc_settings* settings)
{
	current_file_idx = -1;
	player.use_loop = true;
	player.position = 0;
	player.target_position = 0;
	player.offset = 0;

	// Reset platter angle offset
	if (settings->platter_enabled)
	{
		angle_offset = 0 - encoder_angle;
	}
	else
	{
		angle_offset = static_cast<int32_t>(player.position * settings->platter_speed) - encoder_angle;
	}

	LOG_DEBUG("Deck %d: goto_loop", deck_no);
}

