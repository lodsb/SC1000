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
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h> /* mlockall() */

#include <unistd.h>         //Needed for I2C port
#include <fcntl.h>         //Needed for I2C port
#include <sys/ioctl.h>      //Needed for I2C port
#include <linux/i2c-dev.h> //Needed for I2C port
#include <time.h>
#include <dirent.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>

#include "audio/alsa.h"
#include "input/sc_input.h"
#include "input/sc_midimap.h"
#include "player/controller.h"
#include "player/device.h"
#include "player/settings.h"

#include "audio//dummy.h"
#include "player/dicer.h"
#include "player/track.h"
#include "thread/realtime.h"
#include "thread/thread.h"
#include "thread/rig.h"

#include "global/global.h"

#include "xwax.h"

//#define DEFAULT_IMPORTER EXECDIR "/xwax-import"


struct mapping* maps = NULL;

unsigned int count_chars( char* string, char c )
{
   unsigned int count = 0;

   //printf("Checking for commas in %s\n", string);

   do
   {
      if ( (*string) == c )
      {
         count++;
      }
   } while ( (*(string++)) );
   return count;
}

void create_settings_and_load_user_configuration( struct sc_settings* settings )
{
   FILE* fp;
   char* line = NULL;
   size_t len = 0;
   ssize_t read;
   char* param, * actions;
   char* value;
   unsigned char channel = 0, notenum = 0, controlType = 0, pin = 0, pullup = 0, port = 0;
   char edge;
   char delim[] = "=";
   char delimc[] = ",";
   unsigned char midicommand[3];
   char* linetok, * valuetok;
   // set defaults
   settings->period_size = 256;
   settings->buffer_period_factor = 4;
   settings->fader_close_point = 2;
   settings->fader_open_point = 10;
   settings->platter_enabled = 1;
   settings->platter_speed = 2275;
   settings->sample_rate = 48000;
   settings->update_rate = 2000;
   settings->debounce_time = 5;
   settings->hold_time = 100;
   settings->slippiness = 200;
   settings->brake_speed = 3000;
   settings->pitch_range = 50;
   settings->midi_init_delay = 5;
   settings->audio_init_delay = 2;
   settings->volume_amount = 0.03;
   settings->volume_amount_held = 0.001;
   settings->initial_volume = 0.125;
   settings->midi_remapped = 0;
   settings->io_remapped = 0;
   settings->jog_reverse = 0;
   settings->cut_beats = 0;
   settings->importer = DEFAULT_IMPORTER;

   // later we'll check for sc500 pin and use it to set following settings
   settings->disable_volume_adc = 0;
   settings->disable_pic_buttons = 0;

   // Load any settings from config file
   fp = fopen("/media/sda/scsettings.txt", "r");
   if ( fp == NULL )
   {
      // load internal copy instead
      fp = fopen("/var/scsettings.txt", "r");
   }


   if ( fp != NULL )
   {
      while ( (read = getline(&line, &len, fp)) != -1 )
      {
         if ( strlen(line) < 2 || line[ 0 ] == '#' )
         { // Comment or blank line
         }
         else
         {
            param = strtok_r(line, delim, &linetok);
            value = strtok_r(NULL, delim, &linetok);

            if ( strcmp(param, "period_size") == 0 )
            {
               settings->period_size = atoi(value);
            }
            else if ( strcmp(param, "buffer_period_factor") == 0 )
            {
               settings->buffer_period_factor = atoi(value);
            }
            else if ( strcmp(param, "fader_close_point") == 0 )
            {
               settings->fader_close_point = atoi(value);
            }
            else if ( strcmp(param, "fader_open_point") == 0 )
            {
               settings->fader_open_point = atoi(value);
            }
            else if ( strcmp(param, "platter_enabled") == 0 )
            {
               settings->platter_enabled = atoi(value);
            }
            else if ( strcmp(param, "disable_volume_adc") == 0 )
            {
               settings->disable_volume_adc = atoi(value);
            }
            else if ( strcmp(param, "platter_speed") == 0 )
            {
               settings->platter_speed = atoi(value);
            }
            else if ( strcmp(param, "sample_rate") == 0 )
            {
               settings->sample_rate = atoi(value);
            }
            else if ( strcmp(param, "update_rate") == 0 )
            {
               settings->update_rate = atoi(value);
            }
            else if ( strcmp(param, "debounce_time") == 0 )
            {
               settings->debounce_time = atoi(value);
            }
            else if ( strcmp(param, "hold_time") == 0 )
            {
               settings->hold_time = atoi(value);
            }
            else if ( strcmp(param, "slippiness") == 0 )
            {
               settings->slippiness = atoi(value);
            }
            else if ( strcmp(param, "brake_speed") == 0 )
            {
               settings->brake_speed = atoi(value);
            }
            else if ( strcmp(param, "pitch_range") == 0 )
            {
               settings->pitch_range = atoi(value);
            }
            else if ( strcmp(param, "jog_reverse") == 0 )
            {
               settings->jog_reverse = atoi(value);
            }
            else if ( strcmp(param, "cut_beats") == 0 )
            {
               settings->cut_beats = atoi(value);
            }
            else if ( strstr(param, "midii") != NULL )
            {
               settings->midi_remapped = 1;
               controlType = atoi(strtok_r(value, delimc, &valuetok));
               channel = atoi(strtok_r(NULL, delimc, &valuetok));
               notenum = atoi(strtok_r(NULL, delimc, &valuetok));
               edge = atoi(strtok_r(NULL, delimc, &valuetok));
               actions = strtok_r(NULL, delimc, &valuetok);

               //255 means bind to all notes
               if ( notenum == 255 )
               {

                  // Build MIDI command
                  midicommand[ 0 ] = (controlType << 4) | channel;
                  midicommand[ 1 ] = notenum;
                  midicommand[ 2 ] = 0;

                  char tempact[20];
                  for ( midicommand[ 1 ] = 0; midicommand[ 1 ] < 128; midicommand[ 1 ]++ )
                  {
                     sprintf(tempact, "%s%u", actions, midicommand[ 1 ]);
                     add_config_mapping(
                             &maps,
                             MAP_MIDI,
                             midicommand,
                             0,
                             0,
                             0,
                             edge,
                             tempact);
                  }
               }
                  // otherwise just bind to one note
               else
               {
                  // Build MIDI command
                  midicommand[ 0 ] = (controlType << 4) | channel;
                  midicommand[ 1 ] = notenum;
                  midicommand[ 2 ] = 0;
                  add_config_mapping(
                          &maps,
                          MAP_MIDI,
                          midicommand,
                          0,
                          0,
                          0,
                          edge,
                          actions);
               }
            }
            else if ( strstr(param, "io") != NULL )
            {
               settings->io_remapped = 1;
               unsigned int commaCount = count_chars(value, ',');
               //printf("Found io %s - comacount %d\n", value, commaCount);
               port = 0;
               if ( commaCount == 4 )
               {
                  port = atoi(strtok_r(value, delimc, &valuetok));
                  pin = atoi(strtok_r(NULL, delimc, &valuetok));
               }
               else
               {
                  pin = atoi(strtok_r(value, delimc, &valuetok));
               }
               pullup = atoi(strtok_r(NULL, delimc, &valuetok));
               edge = atoi(strtok_r(NULL, delimc, &valuetok));
               actions = strtok_r(NULL, delimc, &valuetok);
               add_config_mapping(
                       &maps,
                       MAP_IO,
                       NULL,
                       port,
                       pin,
                       pullup,
                       edge,
                       actions);
            }
            else if ( strcmp(param, "midi_init_delay") == 0 )
            {// Literally just a sleep to allow USB devices longer to initialize
               settings->midi_init_delay = atoi(value);
            }
            else if ( strcmp(param, "audio_init_delay") == 0 )
            {
               fprintf(stderr, "audio init settings");
               // Literally just a sleep to allow USB devices longer to initialize
               settings->audio_init_delay = atoi(value);
            }
            else
            {
               printf("Unrecognised configuration line - Param : %s , value : %s\n", param, value);
            }
         }
      }
   }


   printf("ps %d, bpf %d, fcp %d, fop %d, pe %d, ps %d, sr %d, ur %d\n",
          settings->period_size,
          settings->buffer_period_factor,
          settings->fader_close_point,
          settings->fader_open_point,
          settings->platter_enabled,
          settings->platter_speed,
          settings->sample_rate,
          settings->update_rate);

   if ( fp )
   {
      fclose(fp);
   }
   if ( line )
   {
      free(line);
   }
}

void sig_handler( int signo )
{
   if ( signo == SIGINT )
   {
      printf("received SIGINT\n");
      exit(0);
   }
}

int main( int argc, char* argv[] )
{

   int rc = -1, priority;
   bool use_mlock;

   if ( signal(SIGINT, sig_handler) == SIG_ERR )
   {
      printf("\ncan't catch SIGINT\n");
      exit(1);
   }

   if ( setlocale(LC_ALL, "") == NULL )
   {
      fprintf(stderr, "Could not honour the local encoding\n");
      return -1;
   }
   if ( thread_global_init() == -1 )
   {
      return -1;
   }
   if ( rig_init() == -1 )
   {
      return -1;
   }
   rt_init(&g_rt);

   use_mlock = false;

   create_settings_and_load_user_configuration(&g_sc1000_settings);

   sc1000_init(&g_sc1000_engine, &g_sc1000_settings, &g_rt);
   sc1000_load_sample_folders(&g_sc1000_engine);

   rc = EXIT_FAILURE; /* until clean exit */

   // Start input processing thread
   start_sc_input_thread();

   // Start realtime stuff
   priority = 0;

   if ( rt_start(&g_rt, priority) == -1 )
   {
      return -1;
   }

   if ( use_mlock && mlockall(MCL_CURRENT) == -1 )
   {
      perror("mlockall");
      goto out_rt;
   }

   // Main loop

   fprintf(stderr, "WIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIP\n\n");

   if ( rig_main() == -1 )
   {
      goto out_interface;
   }

   // Exit

   rc = EXIT_SUCCESS;
   fprintf(stderr, "Exiting cleanly...\n");

   out_interface:
   out_rt:
   rt_stop(&g_rt);

   sc1000_clear(&g_sc1000_engine);

   rig_clear();
   thread_global_clear();

   if ( rc == EXIT_SUCCESS )
   {
      fprintf(stderr, "Done.\n");
   }

   return rc;
}
