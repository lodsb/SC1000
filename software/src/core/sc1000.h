#pragma once

#include "../player/deck.h"
#include "sc_input.h"
#include <vector>

struct sc_settings;

struct sc1000
{
   struct deck     scratch_deck;
   struct deck     beat_deck;

   struct sc_settings* settings;
   std::vector<mapping> mappings;

   // Crossfader position for CV output (0.0 = beat side, 1.0 = scratch side)
   double crossfader_position;

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

void sc1000_setup(struct sc1000* engine, struct rt *rt, const char* root_path);

void sc1000_load_sample_folders(struct sc1000* engine);

void sc1000_clear(struct sc1000* engine);


void sc1000_audio_engine_start(struct sc1000* engine);
void sc1000_audio_engine_stop(struct sc1000* engine);
void sc1000_audio_engine_init(struct sc1000* engine, struct sc1000_ops *ops);
void sc1000_handle_deck_recording(struct sc1000* engine);
ssize_t sc1000_audio_engine_pollfds(struct sc1000* engine, struct pollfd *pe, size_t z);
void sc1000_audio_engine_handle(struct sc1000* engine);

#ifdef __cplusplus
}
#endif