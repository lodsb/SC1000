#include <unistd.h>
#include <stdint.h>

#include "../audio/alsa.h"
#include "../util/debug.h"

#include "../player/deck.h"
#include "../player/track.h"

#include "audio_engine.h"
#include "sc1000.h"
#include "sc_settings.h"

const char* BEEPS[3] = {
        "----------",          // Start Recording
        "- - - - - - - - -",   // Stop Recording
        "--__--__--__--__--__" // Recording error
};

void sc1000_setup(struct sc1000* engine,
                  struct rt *rt)
{
   printf("sc1000_init\n");

   struct sc_settings* settings = (struct sc_settings*) malloc(sizeof(struct sc_settings));

   engine->settings = settings;
   engine->mappings = NULL;

   settings_load_user_configuration(engine->settings, &engine->mappings);

   alsa_init(engine, settings);

   // Create two decks, both pointed at the same audio device
   deck_init(&engine->scratch_deck, settings);
   deck_init(&engine->beat_deck, settings);

   rt_set_sc1000(rt, engine);

   // point deck1's output at deck0, it will be summed in
   //engine->scratch_deck.device.beat_player = engine->beat_deck.device.scratch_player;

   // Tell deck0 to just play without considering inputs
   engine->beat_deck.player.just_play = 1;

   alsa_clear_config_cache();
}

void sc1000_load_sample_folders(struct sc1000* engine)
{
   // Check for samples folder
   if (access("/media/sda/samples", F_OK) == -1)
   {
      // Not there, so presumably the boot script didn't manage to mount the drive
      // Maybe it hasn't initialized yet, or at least wasn't at boot time
      // We have to do it ourselves

      // Timeout after 12 sec, in which case emergency samples will be loaded
      for (int uscnt = 0; uscnt < 12; uscnt++)
      {
         printf("Waiting for USB stick...\n");
         // Wait for /dev/sda1 to show up and then mount it
         if (access("/dev/sda1", F_OK) != -1)
         {
            printf("Found USB stick, mounting!\n");
            system("/bin/mount /dev/sda1 /media/sda");
            break;
         }
         else
         {
            // If not here yet, wait a second then check again
            sleep(1);
         }
      }
   }

   deck_load_folder(&engine->beat_deck, "/media/sda/beats/");
   deck_load_folder(&engine->scratch_deck, "/media/sda/samples/");

   if (!engine->scratch_deck.files_present)
   {
      // Load the default sentence if no sample files found on usb stick
      player_set_track(&engine->scratch_deck.player, track_acquire_by_import(engine->scratch_deck.importer, "/var/scratchsentence.mp3"));
      printf("set track ok");
      cues_load_from_file(&engine->scratch_deck.cues, engine->scratch_deck.player.track->path);
      printf("set cues ok");
      // Set the time back a bit so the sample doesn't start too soon
      engine->scratch_deck.player.target_position = -4.0;
      engine->scratch_deck.player.position = -4.0;
   }
}

void sc1000_clear(struct sc1000* engine)
{

   deck_clear(&engine->beat_deck);
   deck_clear(&engine->scratch_deck);

   if (engine->ops->clear != NULL)
      engine->ops->clear(engine);
}

/*
static void synthesize_beep( struct device* dv, signed short* pcm, unsigned long frames  )
{// Add beeps, if we need to
   if ( dv->scratch_player->playing_beep != -1 )
   {
      for ( int i = 0; i < frames * 2; i++ )
      {
         char curChar = BEEPS[ dv->scratch_player->playing_beep ][ dv->scratch_player->beep_pos / BEEPSPEED ];
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
               int32_t adder = ( int32_t ) pcm[i] +
                               (sin((( double ) dv->scratch_player->beep_pos / (48000.0 / ( double ) beepFreq)) * 6.2831) * 20000.0);

               // saturate add
               if ( adder > INT16_MAX )
               {
                  adder = INT16_MAX;
               }
               if ( adder < INT16_MIN )
               {
                  adder = INT16_MIN;
               }

               pcm[i] = ( int16_t ) adder;
            }
            dv->scratch_player->beep_pos++;
         }
         else
         {
            dv->scratch_player->playing_beep = -1;
            dv->scratch_player->beep_pos = 0;
            break;
         }
      }
   }
}
 */

/*
static void record_to_file( struct device* dv, signed short* pcm, unsigned long frames )
{
   if ( dv->scratch_player->recording )
   {
      fwrite(pcm, frames * DEVICE_CHANNELS * sizeof(signed short), 1, dv->scratch_player->recording_file);
   }
}
*/

void sc1000_audio_engine_init(struct sc1000* engine, struct sc1000_ops *ops)
{
   debug("%p", dv);
   engine->fault = false;
   engine->ops = ops;
}

/*
 * Start the device inputting and outputting audio
 */

void sc1000_audio_engine_start(struct sc1000* engine)
{
   if (engine->ops->start != NULL)
      engine->ops->start(engine);
}

/*
 * Stop the device
 */

void sc1000_audio_engine_stop(struct sc1000* engine)
{
   if (engine->ops->stop != NULL)
      engine->ops->stop(engine);
}

/*
 * Get file descriptors which should be polled for this device
 *
 * Do not return anything for callback-based audio systems. If the
 * return value is > 0, there must be a handle() function available.
 *
 * Return: the number of pollfd filled, or -1 on error
 */

ssize_t sc1000_audio_engine_pollfds(struct sc1000* engine, struct pollfd *pe, size_t z)
{
   if (engine->ops->pollfds != NULL)
      return engine->ops->pollfds(engine, pe, z);
   else
      return 0;
}

/*
 * Handle any available input or output on the device
 *
 * This function can be called when there is activity on any file
 * descriptor, not specifically one returned by this device.
 */

void sc1000_audio_engine_handle(struct sc1000* engine)
{
   if (engine->fault)
      return;

   if (engine->ops->handle == NULL)
      return;

   if (engine->ops->handle(engine) != 0) {
      engine->fault = true;
      fputs("Error handling audio device; disabling it\n", stderr);
   }
}

void sc1000_audio_engine_process(struct sc1000* engine, signed short* pcm, unsigned long frames)
{
   static int16_t next_recording_number = 0;

   if ( engine->scratch_deck.player.recording_started && !engine->scratch_deck.player.recording )
   {
      next_recording_number = 0;
      while ( 1 )
      {
         sprintf(engine->scratch_deck.player.recording_file_name, "/media/sda/sc%06d.raw", next_recording_number);
         if ( access(engine->scratch_deck.player.recording_file_name, F_OK) != -1 )
         {
            // file exists
            next_recording_number++;

            // If we've reached max files then abort (very unlikely, heh)
            if ( next_recording_number == INT16_MAX )
            {
               printf("Too many recordings\n");
               next_recording_number = -1;
               engine->scratch_deck.player.playing_beep = BEEP_RECORDINGERROR;
               engine->scratch_deck.player.recording_started = 0;
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
         printf("Opening file %s for recording\n", engine->scratch_deck.player.recording_file_name);
         engine->scratch_deck.player.recording_file = fopen(engine->scratch_deck.player.recording_file_name, "w");

         // On error, don't start
         if (engine->scratch_deck.player.recording_file == NULL )
         {
            printf("Failed to open recording file\n");
            engine->scratch_deck.player.recording_started = 0;
            engine->scratch_deck.player.playing_beep = BEEP_RECORDINGERROR;
         }
         else
         {
            engine->scratch_deck.player.recording = 1;
            engine->scratch_deck.player.playing_beep = BEEP_RECORDINGSTART;
         }
      }
   }

   audio_engine_process(engine, pcm, frames);

   //record_to_file(dv, pcm, frames);
   //synthesize_beep(dv, pcm, frames);


   if ( ! engine->scratch_deck.player.recording_started &&  engine->scratch_deck.player.recording )
   {
      static char sync_command_line[300];

      fflush( engine->scratch_deck.player.recording_file);
      fclose( engine->scratch_deck.player.recording_file);
      sprintf(sync_command_line, "/bin/sync %s",  engine->scratch_deck.player.recording_file_name);
      system(sync_command_line);
      engine->scratch_deck.player.recording = false;
      engine->scratch_deck.player.playing_beep = BEEP_RECORDINGSTOP;
   }

}
