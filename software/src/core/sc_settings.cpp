//
// Created by lodsb on 31-Mar-25.
//

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <iostream>
#include <fstream>

#include <json.hpp>

#include "sc_control_mapping.h"
#include "sc_settings.h"
#include "global.h"

namespace sc {
namespace config {

// Default importer path
constexpr const char* DEFAULT_IMPORTER_PATH = "/root/xwax-import";

static unsigned int count_chars( const char* string, char c )
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

void add_mapping_to_list( struct mapping **maps, IOType type, unsigned char deck_no, unsigned char *buf, unsigned char port, unsigned char pin, bool pullup, EventType edge_type, ActionType action, unsigned char parameter )
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

void get_deck_action_parameter(unsigned char& deck_no, ActionType& action, unsigned char& parameter, char* actions)
{
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
}

NLOHMANN_JSON_SERIALIZE_ENUM( EventType, {
    {EventType::BUTTON_HOLDING, "button_holding"},
    {EventType::BUTTON_HOLDING_SHIFTED, "button_holding_shifted"},
    {EventType::BUTTON_PRESSED, "button_pressed"},
    {EventType::BUTTON_PRESSED_SHIFTED, "button_pressed_shifted"},
    {EventType::BUTTON_RELEASED, "button_released"},
})

NLOHMANN_JSON_SERIALIZE_ENUM( MIDIStatusType, {
    {MIDIStatusType::MIDI_NOTE_ON, "midi_note_on"},
    {MIDIStatusType::MIDI_NOTE_OFF, "midi_note_off"},
    {MIDIStatusType::MIDI_CC, "midi_cc"},
    {MIDIStatusType::MIDI_PB, "midi_pb"},
})

NLOHMANN_JSON_SERIALIZE_ENUM( ActionType, {
   {ActionType::CUE, "cue"},
   {ActionType::SHIFTON, "shift_on"},
   {ActionType::SHIFTOFF, "shift_off"},
   {ActionType::STARTSTOP, "start_stop"},
   {ActionType::START, "start"},
   {ActionType::STOP, "stop"},
   {ActionType::PITCH, "pitch"},
   {ActionType::NOTE, "note"},
   {ActionType::GND, "gnd"},
   {ActionType::VOLUME, "volume"},
   {ActionType::NEXTFILE, "next_file"},
   {ActionType::PREVFILE, "prev_file"},
   {ActionType::RANDOMFILE, "random_file"},
   {ActionType::NEXTFOLDER, "next_folder"},
   {ActionType::PREVFOLDER, "prev_folder"},
   {ActionType::RECORD, "record"},
   {ActionType::VOLUP, "volume_up"},
   {ActionType::VOLDOWN, "volume_down"},
   {ActionType::JOGPIT, "jog_pit"},
   {ActionType::DELETECUE, "delete_cue"},
   {ActionType::SC500, "sc500"},
   {ActionType::VOLUHOLD, "volume_up_hold"},
   {ActionType::VOLDHOLD, "volume_down_hold"},
   {ActionType::JOGPSTOP, "jog_pstop"},
   {ActionType::JOGREVERSE, "jog_reverse"},
   {ActionType::BEND, "bend"},
   {ActionType::NOTHING, "nothing"},
})

nlohmann::json settings_to_json(sc_settings* settings)
{
   nlohmann::json json;

   json["period_size"]=settings->period_size;
   json["buffer_period_factor"]=settings->buffer_period_factor;
   json["sample_rate"]=settings->sample_rate;
   json["single_vca"]=settings->single_vca;
   json["double_cut"]=settings->double_cut;
   json["hamster"]=settings->hamster;
   json["fader_open_point"]=settings->fader_open_point;
   json["fader_close_point"]=settings->fader_close_point;
   json["update_rate"]=settings->update_rate;
   json["platter_enabled"]=settings->platter_enabled;
   json["platter_speed"]=settings->platter_speed;
   json["debounce_time"]=settings->debounce_time;
   json["hold_time"]=settings->hold_time;
   json["slippiness"]=settings->slippiness;
   json["brake_speed"]=settings->brake_speed;
   json["pitch_range"]=settings->pitch_range;
   json["midi_init_delay"]=settings->midi_init_delay;
   json["audio_init_delay"]=settings->audio_init_delay;
   json["disable_volume_adc"]=settings->disable_volume_adc;
   json["disable_pic_buttons"]=settings->disable_pic_buttons;
   json["volume_amount"]=settings->volume_amount;
   json["volume_amount_held"]=settings->volume_amount_held;
   json["jog_reverse"]=settings->jog_reverse;
   json["cut_beats"]=settings->cut_beats;

   return json;
}

void settings_from_json(sc_settings* settings, const nlohmann::json& json)
{
   // Set defaults first - these are used if JSON keys are missing
   settings->period_size = json.value("period_size", 256u);
   settings->buffer_period_factor = json.value("buffer_period_factor", 4u);
   settings->sample_rate = json.value("sample_rate", 48000);
   settings->single_vca = json.value("single_vca", '\0');
   settings->double_cut = json.value("double_cut", '\0');
   settings->hamster = json.value("hamster", '\0');
   settings->fader_close_point = json.value("fader_close_point", 2);
   settings->fader_open_point = json.value("fader_open_point", 10);
   settings->update_rate = json.value("update_rate", 2000);
   settings->platter_enabled = json.value("platter_enabled", 1);
   settings->platter_speed = json.value("platter_speed", 2275);
   settings->debounce_time = json.value("debounce_time", 5);
   settings->hold_time = json.value("hold_time", 100);
   settings->slippiness = json.value("slippiness", 200);
   settings->brake_speed = json.value("brake_speed", 3000);
   settings->pitch_range = json.value("pitch_range", 50);
   settings->midi_init_delay = json.value("midi_init_delay", 5u);
   settings->audio_init_delay = json.value("audio_init_delay", 2u);
   settings->disable_volume_adc = json.value("disable_volume_adc", 0);
   settings->disable_pic_buttons = json.value("disable_pic_buttons", 0);
   settings->volume_amount = json.value("volume_amount", 0.03);
   settings->volume_amount_held = json.value("volume_amount_held", 0.001);
   settings->initial_volume = json.value("initial_volume", 0.125);
   settings->midi_remapped = 0;
   settings->io_remapped = 0;
   settings->jog_reverse = json.value("jog_reverse", 0);
   settings->cut_beats = json.value("cut_beats", 0);
   settings->importer = DEFAULT_IMPORTER_PATH;
}

nlohmann::json midi_command_to_json(MIDIStatusType midi_status, EventType event, unsigned char channel, unsigned char parameter1, unsigned char parameter2, unsigned char deck, ActionType action)
{
   nlohmann::json json;

   json["type"]=midi_status;
   json["shifted"]= event==EventType::BUTTON_PRESSED_SHIFTED;
   json["channel"]=channel;
   json["parameter1"]=parameter1;
   json["parameter2"]=parameter2;
   json["deck"]= deck == 0 ? "beats" : "scratch";
   json["action"]=action;

   return json;
}

void add_midi_mapping_from_json(mapping** mappings, const nlohmann::json& json)
{
   const MIDIStatusType midi_status = json["type"].template get<MIDIStatusType>();
   const EventType event = json["shifted"].template get<bool>() ? EventType::BUTTON_PRESSED_SHIFTED : EventType::BUTTON_PRESSED;
   const unsigned char channel = json["channel"].template get<unsigned char>();
   const unsigned char parameter1 = json["parameter1"].template get<unsigned char>();
   const unsigned char parameter2 = json["parameter2"].template get<unsigned char>();
   const auto deck_string = json["deck"].template get<std::string>();
   const unsigned char deck_no = deck_string == "beats" ? 0 : 1;
   const ActionType action = json["action"].template get<ActionType>();

   unsigned char midi_command[3];

   const auto control_type_byte = static_cast<unsigned char>(midi_status);

   if (midi_status == MIDI_NOTE_ON && parameter1 == 255) // all note ons
   {
      for (unsigned char note_number = 0; note_number < 128; note_number++)
      {
         midi_command[ 0 ] = static_cast<unsigned char>((control_type_byte << 4) | channel);
         midi_command[ 1 ] = note_number;
         midi_command[ 2 ] = 0;

         if (action == ActionType::NOTE)
         {
            add_mapping_to_list(mappings, IOType::MIDI, deck_no, midi_command, 0, 0, false, event, action, note_number);
         }
         else
         {
            add_mapping_to_list(mappings, IOType::MIDI, deck_no, midi_command, 0, 0, false, event, action, 0);
         }
      }
   }
   else
   {
      midi_command[ 0 ] = static_cast<unsigned char>((control_type_byte << 4) | channel);
      midi_command[ 1 ] = parameter1;
      midi_command[ 2 ] = 0;

      add_mapping_to_list(mappings, IOType::MIDI, deck_no, midi_command, 0, 0, false, event, action, parameter2);
   }
}

nlohmann::json gpio_command_to_json(EventType event, unsigned char port, unsigned char pin, bool pull_up, unsigned char deck, ActionType action)
{
   nlohmann::json json;

   json["event"]=event;
   json["port"]=port;
   json["pin"]=pin;
   json["pull_up"]=pull_up;
   json["deck"]= deck == 0 ? "beats" : "scratch";
   json["action"]=action;

   return json;
}

void add_gpio_mapping_from_json(mapping** mappings, const nlohmann::json& json)
{
   const EventType event = json["event"].template get<EventType>();
   const unsigned char port = json["port"].template get<unsigned char>();
   const unsigned char pin = json["pin"].template get<unsigned char>();
   const bool pull_up = json["pull_up"].template get<bool>();
   const auto deck_string = json["deck"].template get<std::string>();
   const unsigned char deck_no = deck_string == "beats" ? 0 : 1;
   ActionType action = json["action"].template get<ActionType>();

   add_mapping_to_list(mappings, IOType::IO, deck_no, nullptr, port, pin, pull_up, event, action, 0);
}

void sc_settings_old_format( sc_settings* settings, mapping** mappings )
{
   FILE* fp;
   char* line = NULL;
   size_t len = 0;
   ssize_t read;
   char* param, * actions;
   char* value;
   unsigned char channel = 0, notenum = 0, control_type = 0, pin = 0, port = 0;
   bool pullup = false;
   EventType edge;
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
   settings->importer = DEFAULT_IMPORTER_PATH;

   // later we'll check for sc500 pin and use it to set following settings
   settings->disable_volume_adc = 0;
   settings->disable_pic_buttons = 0;

   // Load any settings from config file
   fp = fopen("/media/sda/scsettings.txt", "r");
   if ( fp == nullptr )
   {
      // load internal copy instead
      fp = fopen("/var/scsettings.txt", "r");

      // try loading
      if (fp == nullptr)
      {
         // probably started from the build-directory
         fp = fopen("../scsettings.txt", "r");
      }
   }

   unsigned char deck_no;
   ActionType action;

   unsigned char parameter = 0;

   auto gpios = nlohmann::json::array();
   auto midis = nlohmann::json::array();

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
               edge = static_cast<EventType>(atoi(strtok_r(NULL, delimc, &valuetok)));
               actions = strtok_r(NULL, delimc, &valuetok);

               std::cout << "midi orig" << std::endl;

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

                     // FIXME!!!
                     get_deck_action_parameter(deck_no, action, parameter, tempact);
                     add_mapping_to_list(mappings, IOType::MIDI, deck_no, midi_command, 0, 0, false, edge, action, parameter);
                  }

                  midis.push_back(midi_command_to_json(
                     static_cast<MIDIStatusType>(control_type),
                     edge,
                     channel,
                     notenum,
                     parameter,
                     deck_no,
                     action
                  ));
               }
                  // otherwise just bind to one note
               else
               {
                  // Build MIDI command
                  midi_command[ 0 ] = static_cast<unsigned char>((control_type << 4) | channel);
                  midi_command[ 1 ] = notenum;
                  midi_command[ 2 ] = 0;

                  get_deck_action_parameter(deck_no, action, parameter, actions);
                  add_mapping_to_list(mappings, IOType::MIDI, deck_no, midi_command, 0, 0, false, edge, action, parameter);

                  midis.push_back(midi_command_to_json(
                     static_cast<MIDIStatusType>(control_type),
                     edge,
                     channel,
                     notenum,
                     parameter,
                     deck_no,
                     action
                  ));
               }
               std::cout << "midi json" << std::endl;
               add_midi_mapping_from_json(mappings, midis.back());
            }
            else if ( strstr(param, "gpio") != NULL )
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
               edge = static_cast<EventType>(atoi(strtok_r(NULL, delimc, &valuetok)));
               actions = strtok_r(NULL, delimc, &valuetok);

               get_deck_action_parameter(deck_no, action, parameter, actions);

               std::cout << "io orig" << parameter << std::endl;

               add_mapping_to_list(mappings, IOType::IO, deck_no, nullptr, port, pin, pullup, edge, action, parameter);

               gpios.push_back(gpio_command_to_json(
                     edge,
                     port,
                     pin,
                     pullup,
                     deck_no,
                     action
                  ));

               std::cout << "io json" << parameter << std::endl;

               add_gpio_mapping_from_json(mappings, gpios.back());
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
   else
   {
      std::cerr << "Could not open any settings file, exiting" << std::endl;
      exit(-1);
   }

   nlohmann::json json_main;
   nlohmann::json json_settings = settings_to_json(settings);
   json_main["sc1000"] = json_settings;
   json_main["gpio_mapping"] = gpios;
   json_main["midi_mapping"] = midis;
   std::cout << "json settings: " << std::setw(4) << json_main << std::endl;

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

void load_json_config( sc_settings* settings, mapping** mappings )
{
   std::ifstream f;
   f.open("/media/sda/sc_settings.json", std::ios::in);

   if ( f.fail() )
   {
      f.open("/var/sc_settings.json", std::ios::in);
      if (f.fail() )
      {
         // probably started native from the build dir
         f.open("../sc_settings.json", std::ios::in);
      }
   }

   if (f.fail())
   {
      std::cerr << "Could not open any settings file, exiting" << std::endl;
      exit(-1);
   }

   try
   {
      auto json_main = nlohmann::json::parse(f);

      // Get settings section, use empty object if missing
      auto json_settings = json_main.value("sc1000", nlohmann::json::object());
      settings_from_json(settings, json_settings);

      // Load GPIO mappings if present
      if (json_main.contains("gpio_mapping") && json_main["gpio_mapping"].is_array())
      {
         for (const auto& m : json_main["gpio_mapping"])
         {
            try {
               add_gpio_mapping_from_json(mappings, m);
            } catch (const nlohmann::json::exception& e) {
               std::cerr << "Warning: Invalid GPIO mapping entry: " << e.what() << std::endl;
            }
         }
      }

      // Load MIDI mappings if present
      if (json_main.contains("midi_mapping") && json_main["midi_mapping"].is_array())
      {
         for (const auto& m : json_main["midi_mapping"])
         {
            try {
               add_midi_mapping_from_json(mappings, m);
            } catch (const nlohmann::json::exception& e) {
               std::cerr << "Warning: Invalid MIDI mapping entry: " << e.what() << std::endl;
            }
         }
      }
   }
   catch (const nlohmann::json::parse_error& e)
   {
      std::cerr << "JSON parse error: " << e.what() << std::endl;
      std::cerr << "Using default settings" << std::endl;
      // Apply defaults via empty JSON object
      settings_from_json(settings, nlohmann::json::object());
   }
   catch (const nlohmann::json::exception& e)
   {
      std::cerr << "JSON error: " << e.what() << std::endl;
      std::cerr << "Using default settings" << std::endl;
      settings_from_json(settings, nlohmann::json::object());
   }
}

} // namespace config
} // namespace sc

// C API - extern "C" function for use by C code
void sc_settings_load_user_configuration( sc_settings* settings, mapping** mappings )
{
   sc::config::load_json_config(settings, mappings);
}