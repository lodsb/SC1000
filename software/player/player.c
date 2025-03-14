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

#include "device.h"
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

/*
 * Return: the cubic interpolation of the sample at position 2 + mu
 */

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

/*
 * Build a block of PCM audio, resampled from the track
 *
 * This is just a basic resampler which has a small amount of aliasing
 * where pitch > 1.0.
 *
 * Return: number of seconds advanced in the source audio track
 * Post: buffer at pcm is filled with the given number of samples
 */

static double build_pcm(signed short *pcm, unsigned samples, double sample_dt,
						struct track *tr, double position, double pitch, double end_pitch, double start_vol,
						double end_vol)
{
	int s;
	double sample, step, vol, gradient, pitchGradient;

	sample = position * tr->rate;
	step = sample_dt * pitch * tr->rate;

	vol = start_vol;
	gradient = (end_vol - start_vol) / samples;

	pitchGradient = (end_pitch - pitch) / samples;

	for (s = 0; s < samples; s++)
	{

		step = sample_dt * pitch * tr->rate;

		int c, sa, q;
		double f;
		signed short i[PLAYER_CHANNELS][4];

		/* 4-sample window for interpolation */

		sa = (int)sample;
		if (sample < 0.0)
			sa--;
		f = sample - sa;
		sa--;

		// wrap to track boundary, i.e. loop
		if (tr->length != 0)
		{
			sa = sa % (int)tr->length;
			// Actually don't let people go to minus numbers
			// as entire track might not be loaded yet
			//if (sa < 0) sa += tr->length;
		}

		for (q = 0; q < 4; q++, sa++)
		{
			if (sa < 0 || sa >= tr->length)
			{
				for (c = 0; c < PLAYER_CHANNELS; c++)
					i[c][q] = 0;
			}
			else
			{
				signed short *ts;
				int c;

				ts = track_get_sample(tr, sa);
				for (c = 0; c < PLAYER_CHANNELS; c++)
					i[c][q] = ts[c];
			}
		}

		for (c = 0; c < PLAYER_CHANNELS; c++)
		{
			double v;

			v = vol * cubic_interpolate(i[c], f);// + dither();

			if (v > SHRT_MAX)
			{
				*pcm++ = SHRT_MAX;
			}
			else if (v < SHRT_MIN)
			{
				*pcm++ = SHRT_MIN;
			}
			else
			{
				*pcm++ = (signed short)v;
			}
		}

		sample += step;

		// Loop when track gets to end
		/*f (sample > tr->length && looping){
			sample = 0;

		}*/

		vol += gradient;
		pitch += pitchGradient;
	}

	//return sample_dt * pitch * samples;
	return (sample / tr->rate) - position;
}

static inline void build_pcm_add_dither( signed short *pcm, unsigned samples,
                                         double sample_dt_1, struct track *tr_1, double position_1, double pitch_1, double end_pitch_1, double start_vol_1, double end_vol_1, double* r1,
                                         double sample_dt_2, struct track *tr_2, double position_2, double pitch_2, double end_pitch_2, double start_vol_2, double end_vol_2, double* r2)
{
   double sample_1 = position_1 * tr_1->rate;
   double vol_1 = start_vol_1;
   double volume_gradient_1 = (end_vol_1 - start_vol_1) / samples;
   double pitch_gradient_1 = (end_pitch_1 - pitch_1) / samples;

   double sample_2 = position_2 * tr_2->rate;
   double vol_2 = start_vol_2;
   double volume_gradient_2 = (end_vol_2 - start_vol_2) / samples;
   double pitch_gradient_2 = (end_pitch_2 - pitch_2) / samples;


   for (int s = 0; s < samples; s++)
   {
      double step_1 = sample_dt_1 * pitch_1 * tr_1->rate;

      double f_1;
      signed short i_1[PLAYER_CHANNELS][4];
      {

         /* 4-sample window for interpolation */
         int sa = ( int ) sample_1;
         if ( sample_1 < 0.0 )
         {
            sa--;
         }

         f_1 = sample_1 - sa;
         sa--;

         // wrap to track boundary, i.e. loop
         if ( tr_1->length != 0 )
         {
            sa = sa % ( int ) tr_1->length;
            // Actually don't let people go to minus numbers
            // as entire track might not be loaded yet
            //if (sa < 0) sa += tr->length;
         }

         for (int q = 0; q < 4; q++, sa++ )
         {
            if ( sa < 0 || sa >= tr_1->length )
            {
               for (int c = 0; c < PLAYER_CHANNELS; c++ )
                  i_1[ c ][ q ] = 0;
            }
            else
            {
               signed short* ts;

               ts = track_get_sample(tr_1, sa);
               for (int c = 0; c < PLAYER_CHANNELS; c++ )
                  i_1[ c ][ q ] = ts[ c ];
            }
         }
      }

      double step_2 = sample_dt_2 * pitch_2 * tr_2->rate;

      double f_2;
      signed short i_2[PLAYER_CHANNELS][4];
      {

         /* 4-sample window for interpolation */
         int sa = ( int ) sample_2;
         if ( sample_2 < 0.0 )
         {
            sa--;
         }

         f_2 = sample_2 - sa;
         sa--;

         // wrap to track boundary, i.e. loop
         if ( tr_2->length != 0 )
         {
            sa = sa % ( int ) tr_2->length;
            // Actually don't let people go to minus numbers
            // as entire track might not be loaded yet
            //if (sa < 0) sa += tr->length;
         }

         for (int q = 0; q < 4; q++, sa++ )
         {
            if ( sa < 0 || sa >= tr_2->length )
            {
               for (int c = 0; c < PLAYER_CHANNELS; c++ )
                  i_2[ c ][ q ] = 0;
            }
            else
            {
               signed short* ts;

               ts = track_get_sample(tr_2, sa);
               for (int c = 0; c < PLAYER_CHANNELS; c++ )
                  i_2[ c ][ q ] = ts[ c ];
            }
         }
      }

      for (int c = 0; c < PLAYER_CHANNELS; c++)
      {
         double v_1 = vol_1 * cubic_interpolate(i_1[c], f_1);
         double v_2 = vol_2 * cubic_interpolate(i_2[c], f_2);

         double v = v_1 + v_2 + dither();

         if ( v > SHRT_MAX)
         {
            *pcm++ = SHRT_MAX;
         }
         else if ( v < SHRT_MIN)
         {
            *pcm++ = SHRT_MIN;
         }
         else
         {
            *pcm++ = (signed short)v;
         }
      }

      sample_1 += step_1;
      vol_1 += volume_gradient_1;
      pitch_1 += pitch_gradient_1;

      sample_2 += step_2;
      vol_2 += volume_gradient_2;
      pitch_2 += pitch_gradient_2;      
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
	//player_set_timecoder(pl, tc);

	pl->position = 0.0;
	pl->offset = 0.0;
	pl->target_position = 0.0;
	pl->last_difference = 0.0;

	pl->pitch = 0.0;
	pl->sync_pitch = 1.0;
	pl->volume = 0.0;
	pl->setVolume = settings->initial_volume;
	pl->GoodToGo = 0;
	pl->samplesSoFar = 0;
	pl->note_pitch = 1.0;
	pl->fader_pitch = 1.0;
	pl->bend_pitch = 1.0;
	pl->stopped = 0;
	pl->recording = false;
	pl->recordingStarted = false;
	pl->beepPos = 0;
	pl->playingBeep = -1;
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

/*
 * Enable or disable timecode control
 */

void player_set_timecode_control(struct player *pl, bool on)
{
	if (on && !pl->timecode_control)
		pl->recalibrate = true;
	pl->timecode_control = on;
}

/*
 * Toggle timecode control
 *
 * Return: the new state of timecode control
 */

bool player_toggle_timecode_control(struct player *pl)
{
	pl->timecode_control = !pl->timecode_control;
	if (pl->timecode_control)
		pl->recalibrate = true;
	return pl->timecode_control;
}

void player_set_internal_playback(struct player *pl)
{
	pl->timecode_control = false;
	pl->pitch = 1.0;
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
 * Synchronise to the position given by the timecoder without
 * affecting the audio playback position
 */

static void calibrate_to_timecode_position(struct player *pl)
{
	assert(pl->target_position != TARGET_UNKNOWN);
	pl->offset += pl->target_position - pl->position;
	pl->position = pl->target_position;
}

void retarget(struct player *pl)
{
	double diff;

	if (pl->recalibrate)
	{
		calibrate_to_timecode_position(pl);
		pl->recalibrate = false;
	}

	/* Calculate the pitch compensation required to get us back on
	 * track with the absolute timecode position */

	diff = pl->position - pl->target_position;
	pl->last_difference = diff; /* to print in user interface */

	if (fabs(diff) > SKIP_THRESHOLD)
	{

		/* Jump the track to the time */

		pl->position = pl->target_position;
		fprintf(stderr, "Seek to new position %.2lfs.\n", pl->position);
	}
	else if (fabs(pl->pitch) > SYNC_PITCH)
	{

		/* Re-calculate the drift between the timecoder pitch from
		 * the sine wave and the timecode values */

		pl->sync_pitch = pl->pitch / (diff / SYNC_TIME + pl->pitch);
	}
}

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

void player_collect(struct player *pl, signed short *pcm, unsigned long samples, struct sc_settings* settings)
{
	double r, pitch=0.0, target_volume, amountToDecay, target_pitch, filtered_pitch;
	double diff;

	pl->samplesSoFar += samples;

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
	if ( pl->justPlay == 1 || // platter is always released on beat deck
		(
			pl->capTouch == 0 && pl->oldCapTouch == 0 // don't do it on the first iteration so we pick up backspins
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
	pl->oldCapTouch = pl->capTouch;

	filtered_pitch = (0.1 * target_pitch) + (0.9 * pl->pitch);

	amountToDecay = (DECAYSAMPLES) / (double)samples;

	if ( nearly_equal(pl->faderTarget, pl->faderVolume, amountToDecay)) // Make sure to set directly when we're nearly there to avoid oscilation
		pl->faderVolume = pl->faderTarget;
	else if (pl->faderTarget > pl->faderVolume)
		pl->faderVolume += amountToDecay;
	else
		pl->faderVolume -= amountToDecay;

	target_volume = fabs(pl->pitch) * VOLUME * pl->faderVolume;

	if (target_volume > 1.0)
		target_volume = 1.0;

	/* Sync pitch is applied post-filtering */

	/* We must return audio immediately to stay realtime. A spin
	 * lock protects us from changes to the audio source */

	if (!spin_try_lock(&pl->lock))
	{
		r = build_silence(pcm, samples, pl->sample_dt, pitch);
	}
	else
	{
		r = build_pcm(pcm, samples, pl->sample_dt, pl->track,
					  pl->position - pl->offset, pl->pitch, filtered_pitch, pl->volume, target_volume);
		pl->pitch = filtered_pitch;
		spin_unlock(&pl->lock);
	}

	pl->position += r;

	pl->volume = target_volume;
}

void setup_player_for_block( struct player* pl, unsigned long samples, const struct sc_settings* settings,
                             double* target_volume,
                             double* filtered_pitch )
{
   pl->samplesSoFar += samples;

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
   if ( pl->justPlay == 1 || // platter is always released on beat deck
        (
                pl->capTouch == 0 && pl->oldCapTouch == 0 // don't do it on the first iteration so we pick up backspins
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
   pl->oldCapTouch = pl->capTouch;

   (*filtered_pitch) = (0.1 * target_pitch) + (0.9 * pl->pitch);

   double amountToDecay = (DECAYSAMPLES) / (double)samples;

   if ( nearly_equal(pl->faderTarget, pl->faderVolume, amountToDecay)) // Make sure to set directly when we're nearly there to avoid oscilation
      pl->faderVolume = pl->faderTarget;
   else if (pl->faderTarget > pl->faderVolume)
      pl->faderVolume += amountToDecay;
   else
      pl->faderVolume -= amountToDecay;

   (*target_volume) = fabs(pl->pitch) * VOLUME * pl->faderVolume;

   if ( (*target_volume) > 1.0)
      (*target_volume) = 1.0;
}

void player_collect_add( struct player *pl1, struct player *pl2, signed short *pcm, unsigned long samples, struct sc_settings* settings )
{
   {
      double r1, target_volume,   filtered_pitch;
      double r2, target_volume_2, filtered_pitch_2;

      setup_player_for_block(pl1, samples, settings, &target_volume, &filtered_pitch);
      setup_player_for_block(pl2, samples, settings, &target_volume_2, &filtered_pitch_2);

      if ( spin_try_lock(&pl1->lock) && spin_try_lock(&pl2->lock) )
      {
         /*r1 = build_pcm(pcm, samples, pl1->sample_dt, pl1->track,
                       pl1->position - pl1->offset, pl1->pitch, filtered_pitch, pl1->volume, target_volume);*/

         build_pcm_add_dither(pcm, samples,
                              pl1->sample_dt, pl1->track, pl1->position - pl1->offset, pl1->pitch, filtered_pitch, pl1->volume, target_volume, &r1,
                              pl2->sample_dt, pl2->track,pl2->position - pl2->offset, pl2->pitch, filtered_pitch_2, pl2->volume, target_volume_2, &r2);

         pl1->pitch = filtered_pitch;
         spin_unlock(&pl1->lock);

         pl2->pitch = filtered_pitch_2;
         spin_unlock(&pl2->lock);
      }

      pl1->position += r1;
      pl1->volume = target_volume;

      pl2->position += r2;
      pl2->volume = target_volume_2;
   }
}