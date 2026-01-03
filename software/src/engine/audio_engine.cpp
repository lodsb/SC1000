#include <iostream>
#include <cmath>
#include <climits>

#include "audio_engine.h"
#include "../core/sc_settings.h"

#include "../player/track.h"
#include "../player/deck.h"

namespace sc {
namespace audio {

// Time in seconds fader takes to decay
constexpr double FADER_DECAY_TIME = 0.020;
constexpr double DECAY_SAMPLES = FADER_DECAY_TIME * 48000;

// The base volume level. A value of 1.0 leaves no headroom to play
// louder when the record is going faster than 1.0.
constexpr double BASE_VOLUME = 7.0 / 8.0;

// Vector types for SIMD operations
using v4sf = float __attribute__ ((vector_size (16)));
using v4si = int   __attribute__ ((vector_size (16)));
using v2sf = float __attribute__ ((vector_size (8)));
using v2si = int   __attribute__ ((vector_size (8)));

static float dither_f()
{
   unsigned int bit, v;
   static unsigned int x = 0xbeefface;

   /* Maximum length LFSR sequence with 32-bit state */

   bit = (x ^ (x >> 1) ^ (x >> 21) ^ (x >> 31)) & 1;
   x = x << 1 | bit;

   /* We can adjust the balance between randomness and performance
    * by our chosen bit permutation; here we use a 12 bit subset
    * of the state */

   v = (x & 0x0000000f) | ((x & 0x000f0000) >> 12) | ((x & 0x0f000000) >> 16);

   return static_cast<float>(v) / 4096 - 0.5f; /* not quite whole range */
}

static bool nearly_equal( double val1, double val2, double tolerance )
{
   return std::fabs(val1 - val2) < tolerance;
}

inline static v4sf interpolate_two_stereo_samples( v4sf t0, v4sf t1, v4sf t2, v4sf t3, float tsp1, float tsp2 )
{

   //
   // the cubic interpolation of the sample at position 2 + mu
   //

   v4sf mu = {tsp1, tsp1, tsp2, tsp2};
   v4sf mu2= mu*mu;

   v4sf a0 = t3 - t2 - t0 + t1;
   v4sf a1 = t0 - t1 - a0;
   v4sf a2 = t2 - t0;
   v4sf a3 = t1;

   v4sf interpol = (mu * mu2 * a0) + (mu2 * a1) + (mu * a2) + a3;
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
      ts = track_get_sample(tr_1, sa);
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
      ts = track_get_sample(tr_1, sa);
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
      ts = track_get_sample(tr_1, sa);
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
      ts = track_get_sample(tr_1, sa);
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

// Shuffle helper function for selection
static inline v4sf select_vector_4(v4sf a, v4sf b, v4si mask) {
   return __builtin_shuffle(a, b, mask);
}

static inline v2sf select_vector_2(v2sf a, v2sf b, v2si mask) {
   return __builtin_shuffle(a, b, mask);
}

static inline v2sf clamp_vector_2( v2sf v, v2sf min_val, v2sf max_val ) {
   v2si mask_min = v < min_val;  // Mask: 0xFFFFFFFF where v < min_val
   v2si mask_max = v > max_val;  // Mask: 0xFFFFFFFF where v > max_val

   // Select max(v, min_val)
   v = select_vector_2(v, min_val, mask_min);

   // Select min(v, max_val)
   v = select_vector_2(v, max_val, mask_max);

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
   if ( pl->just_play == 1 || // platter is always released on beat deck
        (
                pl->cap_touch == 0 &&
                pl->cap_touch_old == 0 // don't do it on the first iteration so we pick up backspins
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

static inline void process_add_players( signed short *pcm, unsigned samples,
                                        double sample_dt_1, struct track *tr_1, double position_1, float pitch_1, float end_pitch_1, float start_vol_1, float end_vol_1, double* r1,
                                        double sample_dt_2, struct track *tr_2, double position_2, float pitch_2, float end_pitch_2, float start_vol_2, float end_vol_2, double* r2 )
{
   static constexpr v2sf max2 = {SHRT_MAX, SHRT_MAX};
   static constexpr v2sf min2 = {SHRT_MIN, SHRT_MIN};

   const float ONE_OVER_SAMPLES = 1.0f / static_cast<float>(samples);

   const float volume_gradient_1 = (end_vol_1 - start_vol_1) * ONE_OVER_SAMPLES;
   const float pitch_gradient_1  = (end_pitch_1 - pitch_1)   * ONE_OVER_SAMPLES;
   const float volume_gradient_2 = (end_vol_2 - start_vol_2) * ONE_OVER_SAMPLES;
   const float pitch_gradient_2  = (end_pitch_2 - pitch_2)   * ONE_OVER_SAMPLES;

   const double tr_1_len = tr_1->length;
   const double tr_2_len = tr_2->length;

   const double tr_1_rate = tr_1->rate;
   const double tr_2_rate = tr_2->rate;

   const double dt_rate_1 = sample_dt_1*tr_1_rate;
   const double dt_rate_2 = sample_dt_2*tr_2_rate;

   double sample_1 = position_1 * tr_1->rate;
   double sample_2 = position_2 * tr_2->rate;

   float vol_1 = start_vol_1;
   float vol_2 = start_vol_2;

   for (int s = 0; s < samples; s++)
   {
      double step_1 = dt_rate_1 * pitch_1;
      double step_2 = dt_rate_2 * pitch_2;

      double subpos_1;
      double subpos_2;

      {
         v4sf t0;
         v4sf t1;
         v4sf t2;
         v4sf t3;

         collect_track_samples_vectorized(tr_1, tr_2, sample_1, sample_2, tr_1_len, tr_2_len, subpos_1, subpos_2,
                                          t0, t1, t2, t3);

         v4sf interpol = interpolate_two_stereo_samples(t0, t1, t2, t3,
                                                        static_cast<float>(subpos_1),
                                                        static_cast<float>(subpos_2));

         v4sf tvol = {vol_1, vol_1, vol_2, vol_2};
         v4sf res = interpol * tvol;

         v2sf sum = {res[0] + res[2], res[1] + res[3]};

         sum = clamp_vector_2(sum, min2, max2);

         *pcm++ =(signed short) sum[0];
         *pcm++ =(signed short) sum[1];
      }

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

void collect_and_mix_players( struct player *pl1, struct player *pl2,
                              signed short *pcm, unsigned long samples, struct sc_settings* settings )
{
   double r1 = 0;
   double r2 = 0;
   double target_volume_1, filtered_pitch_1;
   double target_volume_2, filtered_pitch_2;

   setup_player_for_block(pl1, samples, settings, &target_volume_1, &filtered_pitch_1);
   setup_player_for_block(pl2, samples, settings, &target_volume_2, &filtered_pitch_2);

   if ( spin_try_lock(&pl1->lock) && spin_try_lock(&pl2->lock) )
   {
      process_add_players(pcm, samples,
                          pl1->sample_dt, pl1->track, pl1->position - pl1->offset, pl1->pitch, filtered_pitch_1,
                          pl1->volume, target_volume_1, &r1,
                          pl2->sample_dt, pl2->track, pl2->position - pl2->offset, pl2->pitch, filtered_pitch_2,
                          pl2->volume, target_volume_2, &r2);

      pl1->pitch = filtered_pitch_1;
      spin_unlock(&pl1->lock);

      pl2->pitch = filtered_pitch_2;
      spin_unlock(&pl2->lock);
   }

   pl1->position += r1;
   pl1->volume = target_volume_1;

   pl2->position += r2;
   pl2->volume = target_volume_2;
}

} // namespace audio
} // namespace sc

// C API - extern "C" function for use by C code
void audio_engine_process( struct sc1000* engine, signed short* pcm, unsigned long frames )
{
   sc::audio::collect_and_mix_players(
      &(engine->beat_deck.player),
      &(engine->scratch_deck.player),
      pcm, frames, engine->settings);
}