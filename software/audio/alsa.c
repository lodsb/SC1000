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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/poll.h>
#include <alsa/asoundlib.h>
#include <stdint.h>
#include <math.h>

#include "../global/global.h"

#include "../player/player.h"
#include "../player/track.h"


#include "alsa.h"


const char* BEEPS[3] = {
        "----------",          // Start Recording
        "- - - - - - - - -",   // Stop Recording
        "--__--__--__--__--__" // Recording error
};

/* This structure doesn't have corresponding functions to be an
 * abstraction of the ALSA calls; it is merely a container for these
 * variables. */

struct alsa_pcm
{
   snd_pcm_t* pcm;

   struct pollfd* pe;
   size_t pe_count; /* number of pollfd entries */

   signed short* buf, * buf2;
   snd_pcm_uframes_t period;
   int rate;
};

struct alsa
{
   struct alsa_pcm capture, playback;
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
struct audio_interface
{
   bool is_present;
   int device_id;
   int subdevice_id;

   int input_channels;
   int output_channels;

   bool is_internal;
   bool supports_48k_samplerate;
   bool supports_16bit_pcm;

   int period;
};

void print_audio_interface_info(struct audio_interface* iface)
{
   printf("device_id %i\n", iface->device_id);
   printf("subdevice_id %i\n", iface->subdevice_id);
   printf("is_present %i\n", iface->is_present);
   printf("is_internal %i\n", iface->is_internal);
   printf("input_channels %i\n", iface->input_channels);
   printf("output_channels %i\n", iface->output_channels);
   printf("supports_48k_samplerate %i\n", iface->supports_48k_samplerate);
   printf("supports_16bit_pcm %i\n", iface->supports_16bit_pcm);
   printf("period %i\n", iface->period);
}

static struct audio_interface audio_interfaces[] = {
        {false, -1, -1, -1, -1, false, false, false, 2},
        {false, -1, -1, -1, -1, false, false, false, 2}
};

void create_alsa_device_id_string(char* str, int size, int dev, int subdev, bool is_plughw)
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

static void fill_audio_interface_info()
{
   register int err;
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
            snd_ctl_card_info_t* card_info = NULL;

            snd_ctl_card_info_alloca(&card_info);

            if ( (err = snd_ctl_card_info(card_handle, card_info)) < 0 )
            {
               printf("Can't get info for card %i: %s\n", card_id, snd_strerror(err));
            }
            else
            {
               const char* card_name = snd_ctl_card_info_get_name(card_info);

               printf("Card %i = %s\n", card_id, card_name);

               audio_interfaces[ card_id ].is_present = true;
               if ( strcmp(card_name, "sun4i-codec") == 0 )
               {
                  audio_interfaces[ card_id ].is_internal = true;
                  audio_interfaces[ card_id ].period = 2;
               }
               else
               {
                  audio_interfaces[ card_id ].is_internal = false;
                  audio_interfaces[ card_id ].period = 3;
               }

               int playback_count = 0;
               int capture_count = 0;

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

                        audio_interfaces[ card_id ].supports_48k_samplerate = true;
                     }
                     else
                     {
                        printf("  - Playback does NOT support %d Hz\n", TARGET_SAMPLE_RATE);

                        audio_interfaces[ card_id ].supports_48k_samplerate = false;
                     }

                     if ( snd_pcm_hw_params_test_format(pcm, params, TARGET_SAMPLE_FORMAT) == 0 )
                     {
                        printf("    - Playback supports 16-bit signed format\n");

                        audio_interfaces[ card_id ].supports_16bit_pcm = true;
                     }
                     else
                     {
                        printf("    - Playback does NOT support 16-bit signed format\n");

                        audio_interfaces[ card_id ].supports_16bit_pcm = false;
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

                  audio_interfaces[ card_id ].input_channels = capture_count;
                  audio_interfaces[ card_id ].output_channels = playback_count;

                  audio_interfaces[ card_id ].device_id = card_id;
                  audio_interfaces[ card_id ].subdevice_id = 0; // for now

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
                     snd_pcm_stream_t stream, int buffer_size, int period )
{
   int r, dir;
   unsigned int p;
   size_t bytes;
   snd_pcm_hw_params_t* hw_params;

   r = snd_pcm_open(&alsa->pcm, device_name, stream, SND_PCM_NONBLOCK);
   if ( !chk("open", r) )
   {
      return -1;
   }

   snd_pcm_hw_params_alloca(&hw_params);

   r = snd_pcm_hw_params_any(alsa->pcm, hw_params);
   if ( !chk("hw_params_any", r) )
   {
      return -1;
   }

   r = snd_pcm_hw_params_set_access(alsa->pcm, hw_params,
                                    SND_PCM_ACCESS_RW_INTERLEAVED);
   if ( !chk("hw_params_set_access", r) )
   {
      return -1;
   }

   r = snd_pcm_hw_params_set_format(alsa->pcm, hw_params, TARGET_SAMPLE_FORMAT);
   if ( !chk("hw_params_set_format", r) )
   {
      fprintf(stderr, "16-bit signed format is not available. "
                      "You may need to use a 'plughw' device.\n");
      return -1;
   }

   r = snd_pcm_hw_params_set_rate(alsa->pcm, hw_params, TARGET_SAMPLE_RATE, 0);
   if ( !chk("hw_params_set_rate", r) )
   {
      fprintf(stderr, "%dHz sample rate not available. You may need to use "
                      "a 'plughw' device.\n",
              TARGET_SAMPLE_RATE);
      return -1;
   }
   alsa->rate = TARGET_SAMPLE_RATE;

   r = snd_pcm_hw_params_set_channels(alsa->pcm, hw_params, DEVICE_CHANNELS);
   if ( !chk("hw_params_set_channels", r) )
   {
      fprintf(stderr, "%d channel audio not available on this device.\n",
              DEVICE_CHANNELS);
      return -1;
   }

   /* p = buffer_time * 1000;
   dir = -1;
   r = snd_pcm_hw_params_set_buffer_time_near(alsa->pcm, hw_params, &p, &dir);
   if (!chk("hw_params_set_buffer_time_near", r)) {
       fprintf(stderr, "Buffer of %dms may be too small for this hardware.\n",
               buffer_time);
       return -1;
   }*/

   if ( snd_pcm_hw_params_set_buffer_size(alsa->pcm, hw_params, buffer_size) < 0 )
   {
      fprintf(stderr, "Error setting buffer_size.\n");
      return (-1);
   }

   p = period;
   dir = 1;
   r = snd_pcm_hw_params_set_periods_min(alsa->pcm, hw_params, &p, &dir);
   if ( !chk("hw_params_set_periods_min", r) )
   {
      fprintf(stderr, "Buffer may be too small for this hardware.\n");
      return -1;
   }

   r = snd_pcm_hw_params(alsa->pcm, hw_params);
   if ( !chk("hw_params", r) )
   {
      return -1;
   }

   r = snd_pcm_hw_params_get_period_size(hw_params, &alsa->period, &dir);
   if ( !chk("get_period_size", r) )
   {
      return -1;
   }
   snd_pcm_uframes_t fun;
   r = snd_pcm_hw_params_get_buffer_size(hw_params, &fun);

   bytes = alsa->period * DEVICE_CHANNELS * sizeof(signed short);
   alsa->buf = malloc(bytes);
   if ( !alsa->buf )
   {
      perror("malloc");
      return -1;
   }
   alsa->buf2 = malloc(bytes);
   if ( !alsa->buf2 )
   {
      perror("malloc");
      return -1;
   }

   /* snd_pcm_readi() returns uninitialised memory on first call,
    * possibly caused by premature POLLIN. Keep valgrind happy. */

   memset(alsa->buf, 0, bytes);
   memset(alsa->buf2, 0, bytes);

   return 0;
}

static void pcm_close( struct alsa_pcm* alsa )
{
   if ( snd_pcm_close(alsa->pcm) < 0 )
   {
      abort();
   }
   free(alsa->buf);
}

static ssize_t pcm_pollfds( struct alsa_pcm* alsa, struct pollfd* pe,
                            size_t z )
{
   int r, count;

   count = snd_pcm_poll_descriptors_count(alsa->pcm);
   if ( count > z )
   {
      return -1;
   }

   if ( count == 0 )
   {
      alsa->pe = NULL;
   }
   else
   {
      r = snd_pcm_poll_descriptors(alsa->pcm, pe, count);
      if ( r < 0 )
      {
         alsa_error("poll_descriptors", r);
         return -1;
      }
      alsa->pe = pe;
   }

   alsa->pe_count = count;
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

static void start( struct device* dv )
{
}

/* Register this device's interest in a set of pollfd file
 * descriptors */

static ssize_t pollfds( struct device* dv, struct pollfd* pe, size_t z )
{
   int total, r;
   struct alsa* alsa = ( struct alsa* ) dv->local;

   total = 0;
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

/* Collect audio from the player and push it into the device's buffer,
 * for playback */

static int playback( struct device* dv, struct sc_settings* settings )
{
   int r, i;
   struct alsa* alsa = ( struct alsa* ) dv->local;
   static int32_t adder = 0;
   static int16_t next_recording_number = 0;
   static char syncCommandLine[300];

   if ( dv->scratch_player->recordingStarted && !dv->scratch_player->recording )
   {
      next_recording_number = 0;
      while ( 1 )
      {
         sprintf(dv->scratch_player->recordingFileName, "/media/sda/sc%06d.raw", next_recording_number);
         if ( access(dv->scratch_player->recordingFileName, F_OK) != -1 )
         {
            // file exists
            next_recording_number++;

            // If we've reached max files then abort (very unlikely, heh)
            if ( next_recording_number == INT16_MAX )
            {
               printf("Too many recordings\n");
               next_recording_number = -1;
               dv->scratch_player->playingBeep = BEEP_RECORDINGERROR;
               dv->scratch_player->recordingStarted = 0;
               break;
            }
         }
         else
         {
            // file doesn't exist
            break;
         }
      }

      if ( next_recording_number != -1 )
      {
         printf("Opening file %s for recording\n", dv->scratch_player->recordingFileName);
         dv->scratch_player->recordingFile = fopen(dv->scratch_player->recordingFileName, "w");

         // On error, don't start
         if ( dv->scratch_player->recordingFile == NULL )
         {
            printf("Failed to open recording file\n");
            dv->scratch_player->recordingStarted = 0;
            dv->scratch_player->playingBeep = BEEP_RECORDINGERROR;
         }
         else
         {
            dv->scratch_player->recording = 1;
            dv->scratch_player->playingBeep = BEEP_RECORDINGSTART;
         }
      }
   }

   /*if ((dv->player->GoodToGo && dv->player2->GoodToGo) || (dv->player->track->finished == 1 && dv->player2->track->finished)){

  dv->player->GoodToGo = 1;dv->player2->GoodToGo = 1;*/

   player_collect(dv->scratch_player, alsa->playback.buf , alsa->playback.period, settings);
   player_collect(dv->beat_player   , alsa->playback.buf2, alsa->playback.period, settings);

   // mix 2 players together
   for ( i = 0; i < alsa->playback.period * 2; i++ )
   {
      adder = ( int32_t ) alsa->playback.buf[ i ] + ( int32_t ) alsa->playback.buf2[ i ];

      // saturate add
      if ( adder > INT16_MAX )
      {
         adder = INT16_MAX;
      }
      if ( adder < INT16_MIN )
      {
         adder = INT16_MIN;
      }
      alsa->playback.buf[ i ] = ( int16_t ) adder;
   }
   //}

   if ( dv->scratch_player->recording )
   {
      fwrite(alsa->playback.buf, alsa->playback.period * DEVICE_CHANNELS * sizeof(signed short), 1,
             dv->scratch_player->recordingFile);
   }

   // Add beeps, if we need to
   if ( dv->scratch_player->playingBeep != -1 )
   {

      for ( i = 0; i < alsa->playback.period * 2; i++ )
      {
         char curChar = BEEPS[ dv->scratch_player->playingBeep ][ dv->scratch_player->beepPos / BEEPSPEED ];
         if ( curChar )
         {
            unsigned int beepFreq = 0;

            if ( curChar == '-' )
            {
               beepFreq = 440;
            }
            else if ( curChar == '_' )
            {
               beepFreq = 220;
            }
            else
            {
               beepFreq = 0;
            }

            if ( beepFreq != 0 )
            {
               adder = ( int32_t ) alsa->playback.buf[ i ] +
                       (sin((( double ) dv->scratch_player->beepPos / (48000.0 / ( double ) beepFreq)) * 6.2831) * 20000.0);

               // saturate add
               if ( adder > INT16_MAX )
               {
                  adder = INT16_MAX;
               }
               if ( adder < INT16_MIN )
               {
                  adder = INT16_MIN;
               }
               alsa->playback.buf[ i ] = ( int16_t ) adder;
            }
            dv->scratch_player->beepPos++;
         }
         else
         {
            dv->scratch_player->playingBeep = -1;
            dv->scratch_player->beepPos = 0;
            break;
         }
      }
   }
   r = snd_pcm_writei(alsa->playback.pcm, alsa->playback.buf,
                      alsa->playback.period);
   if ( r < 0 )
   {
      return r;
   }

   if ( r < alsa->playback.period )
   {
      fprintf(stderr, "alsa: playback underrun %d/%ld.\n", r,
              alsa->playback.period);
   }

   if ( !dv->scratch_player->recordingStarted && dv->scratch_player->recording )
   {
      fflush(dv->scratch_player->recordingFile);
      fclose(dv->scratch_player->recordingFile);
      sprintf(syncCommandLine, "/bin/sync %s", dv->scratch_player->recordingFileName);
      system(syncCommandLine);
      dv->scratch_player->recording = 0;
      dv->scratch_player->playingBeep = BEEP_RECORDINGSTOP;
   }

   return 0;
}

/* After poll() has returned, instruct a device to do all it can at
 * the present time. Return zero if success, otherwise -1 */

static int handle( struct device* dv )
{
   int r;
   unsigned short revents;
   struct alsa* alsa = ( struct alsa* ) dv->local;

   /* Check the output buffer for playback */

   r = pcm_revents(&alsa->playback, &revents);
   if ( r < 0 )
   {
      return -1;
   }

   if ( revents & POLLOUT )
   {
      r = playback(dv, &sc1000_settings);

      if ( r < 0 )
      {
         if ( r == -EPIPE )
         {
            fputs("ALSA: playback xrun.\n", stderr);

            r = snd_pcm_prepare(alsa->playback.pcm);
            if ( r < 0 )
            {
               alsa_error("prepare", r);
               return -1;
            }

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

static unsigned int sample_rate( struct device* dv )
{
   struct alsa* alsa = ( struct alsa* ) dv->local;

   return alsa->playback.rate;
}

/* Close ALSA device and clear any allocations */

static void clear( struct device* dv )
{
   struct alsa* alsa = ( struct alsa* ) dv->local;

   pcm_close(&alsa->capture);
   pcm_close(&alsa->playback);
   free(dv->local);
}

static struct device_ops alsa_ops = {
        .pollfds = pollfds,
        .handle = handle,
        .sample_rate = sample_rate,
        .start = start,
        .clear = clear
};

int setup_alsa_device( struct sc1000* sc1000_engine, struct audio_interface* audio_interface, int buffer_size )
{
   struct alsa* alsa;

   alsa = malloc(sizeof *alsa);
   if ( alsa == NULL )
   {
      perror("malloc");
      return -1;
   }

   print_audio_interface_info(audio_interface);

   bool needs_plughw = !audio_interface->supports_16bit_pcm || !audio_interface->supports_48k_samplerate;

   char device_name[64];
   create_alsa_device_id_string(device_name, sizeof(device_name), audio_interface->device_id, audio_interface->subdevice_id, needs_plughw);
   printf("Opening device %s with buffersize %i...", device_name, buffer_size);

   if ( pcm_open(&alsa->playback, device_name, SND_PCM_STREAM_PLAYBACK, buffer_size, audio_interface->period) < 0 )
   {
      fputs("Failed to open device for playback.\n", stderr);
      printf(" failed!\n");
      goto fail_capture;
   }

   device_init(&sc1000_engine->scratch_deck.device, &alsa_ops);
   device_init(&sc1000_engine->beat_deck.device, &alsa_ops);

   sc1000_engine->scratch_deck.device.local = alsa;
   sc1000_engine->beat_deck.device.local = alsa;

   printf(" success!\n");

   return 0;

   fail_capture:
   pcm_close(&alsa->capture);
   return -1;
}

int alsa_init( struct sc1000* sc1000_engine, int buffer_size )
{
   printf("alsa_init\n");
   fill_audio_interface_info();

   if(!audio_interfaces[0].is_internal && audio_interfaces[0].is_present)
   {
      return setup_alsa_device(sc1000_engine, &audio_interfaces[0], buffer_size);
   }
   else if(!audio_interfaces[1].is_internal && audio_interfaces[1].is_present)
   {
      return setup_alsa_device(sc1000_engine, &audio_interfaces[1], buffer_size);
   }
   else
   {
      return setup_alsa_device(sc1000_engine, &audio_interfaces[0], buffer_size);
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
