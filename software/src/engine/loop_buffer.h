//
// Loop Buffer - records audio input into memory for immediate scratching
//

#pragma once

#include <stdbool.h>
#include <stdint.h>

struct track;

//
// Loop Buffer State
//
struct loop_buffer {
    struct track* track;          // Underlying track with block storage
    unsigned int write_pos;       // Current write position (samples)
    unsigned int max_samples;     // Maximum recording length (samples)
    int sample_rate;              // Sample rate (48000)
    bool recording;               // Currently recording?
    bool max_reached;             // Hit max length during recording?
};

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

// Initialize loop buffer with sample rate and max recording time
EXTERNC void loop_buffer_init(struct loop_buffer* lb, int sample_rate, int max_seconds);

// Clear loop buffer (release track, reset state)
EXTERNC void loop_buffer_clear(struct loop_buffer* lb);

// Start recording - creates new track, resets position
// Returns true on success, false if already recording or allocation failed
EXTERNC bool loop_buffer_start(struct loop_buffer* lb);

// Stop recording - finalizes track length
EXTERNC void loop_buffer_stop(struct loop_buffer* lb);

// Write audio frames to buffer (call from capture callback)
// Returns number of frames written (may be less than requested if max reached)
EXTERNC unsigned int loop_buffer_write(struct loop_buffer* lb,
                                       const int16_t* pcm,
                                       unsigned int frames,
                                       int num_channels,
                                       int left_channel,
                                       int right_channel);

// Get the recorded track (acquires reference - caller must release)
// Returns NULL if no recording exists
EXTERNC struct track* loop_buffer_get_track(struct loop_buffer* lb);

// Check if recording is active
EXTERNC bool loop_buffer_is_recording(struct loop_buffer* lb);

// Get current recording length in samples
EXTERNC unsigned int loop_buffer_get_length(struct loop_buffer* lb);

#undef EXTERNC
