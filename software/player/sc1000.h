#pragma once

#include "deck.h"
#include "recorder.h"
#include "settings.h"

struct sc1000
{
   struct deck     scratch_deck;
   struct deck     beat_deck;
   struct recorder recorder;

   struct sc_settings* settings;

   /////////////////////////
   bool fault;
   void *local;
   struct sc1000_ops *ops;
};

struct sc1000_ops {
   ssize_t (*pollfds)(struct sc1000 *engine, struct pollfd *pe, size_t z);
   int (*handle)(struct sc1000 *engine);

   unsigned int (*sample_rate)(struct sc1000 *engine);
   void (*start)(struct sc1000 *engine);
   void (*stop)(struct sc1000 *engine);

   void (*clear)(struct sc1000 *engine);
};

void sc1000_setup(struct sc1000* engine, struct sc_settings* settings, struct rt *rt);

void sc1000_load_sample_folders(struct sc1000* engine);

void sc1000_clear(struct sc1000* engine);


void sc1000_engine_start(struct sc1000* engine);
void sc1000_engine_stop(struct sc1000* engine);
void sc1000_engine_init(struct sc1000* engine, struct sc1000_ops *ops);
void sc1000_engine_process(struct sc1000* engine, signed short* pcm, unsigned long frames);
ssize_t sc1000_engine_pollfds(struct sc1000* engine, struct pollfd *pe, size_t z);
void sc1000_engine_handle(struct sc1000* engine);