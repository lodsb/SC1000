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

#pragma once

#include <alsa/asoundlib.h>

/*
 * State information for an open, non-blocking MIDI device
 */

struct midi {
    snd_rawmidi_t *in, *out;
};

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

EXTERNC int midi_open(struct midi *m, const char *name);
EXTERNC void midi_close(struct midi *m);

EXTERNC ssize_t midi_pollfds(struct midi *m, struct pollfd *pe, size_t len);
EXTERNC ssize_t midi_read(struct midi *m, void *buf, size_t len);
EXTERNC ssize_t midi_write(struct midi *m, const void *buf, size_t len);
EXTERNC int listdev(const char *devname, char names[64][64]);

#undef EXTERNC
