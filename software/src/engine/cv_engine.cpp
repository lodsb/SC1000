//
// CV Engine - generates control voltage outputs from turntable inputs
//

#include "cv_engine.h"
#include <cmath>
#include <cstring>

namespace sc {
namespace cv {

// Scale factors for 16-bit output
constexpr float BIPOLAR_SCALE = 32767.0f;   // -32768 to +32767
constexpr float UNIPOLAR_SCALE = 32767.0f;  // 0 to 32767
constexpr int16_t GATE_HIGH = 32767;
constexpr int16_t GATE_LOW = 0;

// Encoder resolution (12-bit)
constexpr float ENCODER_SCALE = 1.0f / 4096.0f;

// Calculate lowpass filter coefficient for given cutoff frequency
static float calc_lowpass_alpha(int sample_rate, float cutoff_hz)
{
    // alpha = 1 - e^(-2*pi*fc/fs)
    float omega = 2.0f * static_cast<float>(M_PI) * cutoff_hz / static_cast<float>(sample_rate);
    return 1.0f - expf(-omega);
}

// Find which hardware channel has a given output type
static int find_channel(struct audio_interface* iface, output_channel_type type)
{
    for (int i = 0; i < MAX_OUTPUT_CHANNELS; i++)
    {
        if (iface->output_map[i] == type)
        {
            return i;
        }
    }
    return -1;
}

// Clamp float to range
static inline float clamp(float val, float min_val, float max_val)
{
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

} // namespace cv
} // namespace sc

void cv_engine_init(struct cv_state* state, int sample_rate)
{
    memset(state, 0, sizeof(*state));

    state->sample_rate = sample_rate;
    state->filter.alpha = sc::cv::calc_lowpass_alpha(sample_rate, CV_DEFAULT_CUTOFF_HZ);

    // Compute pulse duration in samples
    state->trigger.pulse_duration = static_cast<int>(
        CV_PULSE_DURATION_MS * static_cast<float>(sample_rate) / 1000.0f
    );

    // Initialize all channel indices to "not mapped"
    state->channels.platter_speed = -1;
    state->channels.platter_angle = -1;
    state->channels.platter_accel = -1;
    state->channels.sample_position = -1;
    state->channels.crossfader = -1;
    state->channels.gate_a = -1;
    state->channels.gate_b = -1;
    state->channels.direction_pulse = -1;
}

void cv_engine_set_mapping(struct cv_state* state, struct audio_interface* iface)
{
    using namespace sc::cv;

    if (!iface || !iface->supports_cv)
    {
        state->channels.platter_speed = -1;
        state->channels.platter_angle = -1;
        state->channels.platter_accel = -1;
        state->channels.sample_position = -1;
        state->channels.crossfader = -1;
        state->channels.gate_a = -1;
        state->channels.gate_b = -1;
        state->channels.direction_pulse = -1;
        return;
    }

    // Cache channel mappings - looked up once, used every block
    state->channels.platter_speed = find_channel(iface, OUT_CV_PLATTER_SPEED);
    state->channels.platter_angle = find_channel(iface, OUT_CV_PLATTER_ANGLE);
    state->channels.platter_accel = find_channel(iface, OUT_CV_PLATTER_ACCEL);
    state->channels.sample_position = find_channel(iface, OUT_CV_SAMPLE_POSITION);
    state->channels.crossfader = find_channel(iface, OUT_CV_CROSSFADER);
    state->channels.gate_a = find_channel(iface, OUT_CV_GATE_A);
    state->channels.gate_b = find_channel(iface, OUT_CV_GATE_B);
    state->channels.direction_pulse = find_channel(iface, OUT_CV_DIRECTION_PULSE);
}

void cv_engine_update(struct cv_state* state, const struct cv_controller_input* input)
{
    using namespace sc::cv;

    // Store previous speed for acceleration calculation
    float prev_speed = state->platter.speed_raw;

    //
    // Process platter inputs
    //

    // Speed: clamp raw pitch to -1..+1 range
    float speed = clamp(static_cast<float>(input->pitch), -1.0f, 1.0f);
    state->platter.speed_raw = speed;

    // Angle: normalize 12-bit encoder to 0..1 (saw wave)
    state->platter.angle = static_cast<float>(input->encoder_angle) * ENCODER_SCALE;

    // Acceleration: rate of speed change, scaled and clamped
    float accel = (speed - prev_speed) * CV_ACCEL_SCALE;
    state->platter.acceleration = clamp(accel, -1.0f, 1.0f);

    // Direction: -1/0/+1 with hysteresis threshold
    int direction = 0;
    if (speed > CV_DIRECTION_THRESHOLD) direction = 1;
    else if (speed < -CV_DIRECTION_THRESHOLD) direction = -1;
    state->platter.direction = direction;

    // Direction change pulse detection
    if (state->trigger.prev_direction != 0 && direction != 0 &&
        state->trigger.prev_direction != direction)
    {
        // Start a new pulse
        state->trigger.pulse_countdown = state->trigger.pulse_duration;
    }
    state->trigger.prev_direction = direction;

    //
    // Process sample position
    //

    if (input->sample_length > 0)
    {
        float pos = static_cast<float>(input->sample_position) /
                    static_cast<float>(input->sample_length);
        state->sample.position = clamp(pos, 0.0f, 1.0f);
    }
    else
    {
        state->sample.position = 0.0f;
    }

    //
    // Process fader/crossfader
    //

    // Crossfader CV uses smoothed fader_volume
    state->fader.position = clamp(static_cast<float>(input->fader_volume), 0.0f, 1.0f);

    // Gates use instant fader_target for quick response
    // Gate A: scratch is open when fader lets scratch through
    state->fader.scratch_open = (input->fader_target > CV_GATE_OPEN_THRESHOLD) ? 1 : 0;

    // Gate B: beat is open when scratch is cut (inverted crossfader logic)
    state->fader.beat_open = (input->fader_target < CV_GATE_CLOSE_THRESHOLD) ? 1 : 0;
}

void cv_engine_process(
    struct cv_state* state,
    int16_t* buffer,
    int num_channels,
    unsigned long frames)
{
    using namespace sc::cv;

    // Get cached channel indices
    const int ch_speed = state->channels.platter_speed;
    const int ch_angle = state->channels.platter_angle;
    const int ch_accel = state->channels.platter_accel;
    const int ch_position = state->channels.sample_position;
    const int ch_crossfader = state->channels.crossfader;
    const int ch_gate_a = state->channels.gate_a;
    const int ch_gate_b = state->channels.gate_b;
    const int ch_pulse = state->channels.direction_pulse;

    // Early out if nothing is mapped
    if (ch_speed < 0 && ch_angle < 0 && ch_accel < 0 && ch_position < 0 &&
        ch_crossfader < 0 && ch_gate_a < 0 && ch_gate_b < 0 && ch_pulse < 0)
        return;

    // Pre-compute constant values for this block
    const int16_t gate_a = state->fader.scratch_open ? GATE_HIGH : GATE_LOW;
    const int16_t gate_b = state->fader.beat_open ? GATE_HIGH : GATE_LOW;
    const int16_t angle_out = static_cast<int16_t>(state->platter.angle * UNIPOLAR_SCALE);
    const int16_t accel_out = static_cast<int16_t>(state->platter.acceleration * BIPOLAR_SCALE);
    const int16_t position_out = static_cast<int16_t>(state->sample.position * UNIPOLAR_SCALE);
    const int16_t crossfader_out = static_cast<int16_t>(state->fader.position * UNIPOLAR_SCALE);

    // Filter state
    const float alpha = state->filter.alpha;
    const float one_minus_alpha = 1.0f - alpha;
    const float target_speed = state->platter.speed_raw;
    float filt_speed = state->filter.speed_filtered;

    // Pulse countdown
    int pulse_countdown = state->trigger.pulse_countdown;

    // Per-sample processing
    for (unsigned long i = 0; i < frames; i++)
    {
        unsigned long base = i * static_cast<unsigned long>(num_channels);

        // Apply lowpass filter to platter speed
        filt_speed = alpha * target_speed + one_minus_alpha * filt_speed;

        // Write CV outputs
        if (ch_speed >= 0)
            buffer[base + ch_speed] = static_cast<int16_t>(filt_speed * BIPOLAR_SCALE);

        if (ch_angle >= 0)
            buffer[base + ch_angle] = angle_out;

        if (ch_accel >= 0)
            buffer[base + ch_accel] = accel_out;

        if (ch_position >= 0)
            buffer[base + ch_position] = position_out;

        if (ch_crossfader >= 0)
            buffer[base + ch_crossfader] = crossfader_out;

        if (ch_gate_a >= 0)
            buffer[base + ch_gate_a] = gate_a;

        if (ch_gate_b >= 0)
            buffer[base + ch_gate_b] = gate_b;

        if (ch_pulse >= 0)
        {
            buffer[base + ch_pulse] = (pulse_countdown > 0) ? GATE_HIGH : GATE_LOW;
            if (pulse_countdown > 0) pulse_countdown--;
        }
    }

    // Store state for next block
    state->filter.speed_filtered = filt_speed;
    state->platter.speed = filt_speed;  // Store filtered speed
    state->trigger.pulse_countdown = pulse_countdown;
}
