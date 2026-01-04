#pragma once

#include "../core/sc1000.h"

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

/* DSP performance metrics */
struct dsp_stats {
    double load_percent;      /* Current DSP load (0-100+) */
    double load_peak;         /* Peak load since last reset */
    double process_time_us;   /* Last process time in microseconds */
    double budget_time_us;    /* Time budget per period in microseconds */
    unsigned long xruns;      /* Count of times we exceeded budget */
};

EXTERNC void audio_engine_process( struct sc1000* engine, signed short* pcm, unsigned long frames );
EXTERNC void audio_engine_get_stats( struct dsp_stats* stats );
EXTERNC void audio_engine_reset_peak( void );

#undef EXTERNC