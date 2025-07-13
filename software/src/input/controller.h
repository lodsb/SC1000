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
#include <stdlib.h>
#include <sys/poll.h>
#include <sys/types.h>

struct deck;
struct rt;

/*
 * Base state of a 'controller', which is a MIDI controller or HID
 * device used to control the program
 */

struct controller {
    bool fault;
    void *local;
    struct controller_ops *ops;
};

/*
 * Functions which must be implemented for a controller
 */

struct controller_ops {
    int (*add_deck)(struct controller *c, struct deck *deck );

    ssize_t (*pollfds)(struct controller *c, struct pollfd *pe, size_t z);
    int (*realtime)(struct controller *c);

    void (*clear)(struct controller *c);
};


#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif
EXTERNC int     controller_init(struct controller *c, struct controller_ops *t, void *local, struct rt *rt);
EXTERNC void    controller_clear(struct controller *c);

EXTERNC void    controller_add_deck(struct controller *c, struct deck *d );
EXTERNC ssize_t controller_pollfds(struct controller *c, struct pollfd *pe, size_t z);
EXTERNC void    controller_handle(struct controller *c);
#undef EXTERNC