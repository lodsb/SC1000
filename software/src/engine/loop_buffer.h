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
    unsigned int loop_length;     // Defined loop length (set after first recording)
    int sample_rate;              // Sample rate (48000)
    bool recording;               // Currently recording?
    bool length_locked;           // Loop length defined (first recording complete)?
    bool max_reached;             // Hit max length during recording?
};

// Initialize loop buffer with sample rate and max recording time
void loop_buffer_init(struct loop_buffer* lb, int sample_rate, int max_seconds);

// Clear loop buffer (release track, reset state)
void loop_buffer_clear(struct loop_buffer* lb);

// Start recording - creates new track, resets position
// Returns true on success, false if already recording or allocation failed
bool loop_buffer_start(struct loop_buffer* lb);

// Stop recording - finalizes track length
void loop_buffer_stop(struct loop_buffer* lb);

// Write audio frames to buffer (call from capture callback)
// Returns number of frames written (may be less than requested if max reached)
unsigned int loop_buffer_write(struct loop_buffer* lb,
                               const int16_t* pcm,
                               unsigned int frames,
                               int num_channels,
                               int left_channel,
                               int right_channel);

// Get the recorded track (acquires reference - caller must release)
// Returns NULL if no recording exists
struct track* loop_buffer_get_track(struct loop_buffer* lb);

// Check if recording is active
bool loop_buffer_is_recording(struct loop_buffer* lb);

// Check if loop length is defined (first recording complete)
bool loop_buffer_has_loop(struct loop_buffer* lb);

// Get current recording length in samples
unsigned int loop_buffer_get_length(struct loop_buffer* lb);

// Reset/erase the loop (clears track and unlocks length for fresh recording)
void loop_buffer_reset(struct loop_buffer* lb);
