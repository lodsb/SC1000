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

#include "../input/midi.h"

#define NUMDECKS 2

struct controller;
struct rt;

struct dicer
{
    struct midi midi;
    struct deck *deck[NUMDECKS];

    char obuf[180];
    size_t ofill;
	bool shifted;
	
	bool parsing;
	unsigned char parsed_bytes;
	unsigned char midi_buffer[3];

    char PortName[32];
};

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif
EXTERNC int dicer_init(struct controller *c, struct rt *rt, const char *hw);

#undef EXTERNC
