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

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include "../input/controller.h"
#include "../core/sc1000.h"
#include "../util/debug.h"
#include "../util/log.h"

#include "realtime.h"
#include "thread.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*x))

/*
 * Raise the priority of the current thread
 *
 * Return: -1 if priority could not be satisfactorily raised, otherwise 0
 */
static int raise_priority(int priority)
{
    int max_pri;
    struct sched_param sp;

    max_pri = sched_get_priority_max(SCHED_FIFO);

    if (priority > max_pri) {
        LOG_ERROR("Invalid scheduling priority (maximum %d)", max_pri);
        return -1;
    }

    if (sched_getparam(0, &sp)) {
        perror("sched_getparam");
        return -1;
    }

    sp.sched_priority = priority;

    if (sched_setscheduler(0, SCHED_FIFO, &sp)) {
        perror("sched_setscheduler");
        LOG_ERROR("Failed to get realtime priorities");
        return -1;
    }

    return 0;
}

/*
 * The realtime thread
 */
static void rt_main(struct rt* rt)
{
    int r;
    size_t n;

    debug("%p", rt);

    thread_to_realtime();

    if (rt->priority != 0) {
        if (raise_priority(rt->priority) == -1) {
            rt->finished = true;
        }
    }

    if (sem_post(&rt->sem) == -1) {
        abort();
    }

    while (!rt->finished) {
        r = poll(rt->pt, static_cast<nfds_t>(rt->npt), -1);
        if (r == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                perror("poll2");
                abort();
            }
        }

        for (n = 0; n < rt->nctl; n++) {
            controller_handle(rt->ctl[n]);
        }

        sc1000_audio_handle(rt->engine);
    }
}

static void* launch(void* p)
{
    rt_main(static_cast<struct rt*>(p));
    return nullptr;
}

/*
 * Initialise state of realtime handler
 */
void rt_init(struct rt* rt)
{
    debug("%p", rt);

    rt->finished = false;
    rt->nctl = 0;
    rt->npt = 0;
}

/*
 * Clear resources associated with the realtime handler
 */
void rt_clear(struct rt* rt)
{
    (void)rt;
}

/*
 * Add a device to this realtime handler
 *
 * Return: -1 if the device could not be added, otherwise 0
 * Post: if 0 is returned the device is added
 */
int rt_set_sc1000(struct rt* rt, struct sc1000* engine)
{
    ssize_t z;

    debug("%p adding device %p", rt, engine);

    z = sc1000_audio_pollfds(engine, &rt->pt[rt->npt], sizeof(rt->pt) - rt->npt);
    if (z == -1) {
        LOG_ERROR("Device failed to return file descriptors");
        return -1;
    }

    rt->npt += static_cast<size_t>(z);
    rt->engine = engine;

    return 0;
}

/*
 * Add a controller to the realtime handler
 *
 * Return: -1 if the device could not be added, otherwise 0
 */
int rt_add_controller(struct rt* rt, Controller* c)
{
    ssize_t z;

    debug("%p adding controller %p", rt, c);

    if (rt->nctl == ARRAY_SIZE(rt->ctl)) {
        LOG_WARN("Too many controllers");
        return -1;
    }

    z = controller_pollfds(c, &rt->pt[rt->npt], sizeof(rt->pt) - rt->npt);
    if (z == -1) {
        LOG_ERROR("Controller failed to return file descriptors");
        return -1;
    }

    rt->npt += static_cast<size_t>(z);
    rt->ctl[rt->nctl++] = c;

    return 0;
}

/*
 * Start realtime handling of the given devices
 *
 * Return: -1 on error, otherwise 0
 */
int rt_start(struct rt* rt, int priority)
{
    assert(priority >= 0);
    rt->priority = priority;

    if (rt->npt > 0) {
        int r;

        LOG_INFO("Launching realtime thread to handle devices...");

        if (sem_init(&rt->sem, 0, 0) == -1) {
            perror("sem_init");
            return -1;
        }

        r = pthread_create(&rt->ph, nullptr, launch, static_cast<void*>(rt));
        if (r != 0) {
            errno = r;
            perror("pthread_create");
            if (sem_destroy(&rt->sem) == -1) {
                abort();
            }
            return -1;
        }

        if (sem_wait(&rt->sem) == -1) {
            abort();
        }
        if (sem_destroy(&rt->sem) == -1) {
            abort();
        }

        if (rt->finished) {
            if (pthread_join(rt->ph, nullptr) != 0) {
                abort();
            }
            return -1;
        }
    }

    sc1000_audio_start(rt->engine);

    return 0;
}

/*
 * Stop realtime handling, which was previously started by rt_start()
 */
void rt_stop(struct rt* rt)
{
    rt->finished = true;

    sc1000_audio_stop(rt->engine);

    if (rt->npt > 0) {
        if (pthread_join(rt->ph, nullptr) != 0) {
            abort();
        }
    }
}
