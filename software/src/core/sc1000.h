#pragma once

#include "../player/deck.h"
#include "../platform/crossfader.h"
#include "sc_input.h"
#include <vector>
#include <memory>

struct sc_settings;

//
// Abstract audio hardware interface
// Platform implementations (ALSA, etc.) inherit from this
//
class AudioHardware {
public:
    virtual ~AudioHardware() = default;

    virtual ssize_t pollfds(struct pollfd* pe, size_t z) = 0;
    virtual int handle() = 0;
    virtual unsigned int sample_rate() const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;

    // Recording control (delegated to audio engine internally)
    virtual bool start_recording(int deck, double playback_position) = 0;
    virtual void stop_recording(int deck) = 0;
    virtual bool is_recording(int deck) const = 0;
    virtual bool has_loop(int deck) const = 0;
    virtual bool has_capture() const = 0;
    virtual void reset_loop(int deck) = 0;
    virtual struct track* get_loop_track(int deck) = 0;
    virtual struct track* peek_loop_track(int deck) = 0;
};

struct sc1000
{
    struct deck scratch_deck;
    struct deck beat_deck;

    std::unique_ptr<sc_settings> settings;
    std::vector<mapping> mappings;

    // Crossfader input (handles ADC conversion and calibration)
    Crossfader crossfader;

    // Audio hardware (ALSA implementation)
    std::unique_ptr<AudioHardware> audio;
    bool fault = false;
};

// Setup and lifecycle
void sc1000_setup(sc1000* engine, struct rt* rt, const char* root_path);
void sc1000_load_sample_folders(sc1000* engine);
void sc1000_clear(sc1000* engine);

// Audio hardware control (delegates to AudioHardware interface)
void sc1000_audio_start(sc1000* engine);
void sc1000_audio_stop(sc1000* engine);
void sc1000_handle_deck_recording(sc1000* engine);
ssize_t sc1000_audio_pollfds(sc1000* engine, struct pollfd* pe, size_t z);
void sc1000_audio_handle(sc1000* engine);