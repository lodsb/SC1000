#pragma once

#include "../core/sc_settings.h"
#include <stdint.h>

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

//
// CV Engine Configuration
// These could be moved to sc_settings.json if runtime configurability is needed
//

// Gate threshold (fader position where gates trigger)
#define CV_GATE_OPEN_THRESHOLD   0.01f   // Gate opens above this (same for both sides)

// Direction detection threshold (avoid triggering on noise near zero)
#define CV_DIRECTION_THRESHOLD   0.05f

// Pulse duration for direction change trigger (milliseconds)
#define CV_PULSE_DURATION_MS     2.0f

// Acceleration scaling factor (maps speed delta to -1..+1 range)
#define CV_ACCEL_SCALE          10.0f

// Default lowpass filter cutoff for platter speed smoothing (Hz)
#define CV_DEFAULT_CUTOFF_HZ    500.0f

//
// CV State Structure
//

struct cv_state {
    // Processed CV values (ready for output)
    struct {
        float speed;          // -1.0 to +1.0 (clamped, filtered)
        float speed_raw;      // Unfiltered speed for acceleration calc
        float angle;          // 0.0 to 1.0 (saw wave from encoder)
        float acceleration;   // -1.0 to +1.0 (rate of speed change)
        int direction;        // -1 = backward, 0 = stopped, +1 = forward
    } platter;

    struct {
        float position;       // 0.0 to 1.0 (relative position in track)
    } sample;

    struct {
        float position;       // 0.0 to 1.0 (crossfader position)
        int scratch_open;     // 1 when scratch side is audible
        int beat_open;        // 1 when beat side is audible
    } fader;

    // Filter state
    struct {
        float speed_filtered; // Lowpass filtered platter speed
        float alpha;          // Filter coefficient
    } filter;

    // Pulse/trigger state
    struct {
        int prev_direction;       // For edge detection
        int pulse_countdown;      // Samples remaining in current pulse
        int pulse_duration;       // Pulse length in samples (computed at init)
    } trigger;

    // Runtime config
    int sample_rate;

    // Cached channel indices (-1 = not mapped)
    struct {
        int platter_speed;
        int platter_angle;
        int platter_accel;
        int sample_position;
        int crossfader;
        int gate_a;
        int gate_b;
        int direction_pulse;
    } channels;
};

//
// Raw controller input (passed from audio thread)
//
struct cv_controller_input {
    // Platter
    double pitch;              // Raw pitch value (can be any value, will be clamped)
    int encoder_angle;         // Raw 12-bit encoder (0-4095)

    // Sample
    double sample_position;    // Current sample position
    unsigned int sample_length; // Track length (0 if no track)

    // Fader
    double fader_volume;       // Smoothed fader position (for crossfader CV)
    double fader_target;       // Instant fader position (for gates)
};

//
// API Functions
//

// Initialize CV engine
EXTERNC void cv_engine_init(struct cv_state* state, int sample_rate);

// Cache channel mappings from audio interface (call once after init)
EXTERNC void cv_engine_set_mapping(struct cv_state* state, struct audio_interface* iface);

// Update all CV inputs from controller state (call once per audio block)
EXTERNC void cv_engine_update(struct cv_state* state, const struct cv_controller_input* input);

// Process one block - generates CV output samples
EXTERNC void cv_engine_process(
    struct cv_state* state,
    int16_t* buffer,
    int num_channels,
    unsigned long frames
);

#undef EXTERNC
