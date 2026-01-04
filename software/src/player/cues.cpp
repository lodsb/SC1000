/*
 * Copyright (C) 2014 Mark Hills <mark@xwax.org>
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
#include <cstdlib>
#include <string>
#include <algorithm>

#include "cues.h"
#include "../util/log.h"

/*
 * Replace file extension with ".cue"
 * Returns empty string on failure (no extension found)
 */
static std::string get_cue_path(const char* pathname)
{
    if (pathname == nullptr) {
        return {};
    }

    std::string path(pathname);
    auto dot_pos = path.rfind('.');
    if (dot_pos == std::string::npos) {
        return {};
    }

    path.replace(dot_pos, std::string::npos, ".cue");
    return path;
}

void cues_reset(struct cues *q)
{
    std::fill(std::begin(q->position), std::end(q->position), CUE_UNSET);
}

void cues_unset(struct cues *q, unsigned int label)
{
    assert(label < MAX_CUES);
    q->position[label] = CUE_UNSET;
}

void cues_set(struct cues *q, unsigned int label, double position)
{
    assert(label < MAX_CUES);
    q->position[label] = position;
}

double cues_get(const struct cues *q, unsigned int label)
{
    assert(label < MAX_CUES);
    return q->position[label];
}

/*
 * Load cue points from .cue file if present
 */
void cues_load_from_file(struct cues *q, char const* pathname)
{
    std::string cuepath = get_cue_path(pathname);
    if (cuepath.empty()) {
        return;
    }

    FILE* f = fopen(cuepath.c_str(), "r");
    if (f == nullptr) {
        return;
    }

    int i = 0;
    while (i < MAX_CUES && fscanf(f, "%lf", &q->position[i]) != EOF) {
        i++;
    }

    fclose(f);
}

/*
 * Save cue points to .cue file if at least one cue point is set
 */
void cues_save_to_file(struct cues *q, char const* pathname)
{
    // Don't save if first cue point is at zero (likely uninitialized)
    if (q->position[0] == 0.0) {
        return;
    }

    std::string cuepath = get_cue_path(pathname);
    if (cuepath.empty()) {
        return;
    }

    LOG_DEBUG("Saving cue: %s", cuepath.c_str());

    FILE* f = fopen(cuepath.c_str(), "w");
    if (f == nullptr) {
        return;
    }

    for (int i = 0; i < MAX_CUES; i++) {
        fprintf(f, "%lf\n", q->position[i]);
    }

    fclose(f);

    // Sync the file to disk
    std::string sync_cmd = "/bin/sync \"" + cuepath + "\"";
    system(sync_cmd.c_str());
}
