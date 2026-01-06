#pragma once

#include "../core/sc1000.h"
#include <stdint.h>
#include <stdbool.h>
#include <memory>

struct loop_buffer;

//
// C-compatible types and API (for backward compatibility)
//

/* Interpolation mode selection */
typedef enum {
    INTERP_CUBIC = 0,  /* 4-tap Catmull-Rom (fast, no anti-aliasing) */
    INTERP_SINC = 1    /* 16-tap sinc (slower, proper anti-aliasing) */
} interpolation_mode_t;

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

#ifdef __cplusplus
extern "C" {
#endif

/* Set interpolation mode (call before audio engine starts) */
void audio_engine_set_interpolation(interpolation_mode_t mode);

/* Get current interpolation mode */
interpolation_mode_t audio_engine_get_interpolation(void);

/* Main audio processing function (legacy S16 interface)
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

#ifdef __cplusplus
}
#endif


#ifdef __cplusplus
//
// C++ class-based interface
//

#include "sample_format.h"
#include "interpolation_policy.h"
#include <alsa/asoundlib.h>

namespace sc {
namespace audio {

//
// DSP statistics (C++ version)
//
struct DspStats {
    double load_percent = 0.0;
    double load_peak = 0.0;
    double process_time_us = 0.0;
    double budget_time_us = 0.0;
    unsigned long xruns = 0;
};

//
// Abstract base class for runtime dispatch
// Virtual dispatch happens once per buffer (~256 samples), negligible overhead.
//
class AudioEngineBase {
public:
    virtual ~AudioEngineBase() = default;

    // Main processing function
    // capture: input audio (nullptr if not available)
    // playback: output buffer (format determined by template)
    // frames: number of frames to process
    virtual void process(
        sc1000* engine,
        audio_capture* capture,
        void* playback,
        int playback_channels,
        unsigned long frames) = 0;

    virtual const DspStats& get_stats() const = 0;
    virtual void reset_peak() = 0;

    // Factory: creates correct template instantiation based on mode and format
    static std::unique_ptr<AudioEngineBase> create(
        InterpolationMode interp,
        snd_pcm_format_t format);
};

//
// Templated audio engine with compile-time optimized internals
//
// InterpPolicy: CubicInterpolation or SincInterpolation
// FormatPolicy: FormatS16, FormatS24_3LE, FormatS24_LE, FormatS32, FormatFloat
//
template<typename InterpPolicy, typename FormatPolicy>
class AudioEngine final : public AudioEngineBase {
public:
    AudioEngine() = default;

    void process(
        sc1000* engine,
        audio_capture* capture,
        void* playback,
        int playback_channels,
        unsigned long frames) override;

    const DspStats& get_stats() const override { return stats_; }
    void reset_peak() override {
        stats_.load_peak = 0.0;
        stats_.xruns = 0;
    }

private:
    DspStats stats_{};

    // Setup player parameters for the block
    void setup_player(player* pl, unsigned long samples, const sc_settings* settings,
                      double* target_volume, double* filtered_pitch);

    // Process and mix both players
    void process_players(
        sc1000* engine,
        audio_capture* capture,
        void* playback,
        int channels,
        unsigned long frames);
};

//
// Extern template declarations to prevent multiple instantiations
// Actual instantiations are in audio_engine.cpp
//

// Cubic interpolation variants
extern template class AudioEngine<CubicInterpolation, FormatS16>;
extern template class AudioEngine<CubicInterpolation, FormatS24_3LE>;
extern template class AudioEngine<CubicInterpolation, FormatS24_LE>;
extern template class AudioEngine<CubicInterpolation, FormatS32>;
extern template class AudioEngine<CubicInterpolation, FormatFloat>;

// Sinc interpolation variants
extern template class AudioEngine<SincInterpolation, FormatS16>;
extern template class AudioEngine<SincInterpolation, FormatS24_3LE>;
extern template class AudioEngine<SincInterpolation, FormatS24_LE>;
extern template class AudioEngine<SincInterpolation, FormatS32>;
extern template class AudioEngine<SincInterpolation, FormatFloat>;

} // namespace audio
} // namespace sc

#endif // __cplusplus
