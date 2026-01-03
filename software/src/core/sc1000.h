#pragma once

#include "../player/deck.h"

struct sc_settings;
struct mapping;

struct sc1000
{
   struct deck     scratch_deck;
   struct deck     beat_deck;

   struct sc_settings* settings;
   struct mapping*     mappings;

   /////////////////////////////////
   /// audio callback related stuff
   bool fault;
   void *audio_hw_context;
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

#ifdef __cplusplus
extern "C" {
#endif

void sc1000_setup(struct sc1000* engine, struct rt *rt);

void sc1000_load_sample_folders(struct sc1000* engine);

void sc1000_clear(struct sc1000* engine);


void sc1000_audio_engine_start(struct sc1000* engine);
void sc1000_audio_engine_stop(struct sc1000* engine);
void sc1000_audio_engine_init(struct sc1000* engine, struct sc1000_ops *ops);
void sc1000_audio_engine_process(struct sc1000* engine, signed short* pcm, unsigned long frames);
ssize_t sc1000_audio_engine_pollfds(struct sc1000* engine, struct pollfd *pe, size_t z);
void sc1000_audio_engine_handle(struct sc1000* engine);

#ifdef __cplusplus
}
#endif