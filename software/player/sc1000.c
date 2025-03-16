#include <unistd.h>

#include "../audio/alsa.h"

#include "deck.h"

#include "sc1000.h"

void sc1000_init(struct sc1000* engine, struct sc_settings* settings,
                 struct rt *rt)
{
   printf("sc1000_init\n");

   alsa_init(engine, settings);

   // Create two decks, both pointed at the same audio device
   deck_init(&engine->scratch_deck, rt, settings, 0);
   deck_init(&engine->beat_deck, rt, settings, 1);

   // point deck1's output at deck0, it will be summed in
   engine->scratch_deck.device.beat_player = engine->beat_deck.device.scratch_player;

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
}