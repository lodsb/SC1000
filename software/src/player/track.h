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

#pragma once

#include <stdbool.h>
#include <sys/poll.h>
#include <sys/types.h>

#define TRACK_CHANNELS 2

#define TRACK_MAX_BLOCKS 64
#define TRACK_BLOCK_SAMPLES (2048 * 1024)
#define TRACK_PPM_RES 64
#define TRACK_OVERVIEW_RES 2048

struct track_block {
    signed short pcm[TRACK_BLOCK_SAMPLES * TRACK_CHANNELS];
};

struct track {
    unsigned int refcount;
    int rate;

    /* pointers to external data */

    const char *importer, *path;

    size_t bytes; /* loaded in */
    unsigned int length, /* track length in samples */
        blocks; /* number of blocks allocated */
    struct track_block *block[TRACK_MAX_BLOCKS];

    /* State of audio import */

    pid_t pid;
    int fd;
    struct pollfd *pe;
    bool terminated;

   bool finished;
};


#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

EXTERNC void track_use_mlock(void);

/* Tracks are dynamically allocated and reference counted */

EXTERNC struct track* track_acquire_by_import(const char *importer, const char *path);
EXTERNC struct track* track_acquire_empty(void);
EXTERNC struct track* track_acquire_for_recording(int sample_rate);
EXTERNC void track_acquire(struct track *t);
EXTERNC void track_release(struct track *t);

/* Functions for direct writing (used by loop buffer) */
EXTERNC int track_ensure_space(struct track *tr, unsigned int samples);
EXTERNC void track_set_length(struct track *tr, unsigned int samples);

/* Functions used by the rig and main thread */

EXTERNC void track_pollfd(struct track *tr, struct pollfd *pe);
EXTERNC void track_handle(struct track *tr);

/* Return true if the track importer is running, otherwise false */

static inline bool track_is_importing(struct track *tr)
{
    return tr->pid != 0;
}

/* Return a pointer to (not value of) the sample data for each channel */

static inline signed short* track_get_sample(struct track *tr, int s)
{
    struct track_block *b;
    b = tr->block[s / TRACK_BLOCK_SAMPLES];
    return &b->pcm[(s % TRACK_BLOCK_SAMPLES) * TRACK_CHANNELS];
}

#undef EXTERNC

