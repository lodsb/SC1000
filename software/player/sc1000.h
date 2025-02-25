#pragma once

#include "deck.h"
#include "recorder.h"
#include "settings.h"

struct sc1000
{
   struct deck     scratch_deck;
   struct deck     beat_deck;
   struct recorder recorder;
};

void sc1000_init(struct sc1000* engine, struct sc_settings* settings,
                 struct rt *rt, const char *importer);

void sc1000_load_sample_folders(struct sc1000* engine);

void sc1000_clear(struct sc1000* engine);