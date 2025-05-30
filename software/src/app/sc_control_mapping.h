/*
 * Copyright (C) 2018 Andrew Tait <rasteri@gmail.com>
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

#include "sc_settings.h"
#include "sc_input.h"
#include "sc1000.h"

extern struct mapping *queued_midi_command;
extern unsigned char queued_midi_buffer[3];

struct mapping *find_midi_mapping( struct mapping *maps, unsigned char buf[3], enum EdgeType edge );
struct mapping *find_io_mapping( struct mapping *mappings, unsigned char port, unsigned char pin, enum EdgeType edge );
void io_event( struct mapping *map, unsigned char midi_buffer[3], struct sc1000* sc1000_engine, struct sc_settings* settings );

