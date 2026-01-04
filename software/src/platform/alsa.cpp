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

#include "../core/global.h"

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
};

static void alsa_error( const char* msg, int r )
{
   fprintf(stderr, "ALSA %s: %s\n", msg, snd_strerror(r));
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
};

static void print_alsa_device_info(struct alsa_device_info* iface)
{
   printf("device_id %i\n", iface->device_id);
   printf("subdevice_id %i\n", iface->subdevice_id);
   printf("is_present %i\n", iface->is_present);
   printf("is_internal %i\n", iface->is_internal);
   printf("input_channels %i\n", iface->input_channels);
   printf("output_channels %i\n", iface->output_channels);
   printf("supports_48k_samplerate %i\n", iface->supports_48k_samplerate);
   printf("supports_16bit_pcm %i\n", iface->supports_16bit_pcm);
   printf("period %i\n", iface->period_size);
}

static struct alsa_device_info alsa_devices[] = {
        {false, -1, -1, 0, 0, false, false, false, 2, 2},
        {false, -1, -1, 0, 0, false, false, false, 2, 2}
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

   printf("fill_audio_interface_info\n");

   // force alsa to init some state
   while ((err = snd_card_next(&card_id)) >= 0 && card_id < 0) {
      printf("First call returned -1, retrying...\n");
   }

   if(card_id >= 0)
   {
      do
      {
         printf("card_id %i, last_card_id %i\n", card_id, last_card_id);

         snd_ctl_t* card_handle;

         sprintf(str, "hw:%i", card_id);

         printf("Open card %i: %s\n", card_id, str);

         if ( (err = snd_ctl_open(&card_handle, str, 0)) < 0 )
         {
            printf("Can't open card %i: %s\n", card_id, snd_strerror(err));
         }
         else
         {
            snd_ctl_card_info_t* card_info = nullptr;

            snd_ctl_card_info_alloca(&card_info);

            if ( (err = snd_ctl_card_info(card_handle, card_info)) < 0 )
            {
               printf("Can't get info for card %i: %s\n", card_id, snd_strerror(err));
            }
            else
            {
               const char* card_name = snd_ctl_card_info_get_name(card_info);

               printf("Card %i = %s\n", card_id, card_name);

               alsa_devices[ card_id ].is_present = true;
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

                  printf("\nChecking PCM device: %s\n", pcm_name);

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
                        printf("  - Playback supported at %d Hz\n", TARGET_SAMPLE_RATE);

                        alsa_devices[ card_id ].supports_48k_samplerate = true;
                     }
                     else
                     {
                        printf("  - Playback does NOT support %d Hz\n", TARGET_SAMPLE_RATE);

                        alsa_devices[ card_id ].supports_48k_samplerate = false;
                     }

                     if ( snd_pcm_hw_params_test_format(pcm, params, TARGET_SAMPLE_FORMAT) == 0 )
                     {
                        printf("    - Playback supports 16-bit signed format\n");

                        alsa_devices[ card_id ].supports_16bit_pcm = true;
                     }
                     else
                     {
                        printf("    - Playback does NOT support 16-bit signed format\n");

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
                              printf("Outputs: %u\n", max);
                              playback_count = max;
                           }
                        }
                     }

                     snd_pcm_hw_params_get_format_mask(params, fmask);
                     printf("\n");

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
                              printf("Inputs: %u\n", max);
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

                  printf("I / O %i %i\n", capture_count, playback_count);
               }

               snd_ctl_close(card_handle);
            }
         }

         last_card_id = card_id;
      } while ( (err = snd_card_next(&card_id)) >= 0 && card_id >= 0 );
   }

   printf("last card id %i %i\n", card_id, last_card_id);

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
      fprintf(stderr, "16-bit signed format is not available. "
                      "You may need to use a 'plughw' device.\n");
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
      fprintf(stderr, "Sample rate of %dHz is not implemented by the hardware.\n",
              TARGET_SAMPLE_RATE);
      return -1;
   }

   alsa->rate = TARGET_SAMPLE_RATE;

   err = snd_pcm_hw_params_set_channels(alsa->pcm, hw_params, num_channels);
   if (!chk("hw_params_set_channels", err))
   {
      fprintf(stderr, "%d channel audio not available on this device.\n",
              DEVICE_CHANNELS);
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
      printf("Error getting period size: %s\n", snd_strerror(err));
      return -1;
   }

   alsa->period_size = period_size;

   printf("Period size: %lu frames\n", period_size);

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
            fprintf(stderr, "Buffer of %lu samples is probably too small; try increasing period size or buffer_period_factor\n",
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

   printf("poll %d ", count);

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

   /*
   r = pcm_pollfds(&alsa->capture, pe, z);
   if (r < 0)
       return -1;

   pe += r;
   z -= r;
   total += r;
 */
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
                            snd_pcm_uframes_t offset)
{
   assert(area->first % 8 == 0);
   assert(area->step == 32);  /* 2 channel 16-bit interleaved */

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
   signed short* pcm = buffer(&areas[0], offset);

   /* Generate audio directly into MMAP buffer */
   sc1000_audio_engine_process(engine, pcm, frames);

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

/* After poll() has returned, instruct a device to do all it can at
 * the present time. Return zero if success, otherwise -1 */

static int handle( struct sc1000* engine )
{
   int r;
   unsigned short revents;
   auto* alsa = static_cast<struct alsa*>(engine->audio_hw_context);

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
            fprintf(stderr, "ALSA: playback xrun.\n");

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

   pcm_close(&alsa->capture);
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

static int setup_alsa_device( struct sc1000* sc1000_engine, struct alsa_device_info* device_info)
{
   struct alsa* alsa;

   alsa = static_cast<struct alsa*>(malloc(sizeof *alsa));
   if ( alsa == nullptr )
   {
      perror("malloc");
      return -1;
   }

   print_alsa_device_info(device_info);

   bool needs_plughw = !device_info->supports_16bit_pcm || !device_info->supports_48k_samplerate;

   char device_name[64];
   create_alsa_device_id_string(device_name, sizeof(device_name), device_info->device_id, device_info->subdevice_id, needs_plughw);
   printf("Opening device %s with period size %i...", device_name, device_info->period_size);

   if ( pcm_open(&alsa->playback, device_name, SND_PCM_STREAM_PLAYBACK, device_info, DEVICE_CHANNELS) < 0 )
   {
      fputs("Failed to open device for playback.\n", stderr);
      printf(" failed!\n");
      goto fail_capture;
   }

   sc1000_engine->audio_hw_context = alsa;
   alsa->started = false;

   // make sure this is really initialized
   alsa_ops.clear       = clear;
   alsa_ops.pollfds     = pollfds;
   alsa_ops.handle      = handle;
   alsa_ops.sample_rate = sample_rate;
   alsa_ops.start       = start;
   alsa_ops.stop        = stop;

   sc1000_audio_engine_init(sc1000_engine, &alsa_ops);

   printf(" success!\n");

   return 0;

   fail_capture:
   pcm_close(&alsa->capture);
   return -1;
}

int alsa_init( struct sc1000* sc1000_engine, struct sc_settings* settings)
{
   printf("alsa_init\n");

   sleep(settings->audio_init_delay);

   fill_audio_interface_info(settings);

   if(!alsa_devices[0].is_internal && alsa_devices[0].is_present)
   {
      return setup_alsa_device(sc1000_engine, &alsa_devices[0]);
   }
   else if(!alsa_devices[1].is_internal && alsa_devices[1].is_present)
   {
      return setup_alsa_device(sc1000_engine, &alsa_devices[1]);
   }
   else
   {
      // must be internal
      return setup_alsa_device(sc1000_engine, &alsa_devices[0]);
   }
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
