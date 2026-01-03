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
#include <cstdio>

#include "../util/debug.h"
#include "../player/deck.h"

#include "controller.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*x))

/*
 * Add a deck to this controller, if possible
 */
void controller_add_deck(Controller* c, struct deck* d)
{
    debug("%p adding deck %p", c, d);

    if (c->add_deck(d) == 0) {
        debug("deck was added");

        assert(d->ncontrol < ARRAY_SIZE(d->control));
        d->control[d->ncontrol++] = c;
    }
}

/*
 * Get file descriptors which should be polled for this controller
 *
 * Return: the number of pollfd filled, or -1 on error
 */
ssize_t controller_pollfds(Controller* c, struct pollfd* pe, size_t z)
{
    return c->pollfds(pe, z);
}

void controller_handle(Controller* c)
{
    if (c->has_fault()) {
        return;
    }

    if (c->realtime() != 0) {
        c->set_fault();
        fputs("Error handling hardware controller; disabling it\n", stderr);
    }
}

void controller_clear(Controller* c)
{
    debug("%p", c);
    c->clear();
}
