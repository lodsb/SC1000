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

#ifndef REALTIME_H
#define REALTIME_H

#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>

#ifdef __cplusplus
class Controller;
#else
typedef struct Controller Controller;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * State data for the realtime thread, maintained during rt_start and
 * rt_stop
 */

struct rt {
    pthread_t ph;
    sem_t sem;
    bool finished;
    int priority;

    struct sc1000 *engine;

    size_t nctl;
    Controller *ctl[3];

    size_t npt;
    struct pollfd pt[32];
};

int rt_global_init(void);
void rt_not_allowed(void);

void rt_init(struct rt *rt);
void rt_clear(struct rt *rt);

int rt_set_sc1000(struct rt *rt, struct sc1000 *engine);
int rt_add_controller(struct rt *rt, Controller *c);

int rt_start(struct rt *rt, int priority);
void rt_stop(struct rt *rt);

#ifdef __cplusplus
}
#endif

#endif
