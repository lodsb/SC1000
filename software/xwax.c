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

#include <unistd.h>		   //Needed for I2C port
#include <fcntl.h>		   //Needed for I2C port
#include <sys/ioctl.h>	   //Needed for I2C port
#include <linux/i2c-dev.h> //Needed for I2C port
#include <time.h>
#include <dirent.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>

#include "audio/alsa.h"
#include "input/sc_input.h"
#include "midi/sc_midimap.h"
#include "player/controller.h"
#include "player/device.h"
#include "audio//dummy.h"
#include "player/dicer.h"
#include "player/track.h"
#include "thread/realtime.h"
#include "thread/thread.h"
#include "thread/rig.h"
#include "xwax.h"

//#define DEFAULT_IMPORTER EXECDIR "/xwax-import"

#define DEFAULT_IMPORTER "/root/xwax-import"

struct deck decks[2];

struct rt rt;

static const char *importer;

SC_SETTINGS scsettings;

struct mapping *maps = NULL;

unsigned int countChars(char *string, char c)
{
	unsigned int count = 0;
	
	//printf("Checking for commas in %s\n", string);

	do
	{
		if ((*string) == c)
		{
			count++;
		}
	} while ((*(string++)));
	return count;
}

void load_settings_file()
{
	FILE *fp;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	char *param, *actions;
	char *value;
	unsigned char channel = 0, notenum = 0, controlType = 0, pin = 0, pullup = 0, port = 0;
	char edge;
	char delim[] = "=";
	char delimc[] = ",";
	unsigned char midicommand[3];
	char *linetok, *valuetok;
	// set defaults
	scsettings.buffer_size = 256;
	scsettings.fader_close_point = 2;
	scsettings.fader_open_point = 10;
	scsettings.platter_enabled = 1;
	scsettings.platter_speed = 2275;
	scsettings.sample_rate = 48000;
	scsettings.update_rate = 2000;
	scsettings.debounce_time = 5;
	scsettings.hold_time = 100;
	scsettings.slippiness = 200;
	scsettings.brake_speed = 3000;
	scsettings.pitch_range = 50;
	scsettings.midi_delay = 5;
	scsettings.volume_amount = 0.03;
	scsettings.volume_amount_held = 0.001;
	scsettings.initial_volume = 0.125;
	scsettings.midi_remapped = 0;
	scsettings.io_remapped = 0;
	scsettings.jog_reverse = 0;
	scsettings.cut_beats = 0;

	// later we'll check for sc500 pin and use it to set following settings
	scsettings.disable_volume_adc = 0;
	scsettings.disable_pic_buttons = 0;

	// Load any settings from config file
	fp = fopen("/media/sda/scsettings.txt", "r");
	if (fp == NULL)
	{
		// load internal copy instead
		fp = fopen("/var/scsettings.txt", "r");
	}


	if (fp != NULL)
	{
		while ((read = getline(&line, &len, fp)) != -1)
		{
			if (strlen(line) < 2 || line[0] == '#')
			{ // Comment or blank line
			}
			else
			{
				param = strtok_r(line, delim, &linetok);
				value = strtok_r(NULL, delim, &linetok);

				if (strcmp(param, "buffer_size") == 0)
					scsettings.buffer_size = atoi(value);
				else if (strcmp(param, "fader_close_point") == 0)
					scsettings.fader_close_point = atoi(value);
				else if (strcmp(param, "fader_open_point") == 0)
					scsettings.fader_open_point = atoi(value);
				else if (strcmp(param, "platter_enabled") == 0)
					scsettings.platter_enabled = atoi(value);
				else if (strcmp(param, "disable_volume_adc") == 0)
					scsettings.disable_volume_adc = atoi(value);
				else if (strcmp(param, "platter_speed") == 0)
					scsettings.platter_speed = atoi(value);
				else if (strcmp(param, "sample_rate") == 0)
					scsettings.sample_rate = atoi(value);
				else if (strcmp(param, "update_rate") == 0)
					scsettings.update_rate = atoi(value);
				else if (strcmp(param, "debounce_time") == 0)
					scsettings.debounce_time = atoi(value);
				else if (strcmp(param, "hold_time") == 0)
					scsettings.hold_time = atoi(value);
				else if (strcmp(param, "slippiness") == 0)
					scsettings.slippiness = atoi(value);
				else if (strcmp(param, "brake_speed") == 0)
					scsettings.brake_speed = atoi(value);
				else if (strcmp(param, "pitch_range") == 0)
					scsettings.pitch_range = atoi(value);
				else if (strcmp(param, "jog_reverse") == 0)
					scsettings.jog_reverse = atoi(value);
				else if (strcmp(param, "cut_beats") == 0)
					scsettings.cut_beats = atoi(value);
				else if (strstr(param, "midii") != NULL)
				{
					scsettings.midi_remapped = 1;
					controlType = atoi(strtok_r(value, delimc, &valuetok));
					channel = atoi(strtok_r(NULL, delimc, &valuetok));
					notenum = atoi(strtok_r(NULL, delimc, &valuetok));
					edge = atoi(strtok_r(NULL, delimc, &valuetok));
					actions = strtok_r(NULL, delimc, &valuetok);

					//255 means bind to all notes
					if (notenum == 255){

						// Build MIDI command
						midicommand[0] = (controlType << 4) | channel;
						midicommand[1] = notenum;
						midicommand[2] = 0;

						char tempact[20];
						for (midicommand[1] = 0; midicommand[1] < 128; midicommand[1]++){
							sprintf(tempact, "%s%u", actions, midicommand[1]);
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
					else {
						// Build MIDI command
						midicommand[0] = (controlType << 4) | channel;
						midicommand[1] = notenum;
						midicommand[2] = 0;
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
				else if (strstr(param, "io") != NULL)
				{
					scsettings.io_remapped = 1;
					unsigned int commaCount = countChars(value, ',');
					//printf("Found io %s - comacount %d\n", value, commaCount);
					port = 0;
					if (commaCount == 4){
						port = atoi(strtok_r(value, delimc, &valuetok));
						pin = atoi(strtok_r(NULL, delimc, &valuetok));
					}
					else {
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
				else if (strcmp(param, "midi_delay") == 0) // Literally just a sleep to allow USB devices longer to initialize
					scsettings.midi_delay = atoi(value);
				else
				{
					printf("Unrecognised configuration line - Param : %s , value : %s\n", param, value);
				}
			}
		}
	}

	

	printf("bs %d, fcp %d, fop %d, pe %d, ps %d, sr %d, ur %d\n",
		   scsettings.buffer_size,
		   scsettings.fader_close_point,
		   scsettings.fader_open_point,
		   scsettings.platter_enabled,
		   scsettings.platter_speed,
		   scsettings.sample_rate,
		   scsettings.update_rate);

	if (fp)
		fclose(fp);
	if (line)
		free(line);
}

void sig_handler(int signo)
{
	if (signo == SIGINT)
	{
		printf("received SIGINT\n");
		exit(0);
	}
}

int main(int argc, char *argv[])
{

	int rc = -1, priority;
	bool use_mlock;

	if (signal(SIGINT, sig_handler) == SIG_ERR)
	{
		printf("\ncan't catch SIGINT\n");
		exit(1);
	}

	if (setlocale(LC_ALL, "") == NULL)
	{
		fprintf(stderr, "Could not honour the local encoding\n");
		return -1;
	}
	if (thread_global_init() == -1)
		return -1;
	if (rig_init() == -1)
		return -1;
	rt_init(&rt);

	importer = DEFAULT_IMPORTER;
	use_mlock = false;

   load_settings_file();

	// Create two decks, both pointed at the same audio device

	alsa_init(decks, scsettings.buffer_size);

   deck_init(&decks[0], &rt, importer, 0);
   deck_init(&decks[1], &rt, importer, 1);

   // point deck1's output at deck0, it will be summed in

	decks[0].device.beat_player = decks[1].device.scratch_player;

	// Tell deck0 to just play without considering inputs

	decks[0].player.justPlay = 1;

	alsa_clear_config_cache();

	rc = EXIT_FAILURE; /* until clean exit */

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

	deck_load_folder(&decks[0], "/media/sda/beats/");
   printf("load folder 1 ok\n");
	deck_load_folder(&decks[1], "/media/sda/samples/");
   printf("load folder 2 ok\n");

	if (!decks[1].filesPresent)
	{
		// Load the default sentence if no sample files found on usb stick
		player_set_track(&decks[1].player, track_acquire_by_import(decks[1].importer, "/var/scratchsentence.mp3"));
      printf("set track ok");
		cues_load_from_file(&decks[1].cues, decks[1].player.track->path);
      printf("set cues ok");
		// Set the time back a bit so the sample doesn't start too soon
		decks[1].player.target_position = -4.0;
      decks[1].player.position = -4.0;
	}

	// Start input processing thread

	SC_Input_Start();

	// Start realtime stuff

	priority = 0;

	if (rt_start(&rt, priority) == -1)
		return -1;

	if (use_mlock && mlockall(MCL_CURRENT) == -1)
	{
		perror("mlockall");
		goto out_rt;
	}

	// Main loop
	
	fprintf(stderr, "WIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIP\n\n");

   return 0;

   if (rig_main() == -1)
		goto out_interface;

	// Exit

	rc = EXIT_SUCCESS;
	fprintf(stderr, "Exiting cleanly...\n");

out_interface:
out_rt:
	rt_stop(&rt);

	deck_clear(&decks[0]);
	deck_clear(&decks[1]);

	rig_clear();
	thread_global_clear();

	if (rc == EXIT_SUCCESS)
		fprintf(stderr, "Done.\n");

	return rc;
}
