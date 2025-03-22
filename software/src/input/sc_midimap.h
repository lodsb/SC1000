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

#include "../app/sc_settings.h"
#include "../app/sc1000.h"

extern struct mapping *queued_midi_command;
extern unsigned char queued_midi_buffer[3];

void add_mapping(struct mapping **maps, unsigned char Type, unsigned char deckno, unsigned char buf[3], unsigned char port, unsigned char Pin, bool Pullup, char Edge, unsigned char action, unsigned char parameter);
struct mapping *find_midi_mapping( struct mapping *maps, unsigned char buf[3], char edge );
struct mapping *find_io_mapping( struct mapping *maps, unsigned char port, unsigned char pin, char edge );
void io_event( struct mapping *map, unsigned char midi_buffer[3], struct sc1000* sc1000_engine, struct sc_settings* settings );
void add_config_mapping(struct mapping **maps, unsigned char Type, unsigned char buf[3], unsigned char port, unsigned char Pin, bool Pullup, char Edge, char *actions);

