#pragma once

#include "../app/sc1000.h"

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

EXTERNC void audio_engine_process( struct sc1000* engine, signed short* pcm, unsigned long frames );

#undef EXTERNC