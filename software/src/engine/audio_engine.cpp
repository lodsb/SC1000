#include <iostream>
#include <cmath>
#include <climits>
#include <ctime>
#include <cstring>

#include "audio_engine.h"
#include "loop_buffer.h"
#include "../core/sc_settings.h"
#include "../core/sc1000.h"
#include "../platform/alsa.h"

#include "../player/track.h"
#include "../player/deck.h"

// Interpolation headers (optimized versions with direct track access)
#include "../dsp/sinc_interpolate_opt.h"
#include "../dsp/cubic_interpolate_opt.h"

// ARM NEON intrinsics for explicit SIMD
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define USE_NEON 1
#else
#define USE_NEON 0
#endif

namespace sc {
namespace audio {

// Global interpolation mode (default to sinc for better quality)
static interpolation_mode_t g_interpolation_mode = INTERP_SINC;

// DSP performance tracking
static struct {
    double load_percent;
    double load_peak;
    double process_time_us;
    double budget_time_us;
    unsigned long xruns;
    // For averaging
    double load_accumulator;
    unsigned int load_samples;
} g_dsp_stats = {};

static inline double get_time_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) * 1000000.0 + static_cast<double>(ts.tv_nsec) / 1000.0;
}

// Time in seconds fader takes to decay
constexpr double FADER_DECAY_TIME = 0.020;
constexpr double DECAY_SAMPLES = FADER_DECAY_TIME * 48000;

// The base volume level. A value of 1.0 leaves no headroom to play
// louder when the record is going faster than 1.0.
constexpr double BASE_VOLUME = 7.0 / 8.0;

#if USE_NEON
// NEON-optimized Catmull-Rom spline interpolation for two stereo samples
// Input: t0-t3 are the 4 sample windows {L1, R1, L2, R2} for each position
// mu1, mu2 are the fractional positions for player 1 and 2
// Returns interpolated {L1, R1, L2, R2}
static inline float32x4_t neon_interpolate_cubic(
    float32x4_t t0, float32x4_t t1, float32x4_t t2, float32x4_t t3,
    float mu1, float mu2)
{
    // Build mu vector {mu1, mu1, mu2, mu2}
    float mu_arr[4] = {mu1, mu1, mu2, mu2};
    float32x4_t mu = vld1q_f32(mu_arr);
    float32x4_t mu2_vec = vmulq_f32(mu, mu);        // mu^2
    float32x4_t mu3_vec = vmulq_f32(mu2_vec, mu);   // mu^3

    // Catmull-Rom spline coefficients (smoother than simple cubic):
    // a0 = 0.5 * (-t0 + 3*t1 - 3*t2 + t3)
    // a1 = 0.5 * (2*t0 - 5*t1 + 4*t2 - t3)
    // a2 = 0.5 * (-t0 + t2)
    // a3 = t1
    // result = a0*mu^3 + a1*mu^2 + a2*mu + a3

    float32x4_t half = vdupq_n_f32(0.5f);
    float32x4_t two = vdupq_n_f32(2.0f);
    float32x4_t three = vdupq_n_f32(3.0f);
    float32x4_t four = vdupq_n_f32(4.0f);
    float32x4_t five = vdupq_n_f32(5.0f);

    // a0 = 0.5 * (-t0 + 3*t1 - 3*t2 + t3)
    float32x4_t a0 = vmulq_f32(half,
        vaddq_f32(vsubq_f32(vsubq_f32(vmulq_f32(three, t1), vmulq_f32(three, t2)), t0), t3));

    // a1 = 0.5 * (2*t0 - 5*t1 + 4*t2 - t3)
    float32x4_t a1 = vmulq_f32(half,
        vsubq_f32(vaddq_f32(vsubq_f32(vmulq_f32(two, t0), vmulq_f32(five, t1)), vmulq_f32(four, t2)), t3));

    // a2 = 0.5 * (-t0 + t2)
    float32x4_t a2 = vmulq_f32(half, vsubq_f32(t2, t0));

    // a3 = t1
    float32x4_t a3 = t1;

    // result = a0*mu^3 + a1*mu^2 + a2*mu + a3
    float32x4_t result = vmlaq_f32(a3, a2, mu);           // a2*mu + a3
    result = vmlaq_f32(result, a1, mu2_vec);              // a1*mu^2 + (a2*mu + a3)
    result = vmlaq_f32(result, a0, mu3_vec);              // a0*mu^3 + ...

    return result;
}

// NEON clamp to int16 range
static inline float32x4_t neon_clamp_s16(float32x4_t v) {
    const float32x4_t min_val = vdupq_n_f32(-32768.0f);
    const float32x4_t max_val = vdupq_n_f32(32767.0f);
    return vminq_f32(vmaxq_f32(v, min_val), max_val);
}
#endif

// Fallback vector types for non-NEON builds
using v4sf = float __attribute__ ((vector_size (16)));
using v4si = int   __attribute__ ((vector_size (16)));
using v2sf = float __attribute__ ((vector_size (8)));
using v2si = int   __attribute__ ((vector_size (8)));

static bool nearly_equal( double val1, double val2, double tolerance )
{
   return std::fabs(val1 - val2) < tolerance;
}

inline static v4sf interpolate_two_stereo_samples( v4sf t0, v4sf t1, v4sf t2, v4sf t3, float tsp1, float tsp2 )
{
   //
   // Catmull-Rom spline interpolation (smoother than simple cubic polynomial)
   // Interpolates between t1 and t2 using t0 and t3 for tangent estimation
   //

   v4sf mu = {tsp1, tsp1, tsp2, tsp2};
   v4sf mu2 = mu * mu;
   v4sf mu3 = mu2 * mu;
   v4sf half = {0.5f, 0.5f, 0.5f, 0.5f};
   v4sf two = {2.0f, 2.0f, 2.0f, 2.0f};
   v4sf three = {3.0f, 3.0f, 3.0f, 3.0f};
   v4sf four = {4.0f, 4.0f, 4.0f, 4.0f};
   v4sf five = {5.0f, 5.0f, 5.0f, 5.0f};

   // Catmull-Rom coefficients:
   // a0 = 0.5 * (-t0 + 3*t1 - 3*t2 + t3)
   // a1 = 0.5 * (2*t0 - 5*t1 + 4*t2 - t3)
   // a2 = 0.5 * (-t0 + t2)
   // a3 = t1
   v4sf a0 = half * (-t0 + three * t1 - three * t2 + t3);
   v4sf a1 = half * (two * t0 - five * t1 + four * t2 - t3);
   v4sf a2 = half * (-t0 + t2);
   v4sf a3 = t1;

   // result = a0*mu^3 + a1*mu^2 + a2*mu + a3
   v4sf interpol = mu3 * a0 + mu2 * a1 + mu * a2 + a3;
   return interpol;
}

inline static void
collect_track_samples( track* tr_1, double sample_1, double tr_1_len, double& subpos_1, float& f1l1, float& f2l1,
                       float& f3l1, float& f4l1, float& f1r1, float& f2r1, float& f3r1, float& f4r1 )
{
   /* 4-sample window for interpolation */
   int sa = ( int ) sample_1;
   if ( sample_1 < 0.0 )
   {
      sa--;
   }

   subpos_1 = sample_1 - sa;
   sa--;

   // wrap to track boundary, i.e. loop
   if ( tr_1_len != 0 )
   {
      sa = sa % ( int ) tr_1_len;
      // Actually don't let people go to minus numbers
      // as entire track might not be loaded yet
      //if (sa < 0) sa += tr->length;
   }

   signed short* ts;
   if ( sa < 0 || sa >= tr_1_len )
   {
      f1l1 = 0;
      f1r1 = 0;
   }
   else
   {
      ts = tr_1->get_sample(sa);
      f1l1 = ts[ 0 ];
      f1r1 = ts[ 1 ];
   }

   sa++;
   if ( sa < 0 || sa >= tr_1_len )
   {
      f2l1 = 0;
      f2r1 = 0;
   }
   else
   {
      ts = tr_1->get_sample(sa);
      f2l1 = ts[ 0 ];
      f2r1 = ts[ 1 ];
   }

   sa++;
   if ( sa < 0 || sa >= tr_1_len )
   {
      f3l1 = 0;
      f3r1 = 0;
   }
   else
   {
      ts = tr_1->get_sample(sa);
      f3l1 = ts[ 0 ];
      f3r1 = ts[ 1 ];
   }

   sa++;
   if ( sa < 0 || sa >= tr_1_len )
   {
      f4l1 = 0;
      f4r1 = 0;
   }
   else
   {
      ts = tr_1->get_sample(sa);
      f4l1 = ts[ 0 ];
      f4r1 = ts[ 1 ];
   }
}

inline static void
collect_track_samples_vectorized( track* tr_1, track* tr_2, double sample_1, double sample_2, double tr_1_len,
                                  double tr_2_len, double& subpos_1, double& subpos_2,
                                  v4sf& t0, v4sf& t1, v4sf& t2, v4sf& t3 )
{
   float f1l1;
   float f2l1;
   float f3l1;
   float f4l1;

   float f1r1;
   float f2r1;
   float f3r1;
   float f4r1;

   float f1l2;
   float f2l2;
   float f3l2;
   float f4l2;

   float f1r2;
   float f2r2;
   float f3r2;
   float f4r2;

   collect_track_samples(tr_1, sample_1, tr_1_len, subpos_1, f1l1, f2l1, f3l1, f4l1, f1r1, f2r1, f3r1, f4r1);
   collect_track_samples(tr_2, sample_2, tr_2_len, subpos_2, f1l2, f2l2, f3l2, f4l2, f1r2, f2r2, f3r2, f4r2);

   v4sf vt0= {f1l1, f1r1, f1l2, f1r2};
   v4sf vt1= {f2l1, f2r1, f2l2, f2r2};
   v4sf vt2= {f3l1, f3r1, f3l2, f3r2};
   v4sf vt3= {f4l1, f4r1, f4l2, f4r2};

   t0 = vt0;
   t1 = vt1;
   t2 = vt2;
   t3 = vt3;
}

// Collect samples for sinc interpolation (16 samples per channel)
// Returns fractional position and fills sample arrays
inline static void
collect_track_samples_sinc(track* tr, double sample_pos, double tr_len,
                           double& subpos, float* samples_l, float* samples_r)
{
   constexpr int NUM_TAPS = sc::dsp::SINC_NUM_TAPS;
   constexpr int HALF_TAPS = NUM_TAPS / 2;

   int sa = static_cast<int>(sample_pos);
   if (sample_pos < 0.0) sa--;

   subpos = sample_pos - sa;
   sa -= HALF_TAPS;  // Center the window

   for (int i = 0; i < NUM_TAPS; i++) {
      int idx = sa + i;

      // Wrap to track boundary
      if (tr_len != 0) {
         idx = idx % static_cast<int>(tr_len);
      }

      if (idx < 0 || idx >= static_cast<int>(tr_len)) {
         samples_l[i] = 0.0f;
         samples_r[i] = 0.0f;
      } else {
         signed short* ts = tr->get_sample(idx);
         samples_l[i] = static_cast<float>(ts[0]);
         samples_r[i] = static_cast<float>(ts[1]);
      }
   }
}

// Simple scalar clamp - the vector shuffle approach was buggy
static inline float clamp_scalar(float v, float min_val, float max_val) {
   if (v < min_val) return min_val;
   if (v > max_val) return max_val;
   return v;
}

inline void setup_player_for_block( struct player* pl, unsigned long samples, const struct sc_settings* settings,
                                    double* target_volume,
                                    double* filtered_pitch )
{
   double target_pitch, diff;

   auto samples_i = 1.0 / static_cast<double>(samples);

   //pl->target_position = (sin(((double) pl->samplesSoFar) / 20000) + 1); // Sine wave to simulate scratching, used for debugging

   // figure out motor speed
   if ( pl->stopped )
   {
      // Simulate braking
      if ( pl->motor_speed > 0.1 )
      {
         pl->motor_speed = pl->motor_speed - samples_i * (settings->brake_speed * 10);
      }
      else
      {
         pl->motor_speed = 0.0;
      }
   }
   else
   {
      // stack all the pitch bends on top of each other
      pl->motor_speed = pl->note_pitch * pl->fader_pitch * pl->bend_pitch;
   }

   // deal with case where we've released the platter
   if ( pl->just_play || // platter is always released on beat deck
        (
                !pl->cap_touch &&
                !pl->cap_touch_old // don't do it on the first iteration so we pick up backspins
        )
           )
   {
      if ( pl->pitch > 20.0 )
      { pl->pitch = 20.0; }
      if ( pl->pitch < -20.0 )
      { pl->pitch = -20.0; }
      // Simulate slipmat for lasers/phasers
      if ( pl->pitch < pl->motor_speed - 0.1 )
      {
         target_pitch = pl->pitch + samples_i * settings->slippiness;
      }
      else if ( pl->pitch > pl->motor_speed + 0.1 )
      {
         target_pitch = pl->pitch - samples_i * settings->slippiness;
      }
      else
      {
         target_pitch = pl->motor_speed;
      }
   }
   else
   {
      diff = pl->position - pl->target_position;

      target_pitch = (-diff) * 40;
   }
   pl->cap_touch_old = pl->cap_touch;

   (*filtered_pitch) = (0.1 * target_pitch) + (0.9 * pl->pitch);

   double vol_decay_amount = samples_i * DECAY_SAMPLES;

   if ( nearly_equal(pl->fader_target, pl->fader_volume,
                     vol_decay_amount) )
   { // Make sure to set directly when we're nearly there to avoid oscillation
      pl->fader_volume = pl->fader_target;
   }
   else if ( pl->fader_target > pl->fader_volume )
   {
      pl->fader_volume += vol_decay_amount;
   }
   else
   {
      pl->fader_volume -= vol_decay_amount;
   }

   (*target_volume) = std::fabs(pl->pitch) * BASE_VOLUME * pl->fader_volume;

   if ( (*target_volume) > 1.0 )
   {
      (*target_volume) = 1.0;
   }
}

// Cubic interpolation version of process_add_players
// Uses optimized 4-tap cubic with:
// - Direct track memory access (avoids per-sample get_sample calls)
// - NEON SIMD for interpolation
static inline void process_add_players( signed short *pcm, unsigned samples,
                                        double sample_dt_1, struct track *tr_1, double position_1, float pitch_1, float end_pitch_1, float start_vol_1, float end_vol_1, double* r1,
                                        double sample_dt_2, struct track *tr_2, double position_2, float pitch_2, float end_pitch_2, float start_vol_2, float end_vol_2, double* r2 )
{
   const float ONE_OVER_SAMPLES = 1.0f / static_cast<float>(samples);

   const float volume_gradient_1 = (end_vol_1 - start_vol_1) * ONE_OVER_SAMPLES;
   const float pitch_gradient_1  = (end_pitch_1 - pitch_1)   * ONE_OVER_SAMPLES;
   const float volume_gradient_2 = (end_vol_2 - start_vol_2) * ONE_OVER_SAMPLES;
   const float pitch_gradient_2  = (end_pitch_2 - pitch_2)   * ONE_OVER_SAMPLES;

   const int tr_1_len = static_cast<int>(tr_1->length);
   const int tr_2_len = static_cast<int>(tr_2->length);

   const double tr_1_rate = tr_1->rate;
   const double tr_2_rate = tr_2->rate;

   const double dt_rate_1 = sample_dt_1 * tr_1_rate;
   const double dt_rate_2 = sample_dt_2 * tr_2_rate;

   double sample_1 = position_1 * tr_1->rate;
   double sample_2 = position_2 * tr_2->rate;

   float vol_1 = start_vol_1;
   float vol_2 = start_vol_2;

   for (unsigned s = 0; s < samples; s++)
   {
      double step_1 = dt_rate_1 * pitch_1;
      double step_2 = dt_rate_2 * pitch_2;

      // Optimized cubic interpolation with direct track access
      auto result = sc::dsp::cubic_interpolate_dual_deck_opt(
         tr_1, sample_1, tr_1_len,
         tr_2, sample_2, tr_2_len);

      // Apply volume and mix
      float sum_l = result.l1 * vol_1 + result.l2 * vol_2;
      float sum_r = result.r1 * vol_1 + result.r2 * vol_2;

      // Clamp to int16 range
      sum_l = clamp_scalar(sum_l, static_cast<float>(SHRT_MIN), static_cast<float>(SHRT_MAX));
      sum_r = clamp_scalar(sum_r, static_cast<float>(SHRT_MIN), static_cast<float>(SHRT_MAX));

      *pcm++ = static_cast<signed short>(sum_l);
      *pcm++ = static_cast<signed short>(sum_r);

      sample_1 += step_1;
      vol_1    += volume_gradient_1;
      pitch_1  += pitch_gradient_1;

      sample_2 += step_2;
      vol_2    += volume_gradient_2;
      pitch_2  += pitch_gradient_2;
   }

   *r1 = (sample_1 / tr_1->rate) - position_1;
   *r2 = (sample_2 / tr_2->rate) - position_2;
}

// Sinc interpolation version of process_add_players
// Uses optimized 16-tap sinc with:
// - Direct track memory access (avoids per-sample get_sample calls)
// - Pre-lerped kernels (computed once per sample)
// - NEON SIMD for convolution (4 taps per instruction)
static inline void process_add_players_sinc( signed short *pcm, unsigned samples,
                                        double sample_dt_1, struct track *tr_1, double position_1, float pitch_1, float end_pitch_1, float start_vol_1, float end_vol_1, double* r1,
                                        double sample_dt_2, struct track *tr_2, double position_2, float pitch_2, float end_pitch_2, float start_vol_2, float end_vol_2, double* r2 )
{
   const float ONE_OVER_SAMPLES = 1.0f / static_cast<float>(samples);

   const float volume_gradient_1 = (end_vol_1 - start_vol_1) * ONE_OVER_SAMPLES;
   const float pitch_gradient_1  = (end_pitch_1 - pitch_1)   * ONE_OVER_SAMPLES;
   const float volume_gradient_2 = (end_vol_2 - start_vol_2) * ONE_OVER_SAMPLES;
   const float pitch_gradient_2  = (end_pitch_2 - pitch_2)   * ONE_OVER_SAMPLES;

   const int tr_1_len = static_cast<int>(tr_1->length);
   const int tr_2_len = static_cast<int>(tr_2->length);

   const double tr_1_rate = tr_1->rate;
   const double tr_2_rate = tr_2->rate;

   const double dt_rate_1 = sample_dt_1 * tr_1_rate;
   const double dt_rate_2 = sample_dt_2 * tr_2_rate;

   double sample_1 = position_1 * tr_1->rate;
   double sample_2 = position_2 * tr_2->rate;

   float vol_1 = start_vol_1;
   float vol_2 = start_vol_2;

   for (unsigned s = 0; s < samples; s++)
   {
      double step_1 = dt_rate_1 * pitch_1;
      double step_2 = dt_rate_2 * pitch_2;

      // Get absolute pitch for bandwidth selection
      float abs_pitch_1 = std::fabs(pitch_1);
      float abs_pitch_2 = std::fabs(pitch_2);

      // Optimized sinc interpolation with direct track access
      auto result = sc::dsp::sinc_interpolate_dual_deck_opt(
         tr_1, sample_1, tr_1_len, abs_pitch_1,
         tr_2, sample_2, tr_2_len, abs_pitch_2);

      // Apply volume and mix
      float sum_l = result.l1 * vol_1 + result.l2 * vol_2;
      float sum_r = result.r1 * vol_1 + result.r2 * vol_2;

      // Clamp to int16 range
      sum_l = clamp_scalar(sum_l, static_cast<float>(SHRT_MIN), static_cast<float>(SHRT_MAX));
      sum_r = clamp_scalar(sum_r, static_cast<float>(SHRT_MIN), static_cast<float>(SHRT_MAX));

      *pcm++ = static_cast<signed short>(sum_l);
      *pcm++ = static_cast<signed short>(sum_r);

      sample_1 += step_1;
      vol_1    += volume_gradient_1;
      pitch_1  += pitch_gradient_1;

      sample_2 += step_2;
      vol_2    += volume_gradient_2;
      pitch_2  += pitch_gradient_2;
   }

   *r1 = (sample_1 / tr_1->rate) - position_1;
   *r2 = (sample_2 / tr_2->rate) - position_2;
}

void collect_and_mix_players( struct sc1000* engine,
                              struct audio_capture* capture,
                              signed short *pcm, unsigned long samples, struct sc_settings* settings )
{
   struct player* pl1 = &engine->beat_deck.player;
   struct player* pl2 = &engine->scratch_deck.player;

   double r1 = 0;
   double r2 = 0;
   double target_volume_1, filtered_pitch_1;
   double target_volume_2, filtered_pitch_2;

   setup_player_for_block(pl1, samples, settings, &target_volume_1, &filtered_pitch_1);
   setup_player_for_block(pl2, samples, settings, &target_volume_2, &filtered_pitch_2);

   // During fresh recording (recording active but no loop yet), mute the file track playback
   // so you only hear the monitoring input. For punch-in (use_loop already true), keep playing.
   if (pl1->recording && !pl1->use_loop) {
      target_volume_1 = 0.0;
   }
   if (pl2->recording && !pl2->use_loop) {
      target_volume_2 = 0.0;
   }

   // Select track for each player: use loop track if use_loop is set
   // Deck 0 = beat deck (pl1), Deck 1 = scratch deck (pl2)
   struct track* tr1 = pl1->use_loop ? alsa_peek_loop_track(engine, 0) : pl1->track;
   struct track* tr2 = pl2->use_loop ? alsa_peek_loop_track(engine, 1) : pl2->track;

   if ( spin_try_lock(&pl1->lock) && spin_try_lock(&pl2->lock) )
   {
      // Dispatch based on interpolation mode
      if (g_interpolation_mode == INTERP_SINC) {
         process_add_players_sinc(pcm, static_cast<unsigned>(samples),
                             pl1->sample_dt, tr1, pl1->position - pl1->offset, pl1->pitch, static_cast<float>(filtered_pitch_1),
                             pl1->volume, static_cast<float>(target_volume_1), &r1,
                             pl2->sample_dt, tr2, pl2->position - pl2->offset, pl2->pitch, static_cast<float>(filtered_pitch_2),
                             pl2->volume, static_cast<float>(target_volume_2), &r2);
      } else {
         process_add_players(pcm, static_cast<unsigned>(samples),
                             pl1->sample_dt, tr1, pl1->position - pl1->offset, pl1->pitch, static_cast<float>(filtered_pitch_1),
                             pl1->volume, static_cast<float>(target_volume_1), &r1,
                             pl2->sample_dt, tr2, pl2->position - pl2->offset, pl2->pitch, static_cast<float>(filtered_pitch_2),
                             pl2->volume, static_cast<float>(target_volume_2), &r2);
      }

      pl1->pitch = filtered_pitch_1;
      spin_unlock(&pl1->lock);

      pl2->pitch = filtered_pitch_2;
      spin_unlock(&pl2->lock);
   }

   pl1->position += r1;
   pl1->volume = target_volume_1;

   pl2->position += r2;
   pl2->volume = target_volume_2;

   // Handle capture: loop recording and monitoring
   if (capture && capture->buffer)
   {
      // Write to loop buffer if recording
      int deck = capture->recording_deck;
      if (deck >= 0 && deck < 2 && capture->loop[deck])
      {
         loop_buffer_write(capture->loop[deck], capture->buffer,
                           static_cast<unsigned int>(samples),
                           capture->channels, capture->left_channel, capture->right_channel);
      }

      // Add monitoring: mix capture input into output
      // Only monitor when actively recording
      if (deck >= 0 && capture->monitoring_volume > 0.0f)
      {
         float vol = capture->monitoring_volume;
         for (unsigned long i = 0; i < samples; i++)
         {
            // Extract stereo from capture buffer using channel mapping
            int16_t cap_l = capture->buffer[i * capture->channels + capture->left_channel];
            int16_t cap_r = capture->buffer[i * capture->channels + capture->right_channel];

            // Mix into output with volume and clamp
            float out_l = static_cast<float>(pcm[i * 2 + 0]) + static_cast<float>(cap_l) * vol;
            float out_r = static_cast<float>(pcm[i * 2 + 1]) + static_cast<float>(cap_r) * vol;

            out_l = clamp_scalar(out_l, static_cast<float>(SHRT_MIN), static_cast<float>(SHRT_MAX));
            out_r = clamp_scalar(out_r, static_cast<float>(SHRT_MIN), static_cast<float>(SHRT_MAX));

            pcm[i * 2 + 0] = static_cast<int16_t>(out_l);
            pcm[i * 2 + 1] = static_cast<int16_t>(out_r);
         }
      }
   }
}

} // namespace audio
} // namespace sc

// C API - extern "C" functions
void audio_engine_process(
    struct sc1000* engine,
    struct audio_capture* capture,
    int16_t* playback,
    int playback_channels,
    unsigned long frames)
{
   using namespace sc::audio;

   (void)playback_channels;  // Currently assumes stereo (2 channels)

   double start_time = get_time_us();

   collect_and_mix_players(engine, capture, playback, frames, engine->settings);

   double end_time = get_time_us();
   double process_time = end_time - start_time;

   // Calculate time budget: frames / sample_rate * 1000000 us
   // At 48kHz, 256 frames = 5333 us budget
   constexpr double SAMPLE_RATE = 48000.0;
   double budget_time = (static_cast<double>(frames) / SAMPLE_RATE) * 1000000.0;

   double load = (process_time / budget_time) * 100.0;

   // Update stats with exponential moving average
   g_dsp_stats.process_time_us = process_time;
   g_dsp_stats.budget_time_us = budget_time;
   g_dsp_stats.load_percent = 0.9 * g_dsp_stats.load_percent + 0.1 * load;

   if (load > g_dsp_stats.load_peak) {
      g_dsp_stats.load_peak = load;
   }

   if (load > 100.0) {
      g_dsp_stats.xruns++;
   }
}

void audio_engine_get_stats( struct dsp_stats* stats )
{
   using namespace sc::audio;

   stats->load_percent = g_dsp_stats.load_percent;
   stats->load_peak = g_dsp_stats.load_peak;
   stats->process_time_us = g_dsp_stats.process_time_us;
   stats->budget_time_us = g_dsp_stats.budget_time_us;
   stats->xruns = g_dsp_stats.xruns;
}

void audio_engine_reset_peak( void )
{
   using namespace sc::audio;

   g_dsp_stats.load_peak = 0.0;
   g_dsp_stats.xruns = 0;
}

void audio_engine_set_interpolation(interpolation_mode_t mode)
{
   using namespace sc::audio;
   g_interpolation_mode = mode;
}

interpolation_mode_t audio_engine_get_interpolation(void)
{
   using namespace sc::audio;
   return g_interpolation_mode;
}