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

void perform_action_for_deck( struct deck* deck, struct mapping* map, const unsigned char midi_buffer[3], struct sc_settings* settings )
{
   //printf("Map notnull type:%d deck:%d po:%d edge:%d pin:%d action:%d param:%d\n", map->Type, map->DeckNo, map->port, map->Edge, map->Pin, map->Action, map->Param);
   //dump_maps();
   if ( map->action_type == CUE)
   {
      unsigned int cuenum = 0;
      if ( map->type == MIDI)
         cuenum = map->midi_command_bytes[1];
      else
         cuenum = (map->gpio_port * 32) + map->pin + 128;

      /*if (shifted)
         deck_unset_cue(&deck[map->DeckNo], cuenum);
      else*/
      deck_cue(deck, cuenum);
   }
   else if ( map->action_type == DELETECUE)
   {
      unsigned int cuenum = 0;
      if ( map->type == MIDI)
         cuenum = map->midi_command_bytes[1];
      else
         cuenum = (map->gpio_port * 32) + map->pin + 128;

      //if (shifted)
      deck_unset_cue(deck, cuenum);
      /*else
         deck_cue(&deck[map->DeckNo], cuenum);*/
   }
   else if ( map->action_type == NOTE)
   {
      deck->player.note_pitch = pow(pow(2, (double)1 / 12), map->parameter - 0x3C); // equal temperament
   }
   else if ( map->action_type == STARTSTOP)
   {
      deck->player.stopped = !deck->player.stopped;
   }
   else if ( map->action_type == SHIFTON)
   {
      printf("Shift on\n");
      shifted = 1;
   }
   else if ( map->action_type == SHIFTOFF)
   {
      printf("Shift off\n");
      shifted = 0;
   }
   else if ( map->action_type == NEXTFILE)
   {
      deck_next_file(deck, settings);
   }
   else if ( map->action_type == PREVFILE)
   {
      deck_prev_file(deck, settings);
   }
   else if ( map->action_type == RANDOMFILE)
   {
      deck_random_file(deck, settings);
   }
   else if ( map->action_type == NEXTFOLDER)
   {
      deck_next_folder(deck, settings);
   }
   else if ( map->action_type == PREVFOLDER)
   {
      deck_prev_folder(deck, settings);
   }
   else if ( map->action_type == VOLUME)
   {
      deck->player.set_volume = (double)midi_buffer[2] / 128.0;
   }
   else if ( map->action_type == PITCH)
   {
      if ( map->type == MIDI)
      {
         double pitch = 0.0;
         // If this came from a pitch bend message, use 14 bit accuracy
         if ( (midi_buffer[0] & 0xF0) == 0xE0)
         {
            unsigned int pval = (((unsigned int)midi_buffer[2]) << 7) | ((unsigned int)midi_buffer[1]);
            pitch = (((double)pval - 8192.0) * ((double)settings->pitch_range / 819200.0)) + 1;
         }
            // Otherwise 7bit (boo)
         else
         {
            pitch = (((double)midi_buffer[2] - 64.0) * ((double)settings->pitch_range / 6400.0) + 1);
         }

         deck->player.fader_pitch = pitch;
      }
   }
   else if ( map->action_type == JOGPIT)
   {
      pitch_mode = map->deck_no + 1;
      printf("Set Pitch Mode %d\n", pitch_mode);
   }
   else if ( map->action_type == JOGPSTOP)
   {
      pitch_mode = 0;
   }
   else if ( map->action_type == SC500)
   {
      printf("SC500 detected\n");
   }
   else if ( map->action_type == VOLUP)
   {
      deck->player.set_volume += settings->volume_amount;
      if ( deck->player.set_volume > 1.0)
         deck->player.set_volume = 1.0;
   }
   else if ( map->action_type == VOLDOWN)
   {
      deck->player.set_volume -= settings->volume_amount;
      if ( deck->player.set_volume < 0.0)
         deck->player.set_volume = 0.0;
   }
   else if ( map->action_type == VOLUHOLD)
   {
      deck->player.set_volume += settings->volume_amount_held;
      if ( deck->player.set_volume > 1.0)
         deck->player.set_volume = 1.0;
   }
   else if ( map->action_type == VOLDHOLD)
   {
      deck->player.set_volume -= settings->volume_amount_held;
      if ( deck->player.set_volume < 0.0)
         deck->player.set_volume = 0.0;
   }
   else if ( map->action_type == JOGREVERSE)
   {
      printf("Reversed Jog Wheel - %d", settings->jog_reverse);
      settings->jog_reverse = !settings->jog_reverse;
      printf(",%d", settings->jog_reverse);
   }
   else if ( map->action_type == BEND) // temporary bend of pitch that goes on top of the other pitch values
   {
      deck->player.bend_pitch = pow(pow(2, (double)1 / 12), map->parameter - 0x3C);
   }
}

void io_event( struct mapping *map, unsigned char midi_buffer[3], struct sc1000* sc1000_engine, struct sc_settings* settings )
{
	if (map != NULL)
	{
      if ( map->action_type == RECORD)
      {
         if (sc1000_engine->scratch_deck.files_present)
         {
            // TODO fix me
            deck_record(&sc1000_engine->beat_deck); // Always record on deck 0
         }
      }
      else
      {
         if ( map->deck_no == 0 )
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

// Find a mapping from a MIDI event
struct mapping *find_midi_mapping( struct mapping *maps, unsigned char buf[3], enum EventType edge )
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
              last_map->type == MIDI && last_map->edge_type == edge &&
              ((((last_map->midi_command_bytes[0] & 0xF0) == 0xE0) && last_map->midi_command_bytes[0] == buf[0]) || //Pitch bend messages only match on first byte
			 (last_map->midi_command_bytes[0] == buf[0] && last_map->midi_command_bytes[1] == buf[1]))			//Everything else matches on first two bytes
		)
		{
			return last_map;
		}

		last_map = last_map->next;
	}
	return NULL;
}

// Find a mapping from a GPIO event
struct mapping *find_io_mapping( struct mapping *mappings, unsigned char port, unsigned char pin, enum EventType edge )
{

	struct mapping *last_mapping = mappings;

	while ( last_mapping != NULL)
	{

		if ( last_mapping->type == IO && last_mapping->pin == pin && last_mapping->edge_type == edge && last_mapping->gpio_port == port)
		{
			return last_mapping;
		}

      last_mapping = last_mapping->next;
	}
	return NULL;
}
