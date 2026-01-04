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

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <cassert>
#include <cstdlib>
#include <sys/poll.h>
#include <alsa/asoundlib.h>

#include "../util/log.h"
#include "../core/global.h"
#include "../core/sc_settings.h"
#include "../engine/cv_engine.h"
#include "../engine/loop_buffer.h"
#include "../player/player.h"
#include "../player/track.h"

#include "alsa.h"


/* This structure doesn't have corresponding functions to be an
 * abstraction of the ALSA calls; it is merely a container for these
 * variables. */

struct alsa_pcm
{
   snd_pcm_t* pcm;

   struct pollfd* pe;
   size_t pe_count; /* number of pollfd entries */

   signed short* buf;  /* audio buffer for RW mode */
   int rate;
   snd_pcm_uframes_t period_size;
};

struct alsa
{
   struct alsa_pcm capture, playback;
   bool started;
   bool capture_enabled;                // Whether capture device is open
   int num_channels;                    // Number of output channels (2 for stereo, 12 for Bitwig Connect, etc.)
   int capture_channels;                // Number of input channels
   int capture_left;                    // Which capture channel is left (default 0)
   int capture_right;                   // Which capture channel is right (default 1)
   struct audio_interface* config;      // Pointer to the matched config (for output_map)
   struct cv_state cv;                  // CV engine state
   struct loop_buffer loop[2];          // Loop buffer per deck (0=beat, 1=scratch)
   int active_recording_deck;           // Which deck is currently recording (-1 = none)
};

static void alsa_error( const char* msg, int r )
{
   LOG_ERROR("ALSA %s: %s", msg, snd_strerror(r));
}

static bool chk( const char* s, int r )
{
   if ( r < 0 )
   {
      alsa_error(s, r);
      return false;
   }
   else
   {
      return true;
   }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal ALSA device discovery info (not to be confused with sc_settings audio_interface)
struct alsa_device_info
{
   bool is_present;
   int device_id;
   int subdevice_id;

   unsigned int input_channels;
   unsigned int output_channels;

   bool is_internal;
   bool supports_48k_samplerate;
   bool supports_16bit_pcm;

   unsigned int period_size;
   unsigned int buffer_period_factor;

   char card_name[128];  // ALSA card name for matching
};

static void print_alsa_device_info(struct alsa_device_info* iface)
{
   LOG_INFO("Device info: card='%s' dev=%d sub=%d present=%d internal=%d in=%d out=%d 48k=%d 16bit=%d period=%d",
            iface->card_name, iface->device_id, iface->subdevice_id,
            iface->is_present, iface->is_internal,
            iface->input_channels, iface->output_channels,
            iface->supports_48k_samplerate, iface->supports_16bit_pcm,
            iface->period_size);
}

#define MAX_ALSA_DEVICES 8

static struct alsa_device_info alsa_devices[MAX_ALSA_DEVICES] = {
        {false, -1, -1, 0, 0, false, false, false, 2, 2, ""},
        {false, -1, -1, 0, 0, false, false, false, 2, 2, ""},
        {false, -1, -1, 0, 0, false, false, false, 2, 2, ""},
        {false, -1, -1, 0, 0, false, false, false, 2, 2, ""},
        {false, -1, -1, 0, 0, false, false, false, 2, 2, ""},
        {false, -1, -1, 0, 0, false, false, false, 2, 2, ""},
        {false, -1, -1, 0, 0, false, false, false, 2, 2, ""},
        {false, -1, -1, 0, 0, false, false, false, 2, 2, ""}
};

static void create_alsa_device_id_string(char* str, unsigned int size, int dev, int subdev, bool is_plughw)
{
   if(!is_plughw)
   {
      snprintf(str, size, "hw:%d,%d", dev, subdev);
   }
   else
   {
      snprintf(str, size, "plughw:%d,%d", dev, subdev);
   }
}

static void fill_audio_interface_info(struct sc_settings* settings)
{
   int err;
   int card_id, last_card_id = -1;

   char str[64];
   char pcm_name[32];

   snd_pcm_format_mask_t* fmask;
   snd_pcm_format_mask_alloca(&fmask);

   LOG_INFO("Scanning ALSA audio interfaces");

   // force alsa to init some state
   while ((err = snd_card_next(&card_id)) >= 0 && card_id < 0) {
      LOG_DEBUG("First call returned -1, retrying...");
   }

   if(card_id >= 0)
   {
      do
      {
         LOG_DEBUG("card_id %d, last_card_id %d", card_id, last_card_id);

         snd_ctl_t* card_handle;

         sprintf(str, "hw:%i", card_id);

         LOG_DEBUG("Open card %d: %s", card_id, str);

         if ( (err = snd_ctl_open(&card_handle, str, 0)) < 0 )
         {
            LOG_WARN("Can't open card %d: %s", card_id, snd_strerror(err));
         }
         else
         {
            snd_ctl_card_info_t* card_info = nullptr;

            snd_ctl_card_info_alloca(&card_info);

            if ( (err = snd_ctl_card_info(card_handle, card_info)) < 0 )
            {
               LOG_WARN("Can't get info for card %d: %s", card_id, snd_strerror(err));
            }
            else
            {
               const char* card_name = snd_ctl_card_info_get_name(card_info);

               LOG_INFO("Card %d = %s", card_id, card_name);

               if (card_id >= MAX_ALSA_DEVICES) {
                  LOG_WARN("Skipping card %d (max %d devices supported)", card_id, MAX_ALSA_DEVICES);
                  snd_ctl_close(card_handle);
                  continue;
               }

               alsa_devices[ card_id ].is_present = true;
               strncpy(alsa_devices[card_id].card_name, card_name, sizeof(alsa_devices[card_id].card_name) - 1);
               alsa_devices[card_id].card_name[sizeof(alsa_devices[card_id].card_name) - 1] = '\0';
               if ( strcmp(card_name, "sun4i-codec") == 0 )
               {
                  alsa_devices[ card_id ].is_internal = true;
                  alsa_devices[ card_id ].period_size = settings->period_size;
                  alsa_devices[ card_id ].buffer_period_factor = settings->buffer_period_factor;
               }
               else
               {
                  alsa_devices[ card_id ].is_internal = false;
                  alsa_devices[ card_id ].period_size = settings->period_size;
                  alsa_devices[ card_id ].buffer_period_factor = settings->buffer_period_factor;
               }

               unsigned int playback_count = 0;
               unsigned int capture_count = 0;

               int device_id = -1;

               while ( snd_ctl_pcm_next_device(card_handle, &device_id) >= 0 && device_id >= 0 )
               {
                  create_alsa_device_id_string(pcm_name, sizeof(pcm_name), card_id, device_id, false);

                  LOG_DEBUG("Checking PCM device: %s", pcm_name);

                  snd_pcm_t* pcm;

                  // Try opening in playback mode
                  if ( snd_pcm_open(&pcm, pcm_name, SND_PCM_STREAM_PLAYBACK, 0) >= 0 )
                  {
                     snd_pcm_hw_params_t* params;
                     snd_pcm_hw_params_alloca(&params);
                     snd_pcm_hw_params_any(pcm, params);

                     // Check if sample rate is supported
                     if ( snd_pcm_hw_params_test_rate(pcm, params, TARGET_SAMPLE_RATE, 0) == 0 )
                     {
                        LOG_DEBUG("  - Playback supported at %d Hz", TARGET_SAMPLE_RATE);

                        alsa_devices[ card_id ].supports_48k_samplerate = true;
                     }
                     else
                     {
                        LOG_DEBUG("  - Playback does NOT support %d Hz", TARGET_SAMPLE_RATE);

                        alsa_devices[ card_id ].supports_48k_samplerate = false;
                     }

                     if ( snd_pcm_hw_params_test_format(pcm, params, TARGET_SAMPLE_FORMAT) == 0 )
                     {
                        LOG_DEBUG("  - Playback supports 16-bit signed format");

                        alsa_devices[ card_id ].supports_16bit_pcm = true;
                     }
                     else
                     {
                        LOG_DEBUG("  - Playback does NOT support 16-bit signed format");

                        alsa_devices[ card_id ].supports_16bit_pcm = false;
                     }

                     unsigned int min, max;
                     err = snd_pcm_hw_params_get_channels_min(params, &min);
                     if ( err >= 0 )
                     {
                        err = snd_pcm_hw_params_get_channels_max(params, &max);
                        if ( err >= 0 )
                        {
                           if ( !snd_pcm_hw_params_test_channels(pcm, params, max) )
                           {
                              LOG_DEBUG("Outputs: %u", max);
                              playback_count = max;
                           }
                        }
                     }

                     snd_pcm_hw_params_get_format_mask(params, fmask);

                     snd_pcm_close(pcm);
                  }

                  // Try opening in capture mode
                  if ( snd_pcm_open(&pcm, pcm_name, SND_PCM_STREAM_CAPTURE, 0) >= 0 )
                  {
                     snd_pcm_hw_params_t* params;
                     snd_pcm_hw_params_alloca(&params);
                     snd_pcm_hw_params_any(pcm, params);

                     unsigned int min, max;
                     err = snd_pcm_hw_params_get_channels_min(params, &min);
                     if ( err >= 0 )
                     {
                        err = snd_pcm_hw_params_get_channels_max(params, &max);
                        if ( err >= 0 )
                        {
                           if ( !snd_pcm_hw_params_test_channels(pcm, params, max) )
                           {
                              LOG_DEBUG("Inputs: %u", max);
                              capture_count = max;
                           }
                        }
                     }

                     snd_pcm_close(pcm);
                  }

                  alsa_devices[ card_id ].input_channels = capture_count;
                  alsa_devices[ card_id ].output_channels = playback_count;

                  alsa_devices[ card_id ].device_id = card_id;
                  alsa_devices[ card_id ].subdevice_id = 0; // for now

                  LOG_DEBUG("I/O channels: %d/%d", capture_count, playback_count);
               }

               snd_ctl_close(card_handle);
            }
         }

         last_card_id = card_id;
      } while ( (err = snd_card_next(&card_id)) >= 0 && card_id >= 0 );
   }

   LOG_DEBUG("Last card id %d %d", card_id, last_card_id);

   //ALSA allocates some mem to load its config file when we call some of the
   //above functions. Now that we're done getting the info, let's tell ALSA
   //to unload the info and free up that mem
   snd_config_update_free_global();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int pcm_open( struct alsa_pcm* alsa, const char* device_name,
                     snd_pcm_stream_t stream, struct alsa_device_info* device_info, uint8_t num_channels)
{
   int err, dir;

   snd_pcm_hw_params_t *hw_params;
   snd_pcm_uframes_t frames;

   err = snd_pcm_open(&alsa->pcm, device_name, stream, SND_PCM_NONBLOCK);
   if (!chk("open", err))
      return -1;

   snd_pcm_hw_params_alloca(&hw_params);

   err = snd_pcm_hw_params_any(alsa->pcm, hw_params);
   if (!chk("hw_params_any", err))
      return -1;

   err = snd_pcm_hw_params_set_access(alsa->pcm, hw_params,
                                      SND_PCM_ACCESS_MMAP_INTERLEAVED);
   if (!chk("hw_params_set_access", err))
      return -1;

   err = snd_pcm_hw_params_set_format(alsa->pcm, hw_params, SND_PCM_FORMAT_S16);
   if (!chk("hw_params_set_format", err)) {
      LOG_ERROR("16-bit signed format is not available. You may need to use a 'plughw' device.");
      return -1;
   }

   /* Prevent accidentally introducing excess resamplers. There is
    * already one on the signal path to handle pitch adjustments.
    * This is even if a 'plug' device is used, which effectively lets
    * the user unknowingly select any sample rate. */

   err = snd_pcm_hw_params_set_rate_resample(alsa->pcm, hw_params, 0);
   if (!chk("hw_params_set_rate_resample", err))
      return -1;

   err = snd_pcm_hw_params_set_rate(alsa->pcm, hw_params, TARGET_SAMPLE_RATE, 0);
   if (!chk("hw_params_set_rate", err)) {
      LOG_ERROR("Sample rate of %dHz is not implemented by the hardware.", TARGET_SAMPLE_RATE);
      return -1;
   }

   alsa->rate = TARGET_SAMPLE_RATE;

   err = snd_pcm_hw_params_set_channels(alsa->pcm, hw_params, num_channels);
   if (!chk("hw_params_set_channels", err))
   {
      LOG_ERROR("%d channel audio not available on this device.", num_channels);
      return -1;
   }

   /* This is fundamentally a latency-sensitive application that is
    * likely to be the primary application running, so assume we want
    * the hardware to be giving us immediate wakeups */

   snd_pcm_uframes_t period_size = ( snd_pcm_uframes_t ) device_info->period_size;
   err = snd_pcm_hw_params_set_period_size_near(alsa->pcm, hw_params, &period_size, &dir);
   if (!chk("snd_pcm_hw_params_set_period_size_near", err))
   {
      return -1;
   }

   err = snd_pcm_hw_params_get_period_size(hw_params, &period_size, nullptr);
   if (!chk("snd_pcm_hw_params_get_period_size", err))
   {
      LOG_ERROR("Error getting period size: %s", snd_strerror(err));
      return -1;
   }

   alsa->period_size = period_size;

   LOG_INFO("Period size: %lu frames", period_size);

   alsa->buf = nullptr;  /* Not needed for MMAP mode */
   auto buffer_size = static_cast<snd_pcm_uframes_t>(period_size * device_info->buffer_period_factor);

   switch (stream) {
      case SND_PCM_STREAM_CAPTURE:
         /* Maximum buffer to minimise drops */
         err = snd_pcm_hw_params_set_buffer_size_last(alsa->pcm, hw_params, &frames);
         if (!chk("hw_params_set_buffer_size_last", err))
            return -1;
         break;

      case SND_PCM_STREAM_PLAYBACK:
         /* Smallest possible buffer to keep latencies low */
         err = snd_pcm_hw_params_set_buffer_size(alsa->pcm, hw_params, buffer_size);
         if (!chk("hw_params_set_buffer_size", err)) {
            LOG_ERROR("Buffer of %lu samples is probably too small; try increasing period size or buffer_period_factor",
                      buffer_size);
            return -1;
         }
         break;

      default:
         abort();
   }

   err = snd_pcm_hw_params(alsa->pcm, hw_params);
   if (!chk("hw_params", err))
      return -1;

   return 0;
}

static void pcm_close( struct alsa_pcm* alsa )
{
   if ( snd_pcm_close(alsa->pcm) < 0 )
   {
      abort();
   }
   if (alsa->buf) {
      free(alsa->buf);
      alsa->buf = nullptr;
   }
}

static ssize_t pcm_pollfds( struct alsa_pcm* alsa, struct pollfd* pe,
                            size_t z )
{
   int r;

   int count = snd_pcm_poll_descriptors_count(alsa->pcm);
   auto ucount = static_cast<unsigned int>(count);

   LOG_DEBUG("poll %d", count);

   if ( ucount > z )
   {
      return -1;
   }

   if ( ucount == 0 )
   {
      alsa->pe = nullptr;
   }
   else
   {
      r = snd_pcm_poll_descriptors(alsa->pcm, pe, ucount);
      if ( r < 0 )
      {
         alsa_error("poll_descriptors", r);
         return -1;
      }
      alsa->pe = pe;
   }

   alsa->pe_count = ucount;
   return count;
}

static int pcm_revents( struct alsa_pcm* alsa, unsigned short* revents )
{
   int r;

   r = snd_pcm_poll_descriptors_revents(alsa->pcm, alsa->pe, alsa->pe_count,
                                        revents);
   if ( r < 0 )
   {
      alsa_error("poll_descriptors_revents", r);
      return -1;
   }

   return 0;
}

/* Start the audio device capture and playback */

static void start( struct sc1000* dv )
{
   (void)dv;
   //struct alsa *alsa = (struct alsa*)dv->local;

   //if (snd_pcm_start(alsa->capture.pcm) < 0)
   //   abort();
}

static void stop( struct sc1000* dv )
{
   (void)dv;
   //struct alsa *alsa = (struct alsa*)dv->local;

   //if (snd_pcm_start(alsa->capture.pcm) < 0)
   //   abort();
}

/* Register this device's interest in a set of pollfd file
 * descriptors */

static ssize_t pollfds( struct sc1000* engine, struct pollfd* pe, size_t z )
{
   ssize_t r;
   ssize_t total = 0;
   auto* alsa = static_cast<struct alsa*>(engine->audio_hw_context);

   // Add capture poll descriptors if capture is enabled
   if (alsa->capture_enabled)
   {
      r = pcm_pollfds(&alsa->capture, pe, z);
      if (r < 0)
         return -1;

      pe += r;
      z -= static_cast<size_t>(r);
      total += r;
   }

   r = pcm_pollfds(&alsa->playback, pe, z);
   if ( r < 0 )
   {
      return -1;
   }

   total += r;

   return total;
}

/* Access the interleaved area presented by the ALSA library.  The
 * device is opened SND_PCM_FORMAT_S16 which is in the local endianess
 * and therefore is "signed short" */

static signed short* buffer(const snd_pcm_channel_area_t *area,
                            snd_pcm_uframes_t offset,
                            int num_channels)
{
   assert(area->first % 8 == 0);
   // step = num_channels * 16 bits per sample
   unsigned int expected_step = num_channels * 16;
   assert(area->step == expected_step);

   /* Calculate byte offset, then cast to short* */
   unsigned int bitofs = area->first + area->step * offset;
   return reinterpret_cast<signed short*>(static_cast<char*>(area->addr) + bitofs / 8);
}


/* Collect audio from the player and push it into the device's buffer,
 * for playback using MMAP for lowest latency */

static int playback( struct sc1000* engine)
{
   auto* alsa = static_cast<struct alsa*>(engine->audio_hw_context);
   const snd_pcm_channel_area_t *areas;
   snd_pcm_uframes_t offset, frames;
   snd_pcm_sframes_t commitres, avail;
   int err;

   /* Check how many frames are available */
   avail = snd_pcm_avail_update(alsa->playback.pcm);
   if (avail < 0)
      return static_cast<int>(avail);

   if (static_cast<snd_pcm_uframes_t>(avail) < alsa->playback.period_size)
      return 0;  /* Not enough space yet */

   frames = alsa->playback.period_size;

   err = snd_pcm_mmap_begin(alsa->playback.pcm, &areas, &offset, &frames);
   if (err < 0)
      return err;

   /* Get pointer to the MMAP buffer */
   signed short* pcm = buffer(&areas[0], offset, alsa->num_channels);

   /* For multi-channel: clear entire buffer first (zeros for unused channels) */
   if (alsa->num_channels > 2)
   {
      memset(pcm, 0, frames * alsa->num_channels * sizeof(signed short));
   }

   /* Generate stereo audio - audio_engine writes to channels 0-1 */
   /* For multi-channel, we need to interleave properly */
   if (alsa->num_channels == 2)
   {
      /* Simple stereo case - write directly */
      sc1000_audio_engine_process(engine, pcm, frames);
   }
   else
   {
      /* Multi-channel: use temporary stereo buffer, then copy to channels 0-1 */
      static signed short stereo_buf[1024 * 2];  /* Max period size * 2 channels */
      sc1000_audio_engine_process(engine, stereo_buf, frames);

      /* Copy stereo to first 2 channels of multi-channel buffer */
      for (unsigned long i = 0; i < frames; i++)
      {
         pcm[i * alsa->num_channels + 0] = stereo_buf[i * 2 + 0];  /* Left */
         pcm[i * alsa->num_channels + 1] = stereo_buf[i * 2 + 1];  /* Right */
      }

      /* Process CV outputs if configured */
      if (alsa->config && alsa->config->supports_cv)
      {
         struct player* pl = &engine->scratch_deck.player;

         /* Gather raw controller state - cv_engine handles all processing */
         struct cv_controller_input cv_input = {
            .pitch = pl->pitch,
            .encoder_angle = engine->scratch_deck.encoder_angle,
            .sample_position = pl->position,
            .sample_length = pl->track ? pl->track->length : 0,
            .fader_volume = pl->fader_volume,
            .fader_target = pl->fader_target
         };

         cv_engine_update(&alsa->cv, &cv_input);
         cv_engine_process(&alsa->cv, pcm, alsa->num_channels, frames);
      }
   }

   commitres = snd_pcm_mmap_commit(alsa->playback.pcm, offset, frames);
   if (commitres < 0 || static_cast<snd_pcm_uframes_t>(commitres) != frames)
      return commitres < 0 ? static_cast<int>(commitres) : -EPIPE;

   /* Start PCM on first write */
   if (!alsa->started) {
      err = snd_pcm_start(alsa->playback.pcm);
      if (err < 0)
         return err;
      alsa->started = true;
   }

   return 0;
}

/* Read audio from capture device and feed to loop buffer */

static int capture( struct sc1000* engine )
{
   auto* alsa = static_cast<struct alsa*>(engine->audio_hw_context);
   const snd_pcm_channel_area_t *areas;
   snd_pcm_uframes_t offset, frames;
   snd_pcm_sframes_t avail;
   int err;

   /* Check how many frames are available */
   avail = snd_pcm_avail_update(alsa->capture.pcm);
   if (avail < 0)
   {
      if (avail == -EPIPE)
      {
         // Capture overrun - just recover
         snd_pcm_prepare(alsa->capture.pcm);
         snd_pcm_start(alsa->capture.pcm);
         return 0;
      }
      return static_cast<int>(avail);
   }

   if (static_cast<snd_pcm_uframes_t>(avail) < alsa->capture.period_size)
      return 0;  /* Not enough data yet */

   frames = alsa->capture.period_size;

   err = snd_pcm_mmap_begin(alsa->capture.pcm, &areas, &offset, &frames);
   if (err < 0)
      return err;

   /* Get pointer to the MMAP buffer */
   const signed short* pcm = buffer(&areas[0], offset, alsa->capture_channels);

   /* Feed to active deck's loop buffer if recording */
   int deck = alsa->active_recording_deck;
   if (deck >= 0 && deck < 2 && loop_buffer_is_recording(&alsa->loop[deck]))
   {
      loop_buffer_write(&alsa->loop[deck], pcm, static_cast<unsigned int>(frames),
                        alsa->capture_channels, alsa->capture_left, alsa->capture_right);
   }

   snd_pcm_sframes_t commitres = snd_pcm_mmap_commit(alsa->capture.pcm, offset, frames);
   if (commitres < 0 || static_cast<snd_pcm_uframes_t>(commitres) != frames)
   {
      if (commitres == -EPIPE)
      {
         snd_pcm_prepare(alsa->capture.pcm);
         snd_pcm_start(alsa->capture.pcm);
         return 0;
      }
      return commitres < 0 ? static_cast<int>(commitres) : -EPIPE;
   }

   return 0;
}

/* After poll() has returned, instruct a device to do all it can at
 * the present time. Return zero if success, otherwise -1 */

static int handle( struct sc1000* engine )
{
   int r;
   unsigned short revents;
   auto* alsa = static_cast<struct alsa*>(engine->audio_hw_context);

   /* Check the capture buffer */
   if (alsa->capture_enabled)
   {
      r = pcm_revents(&alsa->capture, &revents);
      if ( r < 0 )
      {
         return -1;
      }

      if ( revents & POLLIN )
      {
         r = capture(engine);
         if ( r < 0 && r != -EPIPE )
         {
            alsa_error("capture", r);
            // Don't return error - continue with playback
         }
      }
   }

   /* Check the output buffer for playback */

   r = pcm_revents(&alsa->playback, &revents);
   if ( r < 0 )
   {
      return -1;
   }

   if ( revents & POLLOUT )
   {
      r = playback(engine);

      if ( r < 0 )
      {
         if ( r == -EPIPE )
         {
            LOG_WARN("ALSA: playback xrun");

            r = snd_pcm_prepare(alsa->playback.pcm);
            if ( r < 0 )
            {
               alsa_error("prepare", r);
               return -1;
            }

            alsa->started = false;

            /* The device starts when data is written. POLLOUT
             * events are generated in prepared state. */
         }
         else
         {
            alsa_error("playback", r);
            return -1;
         }
      }
   }

   return 0;
}

static unsigned int sample_rate( struct sc1000* engine )
{
   auto* alsa = static_cast<struct alsa*>(engine->audio_hw_context);

   return alsa->playback.rate;
}

/* Close ALSA device and clear any allocations */

static void clear(struct sc1000* engine )
{
   auto* alsa = static_cast<struct alsa*>(engine->audio_hw_context);

   loop_buffer_clear(&alsa->loop[0]);
   loop_buffer_clear(&alsa->loop[1]);
   if (alsa->capture_enabled)
   {
      pcm_close(&alsa->capture);
   }
   pcm_close(&alsa->playback);
   free(engine->audio_hw_context);
}

static struct sc1000_ops alsa_ops = {
        .pollfds = pollfds,
        .handle = handle,
        .sample_rate = sample_rate,
        .start = start,
        .stop = stop,
        .clear = clear
};

static int setup_alsa_device( struct sc1000* sc1000_engine, struct alsa_device_info* device_info,
                               struct audio_interface* config, int num_channels,
                               struct sc_settings* settings)
{
   struct alsa* alsa;

   alsa = static_cast<struct alsa*>(malloc(sizeof *alsa));
   if ( alsa == nullptr )
   {
      LOG_ERROR("malloc failed for alsa struct");
      return -1;
   }

   print_alsa_device_info(device_info);

   bool needs_plughw = !device_info->supports_16bit_pcm || !device_info->supports_48k_samplerate;

   char device_name[64];
   create_alsa_device_id_string(device_name, sizeof(device_name), device_info->device_id, device_info->subdevice_id, needs_plughw);
   LOG_INFO("Opening device %s with %d channels, period size %d...", device_name, num_channels, device_info->period_size);

   if ( pcm_open(&alsa->playback, device_name, SND_PCM_STREAM_PLAYBACK, device_info, num_channels) < 0 )
   {
      LOG_ERROR("Failed to open device for playback");
      free(alsa);
      return -1;
   }

   sc1000_engine->audio_hw_context = alsa;
   alsa->started = false;
   alsa->num_channels = num_channels;
   alsa->config = config;

   // Initialize capture if device has input channels
   alsa->capture_enabled = false;
   alsa->capture_channels = 0;
   // Get input channel mapping from config (user settings), with defaults
   alsa->capture_left = config ? config->input_left : 0;
   alsa->capture_right = config ? config->input_right : 1;

   // Use config->input_channels if specified, otherwise detect from hardware
   int available_inputs = config && config->input_channels > 0
                          ? config->input_channels
                          : static_cast<int>(device_info->input_channels);

   if (available_inputs >= 2)
   {
      LOG_INFO("Opening capture with %d channels (left=%d, right=%d)...",
               available_inputs, alsa->capture_left, alsa->capture_right);

      if ( pcm_open(&alsa->capture, device_name, SND_PCM_STREAM_CAPTURE, device_info,
                    static_cast<uint8_t>(available_inputs)) >= 0 )
      {
         alsa->capture_enabled = true;
         alsa->capture_channels = available_inputs;
         LOG_INFO("Capture device opened successfully");

         // Start capture immediately
         if (snd_pcm_start(alsa->capture.pcm) < 0)
         {
            LOG_WARN("Failed to start capture PCM");
         }
      }
      else
      {
         LOG_WARN("Failed to open capture device, recording disabled");
      }
   }
   else
   {
      LOG_INFO("No input channels available, recording disabled");
   }

   // Initialize loop buffers for both decks
   int loop_max = settings ? settings->loop_max_seconds : 60;
   loop_buffer_init(&alsa->loop[0], TARGET_SAMPLE_RATE, loop_max);  // Beat deck
   loop_buffer_init(&alsa->loop[1], TARGET_SAMPLE_RATE, loop_max);  // Scratch deck
   alsa->active_recording_deck = -1;  // No recording active

   // Initialize CV engine if CV is supported
   if (config && config->supports_cv)
   {
      cv_engine_init(&alsa->cv, TARGET_SAMPLE_RATE);
      cv_engine_set_mapping(&alsa->cv, config);
      LOG_INFO("CV engine initialized for %s", config->name);
   }

   // make sure this is really initialized
   alsa_ops.clear       = clear;
   alsa_ops.pollfds     = pollfds;
   alsa_ops.handle      = handle;
   alsa_ops.sample_rate = sample_rate;
   alsa_ops.start       = start;
   alsa_ops.stop        = stop;

   sc1000_audio_engine_init(sc1000_engine, &alsa_ops);

   LOG_INFO("ALSA device setup complete");

   return 0;
}

// Case-insensitive substring search
static bool contains_substring_ci(const char* haystack, const char* needle)
{
   if (!haystack || !needle) return false;
   if (needle[0] == '\0') return true;

   size_t haylen = strlen(haystack);
   size_t needlelen = strlen(needle);

   if (needlelen > haylen) return false;

   for (size_t i = 0; i <= haylen - needlelen; i++)
   {
      bool match = true;
      for (size_t j = 0; j < needlelen; j++)
      {
         char h = haystack[i + j];
         char n = needle[j];
         // Simple ASCII case-insensitive compare
         if (h >= 'A' && h <= 'Z') h = static_cast<char>(h + ('a' - 'A'));
         if (n >= 'A' && n <= 'Z') n = static_cast<char>(n + ('a' - 'A'));
         if (h != n) { match = false; break; }
      }
      if (match) return true;
   }
   return false;
}

// Match a config entry to a detected ALSA device
static struct alsa_device_info* find_matching_device(struct audio_interface* config)
{
   // Method 1: Parse the device string to get card number
   // Config device is like "hw:0" or "hw:1" or "plughw:1"
   int card_num = -1;
   if (sscanf(config->device, "hw:%d", &card_num) == 1 ||
       sscanf(config->device, "plughw:%d", &card_num) == 1)
   {
      if (card_num >= 0 && card_num < MAX_ALSA_DEVICES && alsa_devices[card_num].is_present)
      {
         return &alsa_devices[card_num];
      }
   }

   // Method 2: Substring match against card name
   // This allows matching by partial name, e.g., "USB" matches "USB Audio Device"
   // Also try matching the config 'name' field against card name
   for (int i = 0; i < MAX_ALSA_DEVICES; i++)
   {
      if (!alsa_devices[i].is_present) continue;

      // Try matching config->device against card name (substring, case-insensitive)
      if (contains_substring_ci(alsa_devices[i].card_name, config->device))
      {
         LOG_DEBUG("Matched by device substring: '%s' contains '%s'",
                   alsa_devices[i].card_name, config->device);
         return &alsa_devices[i];
      }

      // Try matching config->name against card name
      if (contains_substring_ci(alsa_devices[i].card_name, config->name))
      {
         LOG_DEBUG("Matched by name substring: '%s' contains '%s'",
                   alsa_devices[i].card_name, config->name);
         return &alsa_devices[i];
      }
   }

   return nullptr;
}

int alsa_init( struct sc1000* sc1000_engine, struct sc_settings* settings)
{
   LOG_INFO("ALSA init starting");

   sleep(settings->audio_init_delay);

   fill_audio_interface_info(settings);

   // Try to match config entries with detected devices (in priority order)
   for (int i = 0; i < settings->num_audio_interfaces; i++)
   {
      struct audio_interface* config = &settings->audio_interfaces[i];
      struct alsa_device_info* device = find_matching_device(config);

      if (device)
      {
         LOG_INFO("Matched config '%s' to device %s", config->name, config->device);
         return setup_alsa_device(sc1000_engine, device, config, config->channels, settings);
      }
      else
      {
         LOG_DEBUG("Config '%s' (%s) - device not found", config->name, config->device);
      }
   }

   // Fallback: use first available device with stereo
   LOG_INFO("No config match, using fallback");
   for (int i = 0; i < MAX_ALSA_DEVICES; i++)
   {
      if (alsa_devices[i].is_present)
      {
         LOG_INFO("Using fallback device %d (%s)", i, alsa_devices[i].card_name);
         return setup_alsa_device(sc1000_engine, &alsa_devices[i], nullptr, DEVICE_CHANNELS, settings);
      }
   }

   LOG_ERROR("No audio device found!");
   return -1;
}

/* ALSA caches information when devices are open. Provide a call
 * to clear these caches so that valgrind output is clean. */

void alsa_clear_config_cache( void )
{
   int r;

   r = snd_config_update_free_global();
   if ( r < 0 )
   {
      alsa_error("config_update_free_global", r);
   }
}

//
// Loop recording control functions
//

bool alsa_start_recording(struct sc1000* engine, int deck_no)
{
   auto* alsa = static_cast<struct alsa*>(engine->audio_hw_context);

   if (!alsa->capture_enabled)
   {
      LOG_WARN("Recording not available: no capture device");
      return false;
   }

   if (deck_no < 0 || deck_no > 1)
   {
      LOG_ERROR("Invalid deck number: %d", deck_no);
      return false;
   }

   // Only one deck can record at a time
   if (alsa->active_recording_deck >= 0 && alsa->active_recording_deck != deck_no)
   {
      LOG_WARN("Deck %d already recording", alsa->active_recording_deck);
      return false;
   }

   if (loop_buffer_start(&alsa->loop[deck_no]))
   {
      alsa->active_recording_deck = deck_no;
      LOG_INFO("Started recording on deck %d", deck_no);
      return true;
   }
   return false;
}

void alsa_stop_recording(struct sc1000* engine, int deck_no)
{
   auto* alsa = static_cast<struct alsa*>(engine->audio_hw_context);
   if (deck_no >= 0 && deck_no <= 1)
   {
      loop_buffer_stop(&alsa->loop[deck_no]);
      if (alsa->active_recording_deck == deck_no)
      {
         alsa->active_recording_deck = -1;
      }
      LOG_INFO("Stopped recording on deck %d", deck_no);
   }
}

bool alsa_is_recording(struct sc1000* engine, int deck_no)
{
   auto* alsa = static_cast<struct alsa*>(engine->audio_hw_context);
   if (deck_no < 0 || deck_no > 1) return false;
   return loop_buffer_is_recording(&alsa->loop[deck_no]);
}

struct track* alsa_get_loop_track(struct sc1000* engine, int deck_no)
{
   auto* alsa = static_cast<struct alsa*>(engine->audio_hw_context);
   if (deck_no < 0 || deck_no > 1) return nullptr;
   return loop_buffer_get_track(&alsa->loop[deck_no]);
}

struct track* alsa_peek_loop_track(struct sc1000* engine, int deck_no)
{
   // RT-safe: just returns the pointer, no ref count change
   auto* alsa = static_cast<struct alsa*>(engine->audio_hw_context);
   if (deck_no < 0 || deck_no > 1) return nullptr;
   return alsa->loop[deck_no].track;
}

bool alsa_has_capture(struct sc1000* engine)
{
   auto* alsa = static_cast<struct alsa*>(engine->audio_hw_context);
   return alsa->capture_enabled;
}

bool alsa_has_loop(struct sc1000* engine, int deck_no)
{
   auto* alsa = static_cast<struct alsa*>(engine->audio_hw_context);
   if (deck_no < 0 || deck_no > 1) return false;
   return loop_buffer_has_loop(&alsa->loop[deck_no]);
}

void alsa_reset_loop(struct sc1000* engine, int deck_no)
{
   auto* alsa = static_cast<struct alsa*>(engine->audio_hw_context);
   if (deck_no >= 0 && deck_no <= 1)
   {
      loop_buffer_reset(&alsa->loop[deck_no]);
      LOG_INFO("Reset loop on deck %d", deck_no);
   }
}
