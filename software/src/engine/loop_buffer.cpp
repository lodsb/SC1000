//
// Loop Buffer - records audio input into memory for immediate scratching
//

#include "loop_buffer.h"
#include "../player/track.h"
#include <cstring>
#include <cstdio>

void loop_buffer_init(struct loop_buffer* lb, int sample_rate, int max_seconds)
{
    lb->track = nullptr;
    lb->write_pos = 0;
    lb->max_samples = static_cast<unsigned int>(sample_rate * max_seconds);
    lb->sample_rate = sample_rate;
    lb->recording = false;
    lb->max_reached = false;
}

void loop_buffer_clear(struct loop_buffer* lb)
{
    if (lb->track)
    {
        track_release(lb->track);
        lb->track = nullptr;
    }
    lb->write_pos = 0;
    lb->recording = false;
    lb->max_reached = false;
}

bool loop_buffer_start(struct loop_buffer* lb)
{
    if (lb->recording)
    {
        return false;  // Already recording
    }

    // Release any existing track
    if (lb->track)
    {
        track_release(lb->track);
        lb->track = nullptr;
    }

    // Create new track for recording
    lb->track = track_acquire_for_recording(lb->sample_rate);
    if (!lb->track)
    {
        fprintf(stderr, "loop_buffer: failed to allocate track\n");
        return false;
    }

    lb->write_pos = 0;
    lb->max_reached = false;
    lb->recording = true;

    printf("loop_buffer: recording started (max %u samples)\n", lb->max_samples);
    return true;
}

void loop_buffer_stop(struct loop_buffer* lb)
{
    if (!lb->recording)
    {
        return;
    }

    lb->recording = false;

    // Finalize track length
    if (lb->track && lb->write_pos > 0)
    {
        track_set_length(lb->track, lb->write_pos);
        printf("loop_buffer: recording stopped, %u samples (%.2f sec)\n",
               lb->write_pos, static_cast<float>(lb->write_pos) / lb->sample_rate);
    }
    else
    {
        printf("loop_buffer: recording stopped (empty)\n");
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

    // Calculate how many frames we can write
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

    unsigned int to_write = (frames < remaining) ? frames : remaining;

    // Ensure we have space in track
    if (track_ensure_space(lb->track, lb->write_pos + to_write) < 0)
    {
        fprintf(stderr, "loop_buffer: failed to allocate space\n");
        lb->max_reached = true;
        return 0;
    }

    // Write samples to track
    // Input is interleaved multi-channel, we extract left/right channels
    for (unsigned int i = 0; i < to_write; i++)
    {
        signed short* dest = track_get_sample(lb->track, static_cast<int>(lb->write_pos + i));
        const int16_t* src = &pcm[i * num_channels];

        dest[0] = src[left_channel];   // Left
        dest[1] = src[right_channel];  // Right
    }

    lb->write_pos += to_write;

    // Update track length as we go (allows scratching while recording)
    track_set_length(lb->track, lb->write_pos);

    return to_write;
}

struct track* loop_buffer_get_track(struct loop_buffer* lb)
{
    if (!lb->track || lb->write_pos == 0)
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

unsigned int loop_buffer_get_length(struct loop_buffer* lb)
{
    return lb->write_pos;
}
