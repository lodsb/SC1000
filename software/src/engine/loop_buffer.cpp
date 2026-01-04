//
// Loop Buffer - records audio input into memory for immediate scratching
//
// Workflow:
// 1. First RECORD: captures audio, defines loop length when stopped
// 2. Subsequent RECORDs: punch-in mode, overwrites circularly from current position
// 3. Long-hold RECORD: resets/erases, next RECORD starts fresh
//

#include "loop_buffer.h"
#include "../player/track.h"
#include <cstring>
#include <cstdio>

void loop_buffer_init(struct loop_buffer* lb, int sample_rate, int max_seconds)
{
    lb->write_pos = 0;
    lb->max_samples = static_cast<unsigned int>(sample_rate * max_seconds);
    lb->loop_length = 0;
    lb->sample_rate = sample_rate;
    lb->recording = false;
    lb->length_locked = false;
    lb->max_reached = false;

    // Pre-allocate track with full capacity to avoid RT allocation
    lb->track = track_acquire_for_recording(sample_rate);
    if (lb->track)
    {
        // Pre-allocate all the space we'll need
        if (track_ensure_space(lb->track, lb->max_samples) < 0)
        {
            fprintf(stderr, "loop_buffer: failed to pre-allocate %u samples\n", lb->max_samples);
            track_release(lb->track);
            lb->track = nullptr;
        }
        else
        {
            printf("loop_buffer: pre-allocated %u samples (%.1f sec)\n",
                   lb->max_samples, static_cast<float>(lb->max_samples) / sample_rate);
        }
    }
    else
    {
        fprintf(stderr, "loop_buffer: failed to create track\n");
    }
}

void loop_buffer_clear(struct loop_buffer* lb)
{
    if (lb->track)
    {
        track_release(lb->track);
        lb->track = nullptr;
    }
    lb->write_pos = 0;
    lb->loop_length = 0;
    lb->recording = false;
    lb->length_locked = false;
    lb->max_reached = false;
}

bool loop_buffer_start(struct loop_buffer* lb)
{
    if (lb->recording)
    {
        return false;  // Already recording
    }

    if (!lb->track)
    {
        fprintf(stderr, "loop_buffer: no track available (not pre-allocated)\n");
        return false;
    }

    if (lb->length_locked)
    {
        // Punch-in mode: keep existing data, start overwriting from current write_pos
        lb->recording = true;
        lb->max_reached = false;
        printf("loop_buffer: punch-in recording started at pos %u (loop length %u samples, %.2f sec)\n",
               lb->write_pos, lb->loop_length,
               static_cast<float>(lb->loop_length) / lb->sample_rate);
        return true;
    }

    // Fresh recording: reset state but reuse pre-allocated track
    lb->write_pos = 0;
    lb->loop_length = 0;
    lb->length_locked = false;
    lb->max_reached = false;
    lb->recording = true;

    printf("loop_buffer: fresh recording started (max %u samples)\n", lb->max_samples);
    return true;
}

void loop_buffer_stop(struct loop_buffer* lb)
{
    if (!lb->recording)
    {
        return;
    }

    lb->recording = false;

    if (!lb->length_locked)
    {
        // First recording complete - lock the loop length
        if (lb->track && lb->write_pos > 0)
        {
            lb->loop_length = lb->write_pos;
            lb->length_locked = true;
            track_set_length(lb->track, lb->loop_length);
            printf("loop_buffer: loop defined, %u samples (%.2f sec)\n",
                   lb->loop_length, static_cast<float>(lb->loop_length) / lb->sample_rate);
        }
        else
        {
            printf("loop_buffer: recording stopped (empty)\n");
        }
    }
    else
    {
        // Punch-in complete
        printf("loop_buffer: punch-in stopped at pos %u\n", lb->write_pos);
    }
}

unsigned int loop_buffer_write(struct loop_buffer* lb,
                               const int16_t* pcm,
                               unsigned int frames,
                               int num_channels,
                               int left_channel,
                               int right_channel)
{
    if (!lb->recording || !lb->track)
    {
        return 0;
    }

    unsigned int to_write = frames;
    unsigned int written = 0;

    if (lb->length_locked)
    {
        // Punch-in mode: write circularly within loop_length
        if (lb->loop_length == 0)
        {
            return 0;  // Shouldn't happen, but safety check
        }

        for (unsigned int i = 0; i < to_write; i++)
        {
            unsigned int pos = lb->write_pos % lb->loop_length;
            signed short* dest = lb->track->get_sample(static_cast<int>(pos));
            const int16_t* src = &pcm[i * num_channels];

            dest[0] = src[left_channel];   // Left
            dest[1] = src[right_channel];  // Right

            lb->write_pos++;
            written++;
        }
        // Wrap write_pos to avoid overflow over time
        lb->write_pos = lb->write_pos % lb->loop_length;
    }
    else
    {
        // Fresh recording: linear write until max (space is pre-allocated)
        unsigned int remaining = lb->max_samples - lb->write_pos;
        if (remaining == 0)
        {
            if (!lb->max_reached)
            {
                lb->max_reached = true;
                printf("loop_buffer: max length reached\n");
            }
            return 0;
        }

        to_write = (frames < remaining) ? frames : remaining;

        // Write samples to track (space is pre-allocated, no RT allocation)
        for (unsigned int i = 0; i < to_write; i++)
        {
            signed short* dest = lb->track->get_sample(static_cast<int>(lb->write_pos + i));
            const int16_t* src = &pcm[i * num_channels];

            dest[0] = src[left_channel];   // Left
            dest[1] = src[right_channel];  // Right
        }

        lb->write_pos += to_write;
        written = to_write;

        // Update track length as we go (allows scratching while recording)
        track_set_length(lb->track, lb->write_pos);
    }

    return written;
}

struct track* loop_buffer_get_track(struct loop_buffer* lb)
{
    if (!lb->track)
    {
        return nullptr;
    }

    unsigned int len = lb->length_locked ? lb->loop_length : lb->write_pos;
    if (len == 0)
    {
        return nullptr;
    }

    // Acquire a reference for the caller
    track_acquire(lb->track);
    return lb->track;
}

bool loop_buffer_is_recording(struct loop_buffer* lb)
{
    return lb->recording;
}

bool loop_buffer_has_loop(struct loop_buffer* lb)
{
    return lb->length_locked && lb->loop_length > 0;
}

unsigned int loop_buffer_get_length(struct loop_buffer* lb)
{
    return lb->length_locked ? lb->loop_length : lb->write_pos;
}

void loop_buffer_reset(struct loop_buffer* lb)
{
    // Stop recording if active
    if (lb->recording)
    {
        lb->recording = false;
    }

    // Reset state but keep pre-allocated track
    lb->write_pos = 0;
    lb->loop_length = 0;
    lb->length_locked = false;
    lb->max_reached = false;

    printf("loop_buffer: reset/erased\n");
}

void loop_buffer_set_position(struct loop_buffer* lb, unsigned int position_samples)
{
    if (!lb->length_locked || lb->loop_length == 0)
    {
        // No loop defined, can't set position
        return;
    }

    // Clamp to valid range
    lb->write_pos = position_samples % lb->loop_length;
}
