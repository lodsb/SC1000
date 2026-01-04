#pragma once

#include "../core/sc1000.h"
#include <stdint.h>

struct loop_buffer;

/* DSP performance metrics */
struct dsp_stats {
    double load_percent;      /* Current DSP load (0-100+) */
    double load_peak;         /* Peak load since last reset */
    double process_time_us;   /* Last process time in microseconds */
    double budget_time_us;    /* Time budget per period in microseconds */
    unsigned long xruns;      /* Count of times we exceeded budget */
};

/* Capture input info passed to audio engine */
struct audio_capture {
    const int16_t* buffer;      /* Capture samples (nullptr if not available) */
    int channels;               /* Total channels in capture buffer */
    int left_channel;           /* Index of left channel */
    int right_channel;          /* Index of right channel */
    struct loop_buffer* loop[2]; /* Loop buffers for both decks (0=beat, 1=scratch) */
    int recording_deck;          /* Which deck is recording (-1 = none) */
    float monitoring_volume;     /* Monitoring volume */
};

/* Main audio processing function
 * - capture: input from capture device (can have nullptr buffer if not available)
 * - playback: output buffer to fill
 * - playback_channels: channels in playback buffer (usually 2)
 * - frames: number of frames to process
 */
void audio_engine_process(
    struct sc1000* engine,
    struct audio_capture* capture,
    int16_t* playback,
    int playback_channels,
    unsigned long frames
);

void audio_engine_get_stats(struct dsp_stats* stats);
void audio_engine_reset_peak(void);