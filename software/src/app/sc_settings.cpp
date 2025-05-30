//
// Created by lodsb on 31-Mar-25.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sc_control_mapping.h"
#include "sc_settings.h"
#include "global.h"

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

void add_mapping_to_list( struct mapping **maps, IOType type, unsigned char deck_no, unsigned char *buf, unsigned char port, unsigned char pin, bool pullup, EdgeType edge_type, ActionType action, unsigned char parameter )
{
   struct mapping *new_map = (struct mapping *)malloc(sizeof(struct mapping));

   new_map->next = nullptr;

   new_map->type      = type;
   new_map->pin       = pin;
   new_map->gpio_port = port;
   new_map->pullup    = pullup;

   new_map->debounce = 0;

   if (buf == nullptr)
   {
      new_map->midi_command_bytes[0] = 0x00;
      new_map->midi_command_bytes[1] = 0x00;
      new_map->midi_command_bytes[2] = 0x00;
   }
   else
   {
      new_map->midi_command_bytes[0] = buf[0];
      new_map->midi_command_bytes[1] = buf[1];
      new_map->midi_command_bytes[2] = buf[2];
   }

   new_map->edge_type = edge_type;
   new_map->action_type = action;
   new_map->parameter = parameter;

   new_map->deck_no = deck_no;

   printf("Adding Mapping - ty:%d po:%d pn%x pl:%x ed%x mid:%x:%x:%x- dn:%d, a:%d, p:%d\n", new_map->type, new_map->gpio_port, new_map->pin, new_map->pullup, new_map->edge_type, new_map->midi_command_bytes[0], new_map->midi_command_bytes[1], new_map->midi_command_bytes[2], new_map->deck_no, new_map->action_type, new_map->parameter);

   if (*maps == nullptr)
   {
      *maps = new_map;
   }
   else
   {
      struct mapping *last_map = *maps;

      while (last_map->next != nullptr)
      {
         last_map = last_map->next;
      }

      last_map->next = new_map;
   }
}

// Add a mapping from an action string and other params
void add_config_mapping_to_list( struct mapping **maps, IOType type, unsigned char *buf, unsigned char port, unsigned char pin, bool pullup, EdgeType edge_type, char *actions )
{
   unsigned char deck_no;
   ActionType action;

   unsigned char parameter = 0;

   printf("config mapping\n");

   // Extract deck no from action (CHx)
   if (actions[2] == '0')
      deck_no = 0;
   if (actions[2] == '1')
      deck_no = 1;

   // figure out which action it is
   if (strstr(actions + 4, "CUE") != NULL)
      action = CUE;
   if (strstr(actions + 4, "DELETECUE") != NULL)
      action = DELETECUE;
   else if (strstr(actions + 4, "SHIFTON") != NULL)
      action = SHIFTON;
   else if (strstr(actions + 4, "SHIFTOFF") != NULL)
      action = SHIFTOFF;
   else if (strstr(actions + 4, "STARTSTOP") != NULL)
      action = STARTSTOP;
   else if (strstr(actions + 4, "GND") != NULL)
      action = GND;
   else if (strstr(actions + 4, "NEXTFILE") != NULL)
      action = NEXTFILE;
   else if (strstr(actions + 4, "PREVFILE") != NULL)
      action = PREVFILE;
   else if (strstr(actions + 4, "RANDOMFILE") != NULL)
      action = RANDOMFILE;
   else if (strstr(actions + 4, "NEXTFOLDER") != NULL)
      action = NEXTFOLDER;
   else if (strstr(actions + 4, "PREVFOLDER") != NULL)
      action = PREVFOLDER;
   else if (strstr(actions + 4, "PITCH") != NULL)
      action = PITCH;
   else if (strstr(actions + 4, "JOGPIT") != NULL)
      action = JOGPIT;
   else if (strstr(actions + 4, "JOGPSTOP") != NULL)
      action = JOGPSTOP;
   else if (strstr(actions + 4, "RECORD") != NULL)
      action = RECORD;
   else if (strstr(actions + 4, "VOLUME") != NULL)
      action = VOLUME;
   else if (strstr(actions + 4, "VOLUP") != NULL)
      action = VOLUP;
   else if (strstr(actions + 4, "VOLDOWN") != NULL)
      action = VOLDOWN;
   else if (strstr(actions + 4, "VOLUHOLD") != NULL)
      action = VOLUHOLD;
   else if (strstr(actions + 4, "VOLDHOLD") != NULL)
      action = VOLDHOLD;
   else if (strstr(actions + 4, "JOGREVERSE") != NULL)
      action = JOGREVERSE;
   else if (strstr(actions + 4, "SC500") != NULL)
      action = SC500;
   else if (strstr(actions + 4, "NOTE") != NULL)
   {
      action = NOTE;
      parameter = atoi(actions + 8);
   }

   add_mapping_to_list(maps, type, deck_no, buf, port, pin, pullup, edge_type, action, parameter);
}

void sc_settings_load_user_configuration( struct sc_settings* settings, struct mapping** mappings )
{
   FILE* fp;
   char* line = NULL;
   size_t len = 0;
   ssize_t read;
   char* param, * actions;
   char* value;
   unsigned char channel = 0, notenum = 0, control_type = 0, pin = 0, port = 0;
   bool pullup = false;
   EdgeType edge;
   char delim[] = "=";
   char delimc[] = ",";
   unsigned char midi_command[3];
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


   if ( fp != nullptr )
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
               settings->midi_remapped = true;

               control_type = atoi(strtok_r(value, delimc, &valuetok));
               channel = atoi(strtok_r(NULL, delimc, &valuetok));
               notenum = atoi(strtok_r(NULL, delimc, &valuetok));
               edge = static_cast<EdgeType>(atoi(strtok_r(NULL, delimc, &valuetok)));
               actions = strtok_r(NULL, delimc, &valuetok);

               //255 means bind to all notes
               if ( notenum == 255 )
               {

                  // Build MIDI command
                  midi_command[ 0 ] = static_cast<unsigned char>((control_type << 4) | channel);
                  midi_command[ 1 ] = notenum;
                  midi_command[ 2 ] = 0;

                  char tempact[20];
                  for ( midi_command[ 1 ] = 0; midi_command[ 1 ] < 128; midi_command[ 1 ]++ )
                  {
                     sprintf(tempact, "%s%u", actions, midi_command[ 1 ]);
                     add_config_mapping_to_list(
                             mappings,
                             IOType::MIDI,
                             midi_command,
                             0,
                             0,
                             false,
                             edge,
                             tempact);
                  }
               }
                  // otherwise just bind to one note
               else
               {
                  // Build MIDI command
                  midi_command[ 0 ] = static_cast<unsigned char>((control_type << 4) | channel);
                  midi_command[ 1 ] = notenum;
                  midi_command[ 2 ] = 0;
                  add_config_mapping_to_list(
                          mappings,
                          IOType::MIDI,
                          midi_command,
                          0,
                          0,
                          false,
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
               edge = static_cast<EdgeType>(atoi(strtok_r(NULL, delimc, &valuetok)));
               actions = strtok_r(NULL, delimc, &valuetok);

               printf("IO\n");

               add_config_mapping_to_list(
                       mappings,
                       IOType::IO,
                       nullptr,
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