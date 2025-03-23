#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>


#include "../app/sc_input.h"
#include "sc_control_mapping.h"

/*
void add_IO_mapping(struct mapping **maps, unsigned char Pin, bool Pullup, bool Edge, unsigned char DeckNo, unsigned char Action, unsigned char Param)
{
	struct mapping *new_map = (struct mapping *)malloc(sizeof(struct mapping));
	new_map->Pin = Pin;
	new_map->Pullup = Pullup;
	new_map->Edge = Edge;
	new_map->DeckNo = DeckNo;
	new_map->Action = Action;
	new_map->Param = Param;
	new_map->next = NULL;
	new_map->Type = MAP_IO;
	new_map->port = 0;
	//printf("Adding Mapping - pn%x pl:%x ed%x - dn:%d, a:%d, p:%d\n", Pin, Pullup, Edge, DeckNo, Action, Param);
	if (*maps == NULL)
	{
		*maps = new_map;
	}
	else
	{
		struct mapping *last_map = *maps;

		while (last_map->next != NULL)
		{
			last_map = last_map->next;
		}

		last_map->next = new_map;
	}
}*/

/*void add_GPIO_mapping(struct mapping **maps, unsigned char port, unsigned char Pin, bool Pullup, char Edge, unsigned char DeckNo, unsigned char Action, unsigned char Param)
{
	struct mapping *new_map = (struct mapping *)malloc(sizeof(struct mapping));
	new_map->Pin = Pin;
	new_map->port = port;
	new_map->Pullup = Pullup;
	new_map->Edge = Edge;
	new_map->DeckNo = DeckNo;
	new_map->Action = Action;
	new_map->Param = Param;
	new_map->next = NULL;
	new_map->Type = MAP_GPIO;
	printf("Adding Mapping - po:%d pn%x pl:%x ed%x - dn:%d, a:%d, p:%d\n", port, Pin, Pullup, Edge, DeckNo, Action, Param);
	if (*maps == NULL)
	{
		*maps = new_map;
	}
	else
	{
		struct mapping *last_map = *maps;

		while (last_map->next != NULL)
		{
			last_map = last_map->next;
		}

		last_map->next = new_map;
	}
}*/

/*
 * Process an IO event
 */
extern bool shifted;
extern int pitch_mode;

// Queued command from the realtime thread
// this is so dumb

struct mapping *queued_midi_command = NULL;
unsigned char queued_midi_buffer[3];

void perform_action_for_deck(struct deck* deck, struct mapping* map, unsigned char MidiBuffer[3], struct sc_settings* settings)
{
   //printf("Map notnull type:%d deck:%d po:%d edge:%d pin:%d action:%d param:%d\n", map->Type, map->DeckNo, map->port, map->Edge, map->Pin, map->Action, map->Param);
   //dump_maps();
   if (map->Action == ACTION_CUE)
   {
      unsigned int cuenum = 0;
      if (map->Type == MAP_MIDI)
         cuenum = map->MidiBytes[1];
      else
         cuenum = (map->port * 32) + map->Pin + 128;

      /*if (shifted)
         deck_unset_cue(&deck[map->DeckNo], cuenum);
      else*/
      deck_cue(deck, cuenum);
   }
   else if (map->Action == ACTION_DELETECUE)
   {
      unsigned int cuenum = 0;
      if (map->Type == MAP_MIDI)
         cuenum = map->MidiBytes[1];
      else
         cuenum = (map->port * 32) + map->Pin + 128;

      //if (shifted)
      deck_unset_cue(deck, cuenum);
      /*else
         deck_cue(&deck[map->DeckNo], cuenum);*/
   }
   else if (map->Action == ACTION_NOTE)
   {
      deck->player.note_pitch = pow(pow(2, (double)1 / 12), map->Param - 0x3C); // equal temperament
   }
   else if (map->Action == ACTION_STARTSTOP)
   {
      deck->player.stopped = !deck->player.stopped;
   }
   else if (map->Action == ACTION_SHIFTON)
   {
      printf("Shifton\n");
      shifted = 1;
   }
   else if (map->Action == ACTION_SHIFTOFF)
   {
      printf("Shiftoff\n");
      shifted = 0;
   }
   else if (map->Action == ACTION_NEXTFILE)
   {
      deck_next_file(deck, settings);
   }
   else if (map->Action == ACTION_PREVFILE)
   {
      deck_prev_file(deck, settings);
   }
   else if (map->Action == ACTION_RANDOMFILE)
   {
      deck_random_file(deck, settings);
   }
   else if (map->Action == ACTION_NEXTFOLDER)
   {
      deck_next_folder(deck, settings);
   }
   else if (map->Action == ACTION_PREVFOLDER)
   {
      deck_prev_folder(deck, settings);
   }
   else if (map->Action == ACTION_VOLUME)
   {
      deck->player.set_volume = (double)MidiBuffer[2] / 128.0;
   }
   else if (map->Action == ACTION_PITCH)
   {
      if (map->Type == MAP_MIDI)
      {
         double pitch = 0.0;
         // If this came from a pitch bend message, use 14 bit accuracy
         if ((MidiBuffer[0] & 0xF0) == 0xE0)
         {
            unsigned int pval = (((unsigned int)MidiBuffer[2]) << 7) | ((unsigned int)MidiBuffer[1]);
            pitch = (((double)pval - 8192.0) * ((double)settings->pitch_range / 819200.0)) + 1;
         }
            // Otherwise 7bit (boo)
         else
         {
            pitch = (((double)MidiBuffer[2] - 64.0) * ((double)settings->pitch_range / 6400.0) + 1);
         }

         deck->player.fader_pitch = pitch;
      }
   }
   else if (map->Action == ACTION_JOGPIT)
   {
      pitch_mode = map->DeckNo + 1;
      printf("Set Pitch Mode %d\n", pitch_mode);
   }
   else if (map->Action == ACTION_JOGPSTOP)
   {
      pitch_mode = 0;
   }
   else if (map->Action == ACTION_SC500)
   {
      printf("SC500 detected\n");
   }
   else if (map->Action == ACTION_VOLUP)
   {
      deck->player.set_volume += settings->volume_amount;
      if ( deck->player.set_volume > 1.0)
         deck->player.set_volume = 1.0;
   }
   else if (map->Action == ACTION_VOLDOWN)
   {
      deck->player.set_volume -= settings->volume_amount;
      if ( deck->player.set_volume < 0.0)
         deck->player.set_volume = 0.0;
   }
   else if (map->Action == ACTION_VOLUHOLD)
   {
      deck->player.set_volume += settings->volume_amount_held;
      if ( deck->player.set_volume > 1.0)
         deck->player.set_volume = 1.0;
   }
   else if (map->Action == ACTION_VOLDHOLD)
   {
      deck->player.set_volume -= settings->volume_amount_held;
      if ( deck->player.set_volume < 0.0)
         deck->player.set_volume = 0.0;
   }
   else if (map->Action == ACTION_JOGREVERSE)
   {
      printf("Reversed Jog Wheel - %d", settings->jog_reverse);
      settings->jog_reverse = !settings->jog_reverse;
      printf(",%d", settings->jog_reverse);
   }
   else if (map->Action == ACTION_BEND) // temporary bend of pitch that goes on top of the other pitch values
   {
      deck->player.bend_pitch = pow(pow(2, (double)1 / 12), map->Param - 0x3C);
   }
}

void io_event( struct mapping *map, unsigned char midi_buffer[3], struct sc1000* sc1000_engine, struct sc_settings* settings )
{
	if (map != NULL)
	{
      if (map->Action == ACTION_RECORD)
      {
         if (sc1000_engine->scratch_deck.files_present)
         {
            // TODO fix me
            deck_record(&sc1000_engine->beat_deck); // Always record on deck 0
         }
      }
      else
      {
         if ( map->DeckNo == 0 )
         {
            perform_action_for_deck(&sc1000_engine->beat_deck, map, midi_buffer, settings);
         }
         else
         {
            perform_action_for_deck(&sc1000_engine->scratch_deck, map, midi_buffer, settings);
         }
      }
	}
}

// Add a mapping from an action string and other params
void add_config_mapping(struct mapping **maps, unsigned char Type, unsigned char *buf, unsigned char port, unsigned char Pin, bool Pullup, char Edge, char *actions)
{
	unsigned char deckno, action, parameter = 0;

   printf("config mapping\n");

	// Extract deck no from action (CHx)
	if (actions[2] == '0')
		deckno = 0;
	if (actions[2] == '1')
		deckno = 1;

	// figure out which action it is
	if (strstr(actions + 4, "CUE") != NULL)
		action = ACTION_CUE;
	if (strstr(actions + 4, "DELETECUE") != NULL)
		action = ACTION_DELETECUE;
	else if (strstr(actions + 4, "SHIFTON") != NULL)
		action = ACTION_SHIFTON;
	else if (strstr(actions + 4, "SHIFTOFF") != NULL)
		action = ACTION_SHIFTOFF;
	else if (strstr(actions + 4, "STARTSTOP") != NULL)
		action = ACTION_STARTSTOP;
	else if (strstr(actions + 4, "GND") != NULL)
		action = ACTION_GND;
	else if (strstr(actions + 4, "NEXTFILE") != NULL)
		action = ACTION_NEXTFILE;
	else if (strstr(actions + 4, "PREVFILE") != NULL)
		action = ACTION_PREVFILE;
	else if (strstr(actions + 4, "RANDOMFILE") != NULL)
		action = ACTION_RANDOMFILE;
	else if (strstr(actions + 4, "NEXTFOLDER") != NULL)
		action = ACTION_NEXTFOLDER;
	else if (strstr(actions + 4, "PREVFOLDER") != NULL)
		action = ACTION_PREVFOLDER;
	else if (strstr(actions + 4, "PITCH") != NULL)
		action = ACTION_PITCH;
	else if (strstr(actions + 4, "JOGPIT") != NULL)
		action = ACTION_JOGPIT;
	else if (strstr(actions + 4, "JOGPSTOP") != NULL)
		action = ACTION_JOGPSTOP;
	else if (strstr(actions + 4, "RECORD") != NULL)
		action = ACTION_RECORD;
	else if (strstr(actions + 4, "VOLUME") != NULL)
		action = ACTION_VOLUME;
	else if (strstr(actions + 4, "VOLUP") != NULL)
		action = ACTION_VOLUP;
	else if (strstr(actions + 4, "VOLDOWN") != NULL)
		action = ACTION_VOLDOWN;
	else if (strstr(actions + 4, "VOLUHOLD") != NULL)
		action = ACTION_VOLUHOLD;
	else if (strstr(actions + 4, "VOLDHOLD") != NULL)
		action = ACTION_VOLDHOLD;
	else if (strstr(actions + 4, "JOGREVERSE") != NULL)
		action = ACTION_JOGREVERSE;
	else if (strstr(actions + 4, "SC500") != NULL)
		action = ACTION_SC500;
	else if (strstr(actions + 4, "NOTE") != NULL)
	{
		action = ACTION_NOTE;
		parameter = atoi(actions + 8);
	}
	add_mapping(maps, Type, deckno, buf, port, Pin, Pullup, Edge, action, parameter);
}

void add_mapping(struct mapping **maps, unsigned char Type, unsigned char deckno, unsigned char *buf, unsigned char port, unsigned char Pin, bool Pullup, char Edge, unsigned char action, unsigned char parameter)
{
	struct mapping *new_map = (struct mapping *)malloc(sizeof(struct mapping));
	new_map->Type = Type;
	new_map->Pin = Pin;
	new_map->port = port;
	new_map->Pullup = Pullup;
	if (buf == NULL)
	{
		new_map->MidiBytes[0] = 0x00;
		new_map->MidiBytes[1] = 0x00;
		new_map->MidiBytes[2] = 0x00;
	}
	else
	{
		new_map->MidiBytes[0] = buf[0];
		new_map->MidiBytes[1] = buf[1];
		new_map->MidiBytes[2] = buf[2];
	}

	new_map->Edge = Edge;
	new_map->Action = action;
	new_map->Param = parameter;
	new_map->next = NULL;

	new_map->DeckNo = deckno;
	new_map->debounce = 0;

	printf("Adding Mapping - ty:%d po:%d pn%x pl:%x ed%x mid:%x:%x:%x- dn:%d, a:%d, p:%d\n", new_map->Type, new_map->port, new_map->Pin, new_map->Pullup, new_map->Edge, new_map->MidiBytes[0], new_map->MidiBytes[1], new_map->MidiBytes[2], new_map->DeckNo, new_map->Action, new_map->Param);

	if (*maps == NULL)
	{
		*maps = new_map;
	}
	else
	{
		struct mapping *last_map = *maps;

		while (last_map->next != NULL)
		{
			last_map = last_map->next;
		}

		last_map->next = new_map;
	}
}

// Find a mapping from a MIDI event
struct mapping *find_midi_mapping( struct mapping *maps, unsigned char buf[3], char edge )
{

	struct mapping *last_map = maps;
	// Interpret zero-velocity notes as note-off commands
	if (((buf[0] & 0xF0) == 0x90) && (buf[2] == 0x00))
	{
		buf[0] = 0x80 | (buf[0] & 0x0F);
	}

	while (last_map != NULL)
	{

		if (
			last_map->Type == MAP_MIDI && last_map->Edge == edge &&
			((((last_map->MidiBytes[0] & 0xF0) == 0xE0) && last_map->MidiBytes[0] == buf[0]) || //Pitch bend messages only match on first byte
			 (last_map->MidiBytes[0] == buf[0] && last_map->MidiBytes[1] == buf[1]))			//Everything else matches on first two bytes
		)
		{
			return last_map;
		}

		last_map = last_map->next;
	}
	return NULL;
}

// Find a mapping from a GPIO event
struct mapping *find_io_mapping( struct mapping *mappings, unsigned char port, unsigned char pin, char edge )
{

	struct mapping *last_mapping = mappings;

	while ( last_mapping != NULL)
	{

		if ( last_mapping->Type == MAP_IO && last_mapping->Pin == pin && last_mapping->Edge == edge && last_mapping->port == port)
		{
			return last_mapping;
		}

      last_mapping = last_mapping->next;
	}
	return NULL;
}
