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

void deck::next_file(struct sc_settings* settings)
{
	if (files_present && playlist->has_next_file(current_folder_idx, current_file_idx))
	{
		LOG_DEBUG("files present");
		current_file_idx++;
		sc_file* file = playlist->get_file(current_folder_idx, current_file_idx);
		load_track_internal(this, track_acquire_by_import(importer, file->full_path), settings);
	} else {
		LOG_DEBUG("file not present");
	}
}

void deck::prev_file(struct sc_settings* settings)
{
	if (files_present && playlist->has_prev_file(current_folder_idx, current_file_idx))
	{
		current_file_idx--;
		sc_file* file = playlist->get_file(current_folder_idx, current_file_idx);
		load_track_internal(this, track_acquire_by_import(importer, file->full_path), settings);
	}
}

void deck::next_folder(struct sc_settings* settings)
{
	if (files_present && playlist->has_next_folder(current_folder_idx))
	{
		current_folder_idx++;
		current_file_idx = 0;
		sc_file* file = playlist->get_file(current_folder_idx, current_file_idx);
		load_track_internal(this, track_acquire_by_import(importer, file->full_path), settings);
	}
}

void deck::prev_folder(struct sc_settings* settings)
{
	if (files_present && playlist->has_prev_folder(current_folder_idx))
	{
		current_folder_idx--;
		current_file_idx = 0;
		sc_file* file = playlist->get_file(current_folder_idx, current_file_idx);
		load_track_internal(this, track_acquire_by_import(importer, file->full_path), settings);
	}
}

void deck::random_file(struct sc_settings* settings)
{
	if (files_present) {
		unsigned int num_files = static_cast<unsigned int>(playlist->total_files());
		unsigned int r = static_cast<unsigned int>(rand()) % num_files;
		LOG_DEBUG("Playing file %d/%d", r, num_files);
		sc_file* file = playlist->get_file_at_index(r);
		if (file != nullptr) {
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

//
// Legacy C API wrappers
//

int deck_init(struct deck* d, struct sc_settings* settings)
{
	return d->init(settings);
}

void deck_clear(struct deck* d)
{
	d->clear();
}

bool deck_is_locked(const struct deck* d)
{
	return d->is_locked();
}

void deck_recue(struct deck* d)
{
	d->recue();
}

void deck_clone(struct deck* d, const struct deck* from)
{
	d->clone(*from);
}

void deck_unset_cue(struct deck* d, unsigned int label)
{
	d->unset_cue(label);
}

void deck_cue(struct deck* d, unsigned int label)
{
	d->cue(label);
}

void deck_punch_in(struct deck* d, unsigned int label)
{
	d->punch_in(label);
}

void deck_punch_out(struct deck* d)
{
	d->punch_out();
}

void deck_load_folder(struct deck* d, char* folder_name)
{
	d->load_folder(folder_name);
}

void deck_next_file(struct deck* d, struct sc_settings* settings)
{
	d->next_file(settings);
}

void deck_prev_file(struct deck* d, struct sc_settings* settings)
{
	d->prev_file(settings);
}

void deck_next_folder(struct deck* d, struct sc_settings* settings)
{
	d->next_folder(settings);
}

void deck_prev_folder(struct deck* d, struct sc_settings* settings)
{
	d->prev_folder(settings);
}

void deck_random_file(struct deck* d, struct sc_settings* settings)
{
	d->random_file(settings);
}

void deck_record(struct deck* d)
{
	d->record();
}

bool deck_recall_loop(struct deck* d, struct sc_settings* settings)
{
	return d->recall_loop(settings);
}

bool deck_has_loop(const struct deck* d)
{
	return d->has_loop();
}
