/*
 * Copyright (C) 2018 Mark Hills <mark@xwax.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#include "../xwax.h"

#include "player.h"
#include "track.h"

/* Bend playback speed to compensate for the difference between our
 * current position and that given by the timecode */

#define SYNC_TIME (1.0 / 2) /* time taken to reach sync */
#define SYNC_PITCH 0.05		/* don't sync at low pitches */
#define SYNC_RC 0.05		/* filter to 1.0 when no timecodes available */

/* If the difference between our current position and that given by
 * the timecode is greater than this value, recover by jumping
 * straight to the position given by the timecode. */

#define SKIP_THRESHOLD (1.0 / 8) /* before dropping audio */

/* The base volume level. A value of 1.0 leaves no headroom to play
 * louder when the record is going faster than 1.0. */

#define VOLUME (7.0 / 8)

// Time in seconds fader takes to decay
#define FADERDECAY 0.020
#define DECAYSAMPLES FADERDECAY * 48000

#define SQ(x) ((x) * (x))
#define TARGET_UNKNOWN INFINITY


static inline double cubic_interpolate(signed short y[4], double mu)
{
	signed long a0, a1, a2, a3;
	double mu2;

	mu2 = SQ(mu);
	a0 = y[3] - y[2] - y[0] + y[1];
	a1 = y[0] - y[1] - a0;
	a2 = y[2] - y[0];
	a3 = y[1];

	return (mu * mu2 * a0) + (mu2 * a1) + (mu * a2) + a3;
}

static inline float cubic_interpolate_f(const signed short y[4], float mu)
{
   float a0, a1, a2, a3;
   float mu2;

   mu2 = SQ(mu);
   a0 = (float) (y[3] - y[2] - y[0] + y[1]);
   a1 = (float) (y[0] - y[1]) - a0;
   a2 = (float) (y[2] - y[0]);
   a3 = (float)  y[1];

   return (mu * mu2 * a0) + (mu2 * a1) + (mu * a2) + a3;
}

/*
 * Return: Random dither, between -0.5 and 0.5
 */

static double dither(void)
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

	return (double)v / 4096 - 0.5; /* not quite whole range */
}

static float dither_f(void)
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

   return (float)v / 4096 - 0.5f; /* not quite whole range */
}

typedef float v4sf __attribute__ ((vector_size (16)));
typedef int   v4si __attribute__ ((vector_size (16)));
typedef float v2sf __attribute__ ((vector_size (8)));
typedef int   v2si __attribute__ ((vector_size (8)));

// Shuffle helper function for selection
static inline v4sf select_vector_4(v4sf a, v4sf b, v4si mask) {
   return __builtin_shuffle(a, b, mask);
}

static inline v2sf select_vector_2(v2sf a, v2sf b, v2si mask) {
   return __builtin_shuffle(a, b, mask);
}

// Clamping function using masks and shuffle
static inline v4sf clamp_vector_4( v4sf v, v4sf min_val, v4sf max_val ) {
   v4si mask_min = v < min_val;  // Mask: 0xFFFFFFFF where v < min_val
   v4si mask_max = v > max_val;  // Mask: 0xFFFFFFFF where v > max_val

   // Select max(v, min_val)
   v = select_vector_4(v, min_val, mask_min);

   // Select min(v, max_val)
   v = select_vector_4(v, max_val, mask_max);

   return v;
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

/*
 * Build a block of PCM audio, resampled from the track
 *
 * This is just a basic resampler which has a small amount of aliasing
 * where pitch > 1.0.
 *
 * Return: number of seconds advanced in the source audio track
 * Post: buffer at pcm is filled with the given number of samples
 */

static inline void process_add_players( signed short *pcm, unsigned samples,
                                        double sample_dt_1, struct track *tr_1, double position_1, float pitch_1, float end_pitch_1, float start_vol_1, float end_vol_1, double* r1,
                                        double sample_dt_2, struct track *tr_2, double position_2, float pitch_2, float end_pitch_2, float start_vol_2, float end_vol_2, double* r2 )
{
   double sample_1 = position_1 * tr_1->rate;
   double sample_2 = position_2 * tr_2->rate;

   float ONE_OVER_SAMPLES = 1.0f / (float)samples;

   float vol_1 = start_vol_1;
   float volume_gradient_1 = (end_vol_1 - start_vol_1) * ONE_OVER_SAMPLES;
   float pitch_gradient_1 = (end_pitch_1 - pitch_1)    * ONE_OVER_SAMPLES;


   float vol_2 = start_vol_2;
   float volume_gradient_2 = (end_vol_2 - start_vol_2) * ONE_OVER_SAMPLES;
   float pitch_gradient_2 = (end_pitch_2 - pitch_2)    * ONE_OVER_SAMPLES;

   double tr_1_len = tr_1->length;
   double tr_2_len = tr_2->length;

   double tr_1_rate = tr_1->rate;
   double tr_2_rate = tr_2->rate;

   v2sf max2 = {SHRT_MAX, SHRT_MAX};
   v2sf min2 = {SHRT_MIN, SHRT_MIN};

   double dt_rate_1 = sample_dt_1*tr_1_rate;
   double dt_rate_2 = sample_dt_2*tr_2_rate;

   for (int s = 0; s < samples; s++)
   {
      double step_1 = dt_rate_1 * pitch_1;
      double step_2 = dt_rate_2 * pitch_2;

      double subpos_1;
      double subpos_2;

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

            ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
            /* 4-sample window for interpolation */
            sa = ( int ) sample_2;
            if ( sample_2 < 0.0 )
            {
               sa--;
            }

            subpos_2 = sample_2 - sa;
            sa--;

            // wrap to track boundary, i.e. loop
            if ( tr_2_len != 0 )
            {
               sa = sa % ( int ) tr_2_len;
               // Actually don't let people go to minus numbers
               // as entire track might not be loaded yet
               //if (sa < 0) sa += tr->length;
            }

            if ( sa < 0 || sa >= tr_2_len )
            {
               f1l2 = 0;
               f1r2 = 0;
            }
            else
            {
               ts = track_get_sample(tr_2, sa);
               f1l2 = ts[ 0 ];
               f1r2 = ts[ 1 ];
            }

            sa++;
            if ( sa < 0 || sa >= tr_2_len )
            {
               f2l2 = 0;
               f2r2 = 0;
            }
            else
            {
               ts = track_get_sample(tr_2, sa);
               f2l2 = ts[ 0 ];
               f2r2 = ts[ 1 ];
            }

            sa++;
            if ( sa < 0 || sa >= tr_2_len )
            {
               f3l2 = 0;
               f3r2 = 0;
            }
            else
            {
               ts = track_get_sample(tr_2, sa);
               f3l2 = ts[ 0 ];
               f3r2 = ts[ 1 ];
            }

            sa++;
            if ( sa < 0 || sa >= tr_2_len )
            {
               f4l2 = 0;
               f4r2 = 0;
            }
            else
            {
               ts = track_get_sample(tr_2, sa);
               f4l2 = ts[ 0 ];
               f4r2 = ts[ 1 ];
            }
         }

         //
         // Return: the cubic interpolation of the sample at position 2 + mu
         //

         v4sf t0 = {f1l1, f1r1, f1l2, f1r2};
         v4sf t1 = {f2l1, f2r1, f2l2, f2r2};
         v4sf t2 = {f3l1, f3r1, f3l2, f3r2};
         v4sf t3 = {f4l1, f4r1, f4l2, f4r2};

         float tsp1 = (float) subpos_1;
         float tsp2 = (float) subpos_2;

         v4sf mu = {tsp1, tsp1, tsp2, tsp2};
         v4sf mu2= mu*mu;

         v4sf a0 = t3 - t2 - t0 + t1;
         v4sf a1 = t0 - t1 - a0;
         v4sf a2 = t2 - t0;
         v4sf a3 = t1;

         v4sf interpol = (mu * mu2 * a0) + (mu2 * a1) + (mu * a2) + a3;

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

/*
 * Equivalent to build_pcm, but for use when the track is
 * not available
 *
 * Return: number of seconds advanced in the audio track
 * Post: buffer at pcm is filled with silence
 */

static double build_silence(signed short *pcm, unsigned samples,
							double sample_dt, double pitch)
{
	memset(pcm, '\0', sizeof(*pcm) * PLAYER_CHANNELS * samples);
	return sample_dt * pitch * samples;
}

/*
 * Post: player is initialised
 */

void player_init(struct player *pl, unsigned int sample_rate,
				     struct track *track, struct sc_settings* settings)
{
	assert(track != NULL);
	assert(sample_rate != 0);

	spin_init(&pl->lock);

	pl->sample_dt = 1.0 / sample_rate;
	pl->track = track;

	pl->position = 0.0;
	pl->offset = 0.0;
	pl->target_position = 0.0;
	pl->last_difference = 0.0;

	pl->pitch = 0.0;
	pl->sync_pitch = 1.0;
	pl->volume = 0.0;
	pl->set_volume = settings->initial_volume;

	pl->note_pitch = 1.0;
	pl->fader_pitch = 1.0;
	pl->bend_pitch = 1.0;
	pl->stopped = 0;
	pl->recording = false;
	pl->recording_started = false;
	pl->beep_pos = 0;
	pl->playing_beep = -1;
}

/*
 * Pre: player is initialised
 * Post: no resources are allocated by the player
 */

void player_clear(struct player *pl)
{
	spin_clear(&pl->lock);
	track_release(pl->track);
}

double player_get_position(struct player *pl)
{
	return pl->position;
}

double player_get_elapsed(struct player *pl)
{
	return pl->position - pl->offset;
}

double player_get_remain(struct player *pl)
{
	return (double)pl->track->length / pl->track->rate + pl->offset - pl->position;
}

bool player_is_active(const struct player *pl)
{
	return (fabs(pl->pitch) > 0.01);
}

/*
 * Cue to the zero position of the track
 */

void player_recue(struct player *pl)
{
	pl->offset = pl->position;
}

/*
 * Set the track used for the playback
 *
 * Pre: caller holds reference on track
 * Post: caller does not hold reference on track
 */

void player_set_track(struct player *pl, struct track *track)
{
	struct track *x;
	assert(track != NULL);
	assert(track->refcount > 0);
	spin_lock(&pl->lock); /* Synchronise with the playback thread */
	x = pl->track;
	pl->track = track;
	spin_unlock(&pl->lock);
	track_release(x); /* discard the old track */
}

/*
 * Set the playback of one player to match another, used
 * for "instant doubles" and beat juggling
 */

void player_clone(struct player *pl, const struct player *from)
{
	double elapsed;
	struct track *x, *t;

	elapsed = from->position - from->offset;
	pl->offset = pl->position - elapsed;

	t = from->track;
	track_acquire(t);

	spin_lock(&pl->lock);
	x = pl->track;
	pl->track = t;
	spin_unlock(&pl->lock);

	track_release(x);
}

/*
 * Synchronise to the position and speed given by the timecoder
 *
 * Return: 0 on success or -1 if the timecoder is not currently valid
 */


/*
 * Seek to the given position
 */

void player_seek_to(struct player *pl, double seconds)
{
	pl->offset = pl->position - seconds;
	printf("Seek'n %f %f %f\n", seconds, pl->position, pl->offset);
}

unsigned long samplesSoFar = 0;

/*
 * Get a block of PCM audio data to send to the soundcard
 *
 * This is the main function which retrieves audio for playback.  The
 * clock of playback is decoupled from the clock of the timecode
 * signal.
 *
 * Post: buffer at pcm is filled with the given number of samples
 */


bool nearly_equal( double val1, double val2, double tolerance )
{
	if (fabs(val1 - val2) < tolerance)
		return true;
	else
		return false;
}

inline void setup_player_for_block( struct player* pl, unsigned long samples, const struct sc_settings* settings,
                                    double* target_volume,
                                    double* filtered_pitch )
{
   double target_pitch, diff;

   //pl->target_position = (sin(((double) pl->samplesSoFar) / 20000) + 1); // Sine wave to simulate scratching, used for debugging

   // figure out motor speed
   if (pl->stopped)
   {
      // Simulate braking
      if (pl->motor_speed > 0.1)
         pl->motor_speed = pl->motor_speed - (double)samples / (settings->brake_speed * 10);
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
                pl->cap_touch == 0 && pl->cap_touch_old == 0 // don't do it on the first iteration so we pick up backspins
        )
           )
   {
      if (pl->pitch > 20.0) pl->pitch = 20.0;
      if (pl->pitch < -20.0) pl->pitch = -20.0;
      // Simulate slipmat for lasers/phasers
      if (pl->pitch < pl->motor_speed - 0.1)
         target_pitch = pl->pitch + (double)samples / settings->slippiness;
      else if (pl->pitch > pl->motor_speed + 0.1)
         target_pitch = pl->pitch - (double)samples / settings->slippiness;
      else
         target_pitch = pl->motor_speed;
   }
   else
   {
      diff = pl->position - pl->target_position;

      target_pitch = (-diff) * 40;
   }
   pl->cap_touch_old = pl->cap_touch;

   (*filtered_pitch) = (0.1 * target_pitch) + (0.9 * pl->pitch);

   double vol_decay_amount = (DECAYSAMPLES) / (double)samples;

   if ( nearly_equal(pl->fader_target, pl->fader_volume, vol_decay_amount)) // Make sure to set directly when we're nearly there to avoid oscilation
      pl->fader_volume = pl->fader_target;
   else if ( pl->fader_target > pl->fader_volume)
      pl->fader_volume += vol_decay_amount;
   else
      pl->fader_volume -= vol_decay_amount;

   (*target_volume) = fabs(pl->pitch) * VOLUME * pl->fader_volume;

   if ( (*target_volume) > 1.0)
      (*target_volume) = 1.0;
}

void player_collect_add( struct player *pl1, struct player *pl2, signed short *pcm, unsigned long samples, struct sc_settings* settings )
{
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
}