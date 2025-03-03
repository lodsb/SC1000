// SC1000 input handler
// Thread that grabs data from the rotary sensor and PIC input processor and processes it

#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>		   //Needed for I2C port
#include <sys/ioctl.h>	   //Needed for I2C port
#include <linux/i2c-dev.h> //Needed for I2C port
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/time.h>

#include "../player/sc_playlist.h"
#include "../audio/alsa.h"
#include "../player/controller.h"
#include "../player/device.h"
#include "../audio/dummy.h"
#include "../thread/realtime.h"
#include "../thread/thread.h"
#include "../thread/rig.h"
#include "../player/track.h"

#include "sc_midimap.h"
#include "midi.h"

#include "../player/dicer.h"

#include "../global/global.h"

#include "sc_input.h"

bool shifted = 0;
bool shiftLatched = 0;

struct controller midiControllers[32];
int numControllers = 0;

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)       \
	(byte & 0x80 ? '1' : '0'),     \
		(byte & 0x40 ? '1' : '0'), \
		(byte & 0x20 ? '1' : '0'), \
		(byte & 0x10 ? '1' : '0'), \
		(byte & 0x08 ? '1' : '0'), \
		(byte & 0x04 ? '1' : '0'), \
		(byte & 0x02 ? '1' : '0'), \
		(byte & 0x01 ? '1' : '0')

extern struct mapping *maps;

void i2c_read_address(int file_i2c, unsigned char address, unsigned char *result)
{

	*result = address;
	if (write(file_i2c, result, 1) != 1)
	{
		printf("I2C read error\n");
		exit(1);
	}

	if (read(file_i2c, result, 1) != 1)
	{
		printf("I2C read error\n");
		exit(1);
	}
}

int i2c_write_address(int file_i2c, unsigned char address, unsigned char value)
{
	char buf[2];
	buf[0] = address;
	buf[1] = value;
	if (write(file_i2c, buf, 2) != 2)
	{
		printf("I2C Write Error\n");
		return 0;
	}
	else
		return 1;
}

void dump_maps()
{
	struct mapping *new_map = maps;
	while (new_map != NULL)
	{
		printf("Dump Mapping - ty:%d po:%d pn%x pl:%x ed%x mid:%x:%x:%x- dn:%d, a:%d, p:%d\n", new_map->Type, new_map->port, new_map->Pin, new_map->Pullup, new_map->Edge, new_map->MidiBytes[0], new_map->MidiBytes[1], new_map->MidiBytes[2], new_map->DeckNo, new_map->Action, new_map->Param);
		new_map = new_map->next;
	}
}

int setup_i2c( char *path, unsigned char address )
{

	int file = 0;

	if ((file = open(path, O_RDWR)) < 0)
	{
		printf("%s - Failed to open\n", path);
		return -1;
	}
	else if (ioctl(file, I2C_SLAVE, address) < 0)
	{
		printf("%s - Failed to acquire bus access and/or talk to slave.\n", path);
		return -1;
	}
	else
		return file;
}

void add_new_midi_devices(struct sc1000* sc1000_engine, char mididevices[64][64], int mididevicenum )
{
	bool alreadyAdded;
	// Search to see which devices we've already added
	for (int devc = 0; devc < mididevicenum; devc++)
	{

		alreadyAdded = 0;

		for (int controlc = 0; controlc < numControllers; controlc++)
		{
			char *controlName = ((struct dicer *)(midiControllers[controlc].local))->PortName;
			if (strcmp(mididevices[devc], controlName) == 0)
				alreadyAdded = 1;
		}

		if (!alreadyAdded)
		{
			if (dicer_init(&midiControllers[numControllers], &g_rt, mididevices[devc]) != -1)
			{
				printf("Adding MIDI device %d - %s\n", numControllers, mididevices[devc]);
				controller_add_deck(&midiControllers[numControllers], &sc1000_engine->beat_deck);
				controller_add_deck(&midiControllers[numControllers], &sc1000_engine->scratch_deck);
				numControllers++;
			}
		}
	}
}
unsigned char gpiopresent = 1;
unsigned char mmappresent = 1;
int file_i2c_gpio;
volatile void *gpio_addr;

bool firstTimeRound = 1;
void init_io()
{
	int i, j;
	struct mapping *map;

	// Initialise external MCP23017 GPIO on I2C1
	if ( (file_i2c_gpio = setup_i2c("/dev/i2c-1", 0x20)) < 0)
	{
		printf("Couldn't init external GPIO\n");
		gpiopresent = 0;
	}
	else
	{
		// Do a test write to make sure we got in
		if (!i2c_write_address(file_i2c_gpio, 0x0C, 0xFF))
		{
			gpiopresent = 0;
			printf("Couldn't init external GPIO\n");
		}
	}

	// Configure external IO
	if (gpiopresent)
	{

		// default to pulled up and input
		unsigned int pullups = 0xFFFF;
		unsigned int iodirs = 0xFFFF;

		// For each pin
		for (i = 0; i < 16; i++)
		{
			map = find_io_mapping(maps, 0, i, 1);
			// If pin is marked as ground
			if (map != NULL && map->Action == ACTION_GND)
			{
				//printf("Grounding pin %d\n", i);
				iodirs &= ~(0x0001 << i);
			}

			// If pin's pullup is disabled
			if (map != NULL && !map->Pullup)
			{
				//printf("Disabling pin %d pullup\n", i);
				pullups &= ~(0x0001 << i);
			}
			else printf ("Pulling up pin %d\n", i);
		}

		unsigned char tmpchar;

		// Bank A pullups
		tmpchar = (unsigned char)(pullups & 0xFF);
		i2c_write_address(file_i2c_gpio, 0x0C, tmpchar);

		// Bank B pullups
		tmpchar = (unsigned char)((pullups >> 8) & 0xFF);
		i2c_write_address(file_i2c_gpio, 0x0D, tmpchar);

		/*printf("PULLUPS - B");
		printf(BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY((pullups >> 8) & 0xFF));
		printf("A");
		printf(BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY((pullups & 0xFF)));
		printf("\n");*/

		// Bank A direction
		tmpchar = (unsigned char)(iodirs & 0xFF);
		i2c_write_address(file_i2c_gpio, 0x00, tmpchar);

		// Bank B direction
		tmpchar = (unsigned char)((iodirs >> 8) & 0xFF);
		i2c_write_address(file_i2c_gpio, 0x01, tmpchar);

		/*printf("IODIRS  - B");
		printf(BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY((iodirs >> 8) & 0xFF));
		printf("A");
		printf(BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY(iodirs & 0xFF));
		printf("\n");*/
	}

	// Configure A13 GPIO

	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0)
	{
		fprintf(stderr, "Unable to open port\n\r");
		//exit(fd);
		mmappresent = 0;
	}
	gpio_addr = mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x01C20800 & 0xffff0000);
	if (gpio_addr == MAP_FAILED)
	{
		fprintf(stderr, "Unable to open mmap\n\r");
		//exit(fd);
		mmappresent = 0;
	}
	gpio_addr += 0x0800;

	if (mmappresent)
	{
		// For each port
		for (j = 1; j <= 6; j++)
		{
			// For each pin (max number of pins on each port is 28)
			for (i = 0; i < 28; i++)
			{

				map = find_io_mapping(maps, j, i, 1);

				if (map != NULL)
				{
					// dirty hack, don't map J7 SCL/SDA pins if MCP is present
					if (gpiopresent && j == 1 && (i == 15 || i == 16)){
						map->Action = ACTION_NOTHING;
					}
					else {
						//printf("Pulling %d %d %d\n", j, i, map->Pullup);
						// which config register to use, 0-3
						uint32_t configregister = i >> 3;

						// which pull register to use, 0-1
						uint32_t pullregister = i >> 4;

						// how many bits to shift the config register
						uint32_t configShift = (i % 8) * 4;

						// how many bits to shift the pull register
						uint32_t pullShift = (i % 16) * 2;

						volatile uint32_t *PortConfigRegister = gpio_addr + (j * 0x24) + (configregister * 0x04);
						volatile uint32_t *PortPullRegister = gpio_addr + (j * 0x24) + 0x1C + (pullregister * 0x04);
						uint32_t portConfig = *PortConfigRegister;
						uint32_t portPull = *PortPullRegister;

						// mask to unset the relevant pins in the registers
						uint32_t configMask = ~(0b1111 << configShift);
						uint32_t pullMask = ~(0b11 << pullShift);

						// Set port as input
						// portConfig = (portConfig & configMask) | (0b0000 << configShift); (not needed because input is 0 anyway)
						portConfig = (portConfig & configMask);

						portPull = (portPull & pullMask) | (map->Pullup << pullShift);
						*PortConfigRegister = portConfig;
						*PortPullRegister = portPull;
					}
				}
			}
		}
	}

}

void process_io(struct sc1000* sc1000_engine, struct sc_settings* settings)
{ // Iterate through all digital input mappings and check the appropriate pin
	unsigned int gpios = 0x00000000;
	unsigned char result;
	if (gpiopresent)
	{
		i2c_read_address(file_i2c_gpio, 0x13, &result); // Read bank B
		gpios = ((unsigned int)result) << 8;
		//printf(BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY(result));
		i2c_read_address(file_i2c_gpio, 0x12, &result); // Read bank A
		gpios |= result;
		//printf(" - ");
		//printf(BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY(result));
		//printf("\n");

		// invert logic
		gpios ^= 0xFFFF;
	}
	struct mapping *last_map = maps;
	while (last_map != NULL)
	{
		//printf("arses : %d %d\n", last_map->port, last_map->Pin);

		// Only digital pins
		if (last_map->Type == MAP_IO && (!(last_map->port == 0 && !gpiopresent)))
		{

			bool pinVal = 0;
			if (last_map->port == 0 && gpiopresent) // port 0, I2C GPIO expander
			{
				pinVal = (bool)((gpios >> last_map->Pin) & 0x01);
			}
			else if (mmappresent) // Ports 1-6, olimex GPIO
			{
				volatile uint32_t *PortDataReg = gpio_addr + (last_map->port * 0x24) + 0x10;
				uint32_t PortData = *PortDataReg;
				PortData ^= 0xffffffff;
				pinVal = (bool)((PortData >> last_map->Pin) & 0x01);
			}
			else
			{
				pinVal = 0;
			}

			// iodebounce = 0 when button not pressed,
			// > 0 and < scsettings.debounce_time when debouncing positive edge
			// > scsettings.debounce_time and < scsettings.hold_time when holding
			// = scsettings.hold_time when continuing to hold
			// > scsettings.hold_time when waiting for release
			// > -scsettings.debounce_time and < 0 when debouncing negative edge

			// Button not pressed, check for button
			if (last_map->debounce == 0)
			{
				if (pinVal)
				{
					printf("Button %d pressed\n", last_map->Pin);
					if (firstTimeRound && last_map->DeckNo == 1 && (last_map->Action == ACTION_VOLUP || last_map->Action == ACTION_VOLDOWN))
					{
						player_set_track(&sc1000_engine->beat_deck.player, track_acquire_by_import(sc1000_engine->beat_deck.importer, "/var/os-version.mp3"));
						cues_load_from_file(&sc1000_engine->beat_deck.cues, sc1000_engine->beat_deck.player.track->path);
                  sc1000_engine->scratch_deck.player.setVolume = 0.0;
					}
					else
					{
						if ((!shifted && last_map->Edge == 1) || (shifted && last_map->Edge == 3))
                     io_event(last_map, NULL, sc1000_engine, settings);

						// start the counter
						last_map->debounce++;
					}
				}
			}

			// Debouncing positive edge, increment value
			else if (last_map->debounce > 0 && last_map->debounce < settings->debounce_time)
			{
				last_map->debounce++;
			}

			// debounce finished, keep incrementing until hold reached
			else if ( last_map->debounce >= settings->debounce_time && last_map->debounce < settings->hold_time)
			{
				// check to see if unpressed
				if (!pinVal)
				{
					printf("Button %d released\n", last_map->Pin);
					if (last_map->Edge == 0)
                  io_event(last_map, NULL, sc1000_engine, settings);
					// start the counter
					last_map->debounce = -settings->debounce_time;
				}

				else
					last_map->debounce++;
			}
			// Button has been held for a while
			else if ( last_map->debounce == settings->hold_time)
			{
				printf("Button %d-%d held\n", last_map->port, last_map->Pin);
				if ((!shifted && last_map->Edge == 2) || (shifted && last_map->Edge == 4))
               io_event(last_map, NULL, sc1000_engine, settings);
				last_map->debounce++;
			}

			// Button still holding, check for release
			else if ( last_map->debounce > settings->hold_time)
			{
				if (pinVal)
				{
					if (last_map->Action == ACTION_VOLUHOLD || last_map->Action == ACTION_VOLDHOLD)
					{
						// keep running the vol up/down actions if they're held
						if ((!shifted && last_map->Edge == 2) || (shifted && last_map->Edge == 4))
                     io_event(last_map, NULL, sc1000_engine, settings);
					}
				}
				// check to see if unpressed
				else
				{
					printf("Button %d released\n", last_map->Pin);
					if (last_map->Edge == 0)
                  io_event(last_map, NULL, sc1000_engine, settings);
					// start the counter
					last_map->debounce = -settings->debounce_time;
				}
			}

			// Debouncing negative edge, increment value - will reset when zero is reached
			else if (last_map->debounce < 0)
			{
				last_map->debounce++;
			}
		}

		last_map = last_map->next;
	}

	// Dumb hack to process MIDI commands in this thread rather than the realtime one
	if ( queued_midi_command != NULL)
	{
      io_event(queued_midi_command, queued_midi_buffer, sc1000_engine, settings);
      queued_midi_command = NULL;
	}
}

int file_i2c_rot, file_i2c_pic;

int pitchMode = 0; // If we're in pitch-change mode
int oldPitchMode = 0;
bool capIsTouched = 0;
unsigned char buttons[4] = {0, 0, 0, 0}, totalbuttons[4] = {0, 0, 0, 0};
unsigned int ADCs[4] = {0, 0, 0, 0};
unsigned char buttonState = 0;
unsigned int butCounter = 0;
unsigned char faderOpen1 = 0, faderOpen2 = 0;
void process_pic(struct sc1000* sc1000_engine, struct sc_settings* settings)
{
	unsigned int i;

	unsigned char result;

	unsigned int faderCutPoint1, faderCutPoint2;
	
	double fadertarget0, fadertarget1;

	i2c_read_address(file_i2c_pic, 0x00, &result);
	ADCs[0] = result;
	i2c_read_address(file_i2c_pic, 0x01, &result);
	ADCs[1] = result;
	i2c_read_address(file_i2c_pic, 0x02, &result);
	ADCs[2] = result;
	i2c_read_address(file_i2c_pic, 0x03, &result);
	ADCs[3] = result;
	i2c_read_address(file_i2c_pic, 0x04, &result);
	ADCs[0] |= ((unsigned int)(result & 0x03) << 8);
	ADCs[1] |= ((unsigned int)(result & 0x0C) << 6);
	ADCs[2] |= ((unsigned int)(result & 0x30) << 4);
	ADCs[3] |= ((unsigned int)(result & 0xC0) << 2);
	// Now buttons and capsense

	i2c_read_address(file_i2c_pic, 0x05, &result);
	buttons[0] = !(result & 0x01);
	buttons[1] = !(result >> 1 & 0x01);
	buttons[2] = !(result >> 2 & 0x01);
	buttons[3] = !(result >> 3 & 0x01);
	capIsTouched = (result >> 4 & 0x01);

	process_io(sc1000_engine, settings);

	// Apply volume and fader

	if (!settings->disable_volume_adc)
	{
      sc1000_engine->beat_deck.player.setVolume = ((double)ADCs[2]) / 1024;
      sc1000_engine->scratch_deck.player.setVolume = ((double)ADCs[3]) / 1024;
	}
	
	// Fader Hysteresis
	faderCutPoint1 = faderOpen1 ? settings->fader_close_point : settings->fader_open_point;
	faderCutPoint2 = faderOpen2 ? settings->fader_close_point : settings->fader_open_point;
	
	faderOpen1 = 1; faderOpen2 = 1;
	
	fadertarget0 = sc1000_engine->beat_deck.player.setVolume;
	fadertarget1 = sc1000_engine->scratch_deck.player.setVolume;
	

	if (ADCs[0] < faderCutPoint1)
	{ 
		if ( settings->cut_beats == 1) fadertarget0 = 0.0;
		else fadertarget1 = 0.0;
		faderOpen1 = 0;
	}
	if (ADCs[1] < faderCutPoint2)
	{
		if ( settings->cut_beats == 2) fadertarget0 = 0.0;
		else fadertarget1 = 0.0;
		faderOpen2 = 0;
	}

   sc1000_engine->beat_deck.player.faderTarget = fadertarget0;
   sc1000_engine->scratch_deck.player.faderTarget = fadertarget1;

	if (!settings->disable_pic_buttons)
	{
		/*
		 Button scanning logic goes like -

		 1. Wait for ANY button to be pressed
		 2. Note which buttons are pressed
		 3. If we're still holding down buttons after an amount of time, act on held buttons, goto 5
		 4. If ALL buttons are unpressed act on them instantaneously, goto 5
		 5. wait half a second or so, then goto 1;

		 */

#define BUTTONSTATE_NONE 0
#define BUTTONSTATE_PRESSING 1
#define BUTTONSTATE_ACTING_INSTANT 2
#define BUTTONSTATE_ACTING_HELD 3
#define BUTTONSTATE_WAITING 4
		switch (buttonState)
		{

		// No buttons pressed
		case BUTTONSTATE_NONE:
			if (buttons[0] || buttons[1] || buttons[2] || buttons[3])
			{
				buttonState = BUTTONSTATE_PRESSING;

				if (firstTimeRound)
				{
					player_set_track(&sc1000_engine->beat_deck.player, track_acquire_by_import(sc1000_engine->beat_deck.importer, "/var/os-version.mp3"));
					cues_load_from_file(&sc1000_engine->beat_deck.cues, sc1000_engine->beat_deck.player.track->path);
					buttonState = BUTTONSTATE_WAITING;
				}
			}

			break;

		// At least one button pressed
		case BUTTONSTATE_PRESSING:
			for (i = 0; i < 4; i++)
				totalbuttons[i] |= buttons[i];

			if (!(buttons[0] || buttons[1] || buttons[2] || buttons[3]))
				buttonState = BUTTONSTATE_ACTING_INSTANT;

			butCounter++;
			if ( butCounter > settings->hold_time)
			{
				butCounter = 0;
				buttonState = BUTTONSTATE_ACTING_HELD;
			}

			break;

		// Act on instantaneous (i.e. not held) button press
		case BUTTONSTATE_ACTING_INSTANT:

			// Any button to stop pitch mode
			if (pitchMode)
			{
				pitchMode = 0;
				oldPitchMode = 0;
				printf("Pitch mode Disabled\n");
			}
			else if ( totalbuttons[0] && !totalbuttons[1] && !totalbuttons[2] && !totalbuttons[3] && sc1000_engine->scratch_deck.files_present)
				deck_prev_file(&sc1000_engine->scratch_deck, settings);
			else if ( !totalbuttons[0] && totalbuttons[1] && !totalbuttons[2] && !totalbuttons[3] && sc1000_engine->scratch_deck.files_present)
				deck_next_file(&sc1000_engine->scratch_deck, settings);
			else if ( totalbuttons[0] && totalbuttons[1] && !totalbuttons[2] && !totalbuttons[3] && sc1000_engine->scratch_deck.files_present)
				pitchMode = 2;
			else if ( !totalbuttons[0] && !totalbuttons[1] && totalbuttons[2] && !totalbuttons[3] && sc1000_engine->beat_deck.files_present)
				deck_prev_file(&sc1000_engine->beat_deck, settings);
			else if ( !totalbuttons[0] && !totalbuttons[1] && !totalbuttons[2] && totalbuttons[3] && sc1000_engine->beat_deck.files_present)
				deck_next_file(&sc1000_engine->beat_deck, settings);
			else if ( !totalbuttons[0] && !totalbuttons[1] && totalbuttons[2] && totalbuttons[3] && sc1000_engine->beat_deck.files_present)
				pitchMode = 1;
			else if (totalbuttons[0] && totalbuttons[1] && totalbuttons[2] && totalbuttons[3])
				shiftLatched = 1;
			else
				printf("Sod knows what you were trying to do there\n");

			buttonState = BUTTONSTATE_WAITING;

			break;

		// Act on whatever buttons are being held down when the timeout happens
		case BUTTONSTATE_ACTING_HELD:
			if ( buttons[0] && !buttons[1] && !buttons[2] && !buttons[3] && sc1000_engine->scratch_deck.files_present)
				deck_prev_folder(&sc1000_engine->scratch_deck, settings);
			else if ( !buttons[0] && buttons[1] && !buttons[2] && !buttons[3] && sc1000_engine->scratch_deck.files_present)
				deck_next_folder(&sc1000_engine->scratch_deck, settings);
			else if ( buttons[0] && buttons[1] && !buttons[2] && !buttons[3] && sc1000_engine->scratch_deck.files_present)
				deck_random_file(&sc1000_engine->scratch_deck, settings);
			else if ( !buttons[0] && !buttons[1] && buttons[2] && !buttons[3] && sc1000_engine->beat_deck.files_present)
				deck_prev_folder(&sc1000_engine->beat_deck, settings);
			else if ( !buttons[0] && !buttons[1] && !buttons[2] && buttons[3] && sc1000_engine->beat_deck.files_present)
				deck_next_folder(&sc1000_engine->beat_deck, settings);
			else if ( !buttons[0] && !buttons[1] && buttons[2] && buttons[3] && sc1000_engine->beat_deck.files_present)
				deck_random_file(&sc1000_engine->beat_deck, settings);
			else if (buttons[0] && buttons[1] && buttons[2] && buttons[3])
			{
				printf("All buttons held!\n");
				if (sc1000_engine->scratch_deck.files_present)
					deck_record(&sc1000_engine->beat_deck);
			}
			else
				printf("Sod knows what you were trying to do there\n");

			buttonState = BUTTONSTATE_WAITING;

			break;

		case BUTTONSTATE_WAITING:

			butCounter++;

			// wait till buttons are released before allowing the countdown
			if (buttons[0] || buttons[1] || buttons[2] || buttons[3])
				butCounter = 0;

			if (butCounter > 20)
			{
				butCounter = 0;
				buttonState = BUTTONSTATE_NONE;

				for (i = 0; i < 4; i++)
					totalbuttons[i] = 0;
			}
			break;
		}
	}
}

// Keep a running average of speed so if we suddenly let go it keeps going at that speed
double averageSpeed = 0.0;
unsigned int numBlips = 0;
void process_rot(struct sc1000* sc1000_engine, struct sc_settings* settings)
{
	unsigned char result;
	int8_t crossedZero; // 0 when we haven't crossed zero, -1 when we've crossed in anti-clockwise direction, 1 when crossed in clockwise
	int wrappedAngle = 0x0000;
	// Handle rotary sensor

	i2c_read_address(file_i2c_rot, 0x0c, &result);
   sc1000_engine->scratch_deck.new_encoder_angle = ((int)result) << 8;
	i2c_read_address(file_i2c_rot, 0x0d, &result);
   sc1000_engine->scratch_deck.new_encoder_angle = (sc1000_engine->scratch_deck.new_encoder_angle & 0x0f00) | (int)result;

	if (settings->jog_reverse) {
		//printf("%d,",deck[1].newEncoderAngle);
		sc1000_engine->scratch_deck.new_encoder_angle = 4095 - sc1000_engine->scratch_deck.new_encoder_angle;
		//printf("%d\n",deck[1].newEncoderAngle);
	}

	// First time, make sure there's no difference
	if ( sc1000_engine->scratch_deck.encoder_angle == 0xffff)
      sc1000_engine->scratch_deck.encoder_angle = sc1000_engine->scratch_deck.new_encoder_angle;

	// Handle wrapping at zero

	if ( sc1000_engine->scratch_deck.new_encoder_angle < 1024 && sc1000_engine->scratch_deck.encoder_angle >= 3072)
	{ // We crossed zero in the positive direction

		crossedZero = 1;
		wrappedAngle = sc1000_engine->scratch_deck.encoder_angle - 4096;
	}
	else if ( sc1000_engine->scratch_deck.new_encoder_angle >= 3072 && sc1000_engine->scratch_deck.encoder_angle < 1024)
	{ // We crossed zero in the negative direction
		crossedZero = -1;
		wrappedAngle = sc1000_engine->scratch_deck.encoder_angle + 4096;
	}
	else
	{
		crossedZero = 0;
		wrappedAngle = sc1000_engine->scratch_deck.encoder_angle;
	}

	// rotary sensor sometimes returns incorrect values, if we skip more than 100 ignore that value
	// If we see 3 blips in a row, then I guess we better accept the new value
	if ( abs(sc1000_engine->scratch_deck.new_encoder_angle - wrappedAngle) > 100 && numBlips < 2)
	{
		//printf("blip! %d %d %d\n", deck[1].newEncoderAngle, deck[1].encoderAngle, wrappedAngle);
		numBlips++;
	}
	else
	{
		numBlips = 0;
      sc1000_engine->scratch_deck.encoder_angle = sc1000_engine->scratch_deck.new_encoder_angle;

		if (pitchMode)
		{

			if (!oldPitchMode)
			{ // We just entered pitchmode, set offset etc

            if(pitchMode == 0)
            {
               sc1000_engine->beat_deck.player.note_pitch = 1.0;
            }
            else
            {
               sc1000_engine->scratch_deck.player.note_pitch = 1.0;
            }

            sc1000_engine->scratch_deck.angle_offset = -sc1000_engine->scratch_deck.encoder_angle;
				oldPitchMode = 1;
            sc1000_engine->scratch_deck.player.capTouch = 0;
			}

			// Handle wrapping at zero

			if (crossedZero > 0)
			{
            sc1000_engine->scratch_deck.angle_offset += 4096;
			}
			else if (crossedZero < 0)
			{
            sc1000_engine->scratch_deck.angle_offset -= 4096;
			}

			// Use the angle of the platter to control sample pitch

         if(pitchMode == 0)
         {
            sc1000_engine->scratch_deck.player.note_pitch = (((double)(sc1000_engine->scratch_deck.encoder_angle + sc1000_engine->scratch_deck.angle_offset)) / 16384) + 1.0;
         }
         else
         {
            sc1000_engine->scratch_deck.player.note_pitch = (((double)(sc1000_engine->scratch_deck.encoder_angle + sc1000_engine->scratch_deck.angle_offset)) / 16384) + 1.0;
         }
		}
		else
		{

			if (settings->platter_enabled)
			{
				// Handle touch sensor
				if ( capIsTouched || sc1000_engine->scratch_deck.player.motor_speed == 0.0)
				{

					// Positive touching edge
					if ( !sc1000_engine->scratch_deck.player.capTouch || (oldPitchMode && !sc1000_engine->scratch_deck.player.stopped) )
					{
                  sc1000_engine->scratch_deck.angle_offset = (sc1000_engine->scratch_deck.player.position * settings->platter_speed) - sc1000_engine->scratch_deck.encoder_angle;
						printf("touch!\n");
                  sc1000_engine->scratch_deck.player.target_position = sc1000_engine->scratch_deck.player.position;
                  sc1000_engine->scratch_deck.player.capTouch = 1;
					}
				}
				else
				{
               sc1000_engine->scratch_deck.player.capTouch = 0;
				}
			}

			else
            sc1000_engine->scratch_deck.player.capTouch = 1;

			/*if (deck[1].player.capTouch) we always want to dump the target position so we can do lasers etc
			{*/

			// Handle wrapping at zero

			if (crossedZero > 0)
			{
            sc1000_engine->scratch_deck.angle_offset += 4096;
			}
			else if (crossedZero < 0)
			{
            sc1000_engine->scratch_deck.angle_offset -= 4096;
			}

			// Convert the raw value to track position and set player to that pos

			sc1000_engine->scratch_deck.player.target_position = (double)(sc1000_engine->scratch_deck.encoder_angle + sc1000_engine->scratch_deck.angle_offset) / settings->platter_speed;

			// Loop when track gets to end

			/*if (deck[1].player.target_position > ((double)deck[1].player.track->length / (double)deck[1].player.track->rate))
					{
						deck[1].player.target_position = 0;
						angleOffset = encoderAngle;
					}*/
		}
		//}
		oldPitchMode = pitchMode;
	}
}

void *run_sc_input_thread(struct sc1000* sc1000_engine, struct sc_settings* settings)
{
	unsigned char picskip = 0;
	unsigned char picpresent = 1;
	//unsigned char rotarypresent = 1;

	char mididevices[64][64];
	int mididevicenum = 0, oldmididevicenum = 0;

	// Initialise rotary sensor on I2C0

	if ( (file_i2c_rot = setup_i2c("/dev/i2c-0", 0x36)) < 0)
	{
		printf("Couldn't init rotary sensor\n");
		//rotarypresent = 0;
	}

	// Initialise PIC input processor on I2C2

	if ( (file_i2c_pic = setup_i2c("/dev/i2c-2", 0x69)) < 0)
	{
		printf("Couldn't init input processor\n");
		picpresent = 0;
	}

	init_io();

	//detect SC500 by seeing if G11 is pulled low

	if (mmappresent)
	{
		volatile uint32_t *PortDataReg = gpio_addr + (6 * 0x24) + 0x10;
		uint32_t PortData = *PortDataReg;
		PortData ^= 0xffffffff;
		if ((PortData >> 11) & 0x01)
		{
			printf("SC500 detected\n");
         settings->disable_volume_adc = 1;
         settings->disable_pic_buttons = 1;
		}
	}

	srand(time(NULL)); // TODO - need better entropy source, SoC is starting up annoyingly deterministically

	struct timeval tv;
	unsigned long lastTime = 0;
	unsigned int frameCount = 0;
	struct timespec ts;
	double inputtime = 0, lastinputtime = 0;

	sleep(2);

	int secondCount = 0;

	while (1) // Main input loop
	{

		frameCount++;

		// Update display every second
		gettimeofday(&tv, NULL);
		if (tv.tv_sec != lastTime)
		{
			lastTime = tv.tv_sec;
			printf("\033[H\033[J"); // Clear Screen
			printf("\nFPS: %06u - ADCS: %04u, %04u, %04u, %04u, %04u\nButtons: %01u,%01u,%01u,%01u,%01u\nTP: %f, P : %f\n%f -- %f\n",
                frameCount, ADCs[0], ADCs[1], ADCs[2], ADCs[3], sc1000_engine->scratch_deck.encoder_angle,
                buttons[0], buttons[1], buttons[2], buttons[3], capIsTouched,
                sc1000_engine->scratch_deck.player.target_position, sc1000_engine->scratch_deck.player.position,
                sc1000_engine->beat_deck.player.volume, sc1000_engine->scratch_deck.player.volume);
			//dump_maps();

			//printf("\nFPS: %06u\n", frameCount);
			frameCount = 0;

			// list midi devices
			for (int cunt = 0; cunt < numControllers; cunt++)
			{
				printf("MIDI : %s\n", ((struct dicer *)(midiControllers[cunt].local))->PortName);
			}

			// Wait 10 seconds to enumerate MIDI devices
			// Give them a little time to come up properly
			if ( secondCount < settings->midi_delay)
				secondCount++;
			else if ( secondCount == settings->midi_delay)
			{
				// Check for new midi devices
				mididevicenum = listdev("rawmidi", mididevices);

				// If there are more MIDI devices than last time, add them
				if (mididevicenum > oldmididevicenum)
				{
               add_new_midi_devices(sc1000_engine, mididevices, mididevicenum);
					oldmididevicenum = mididevicenum;
				}
				secondCount = 999;
			}
		}

		// Get info from input processor registers
		// First the ADC values
		// 5 = XFADER1, 6 = XFADER2, 7 = POT1, 8 = POT2

		//picpresent = 0;

		if (picpresent)
		{
			picskip++;
			if (picskip > 4)
			{
				picskip = 0;
				process_pic(sc1000_engine, settings);
				firstTimeRound = 0;
			}

			process_rot(sc1000_engine, settings);
		}
		else // couldn't find input processor, just play the tracks
		{
         sc1000_engine->scratch_deck.player.capTouch = 1;
         sc1000_engine->beat_deck.player.faderTarget = 0.0;
         sc1000_engine->scratch_deck.player.faderTarget = 0.5;
         sc1000_engine->beat_deck.player.justPlay = 1;
         sc1000_engine->beat_deck.player.pitch = 1;

			clock_gettime(CLOCK_MONOTONIC, &ts);
			inputtime = (double)ts.tv_sec + ((double)ts.tv_nsec / 1000000000.0);

			if (lastinputtime != 0)
			{
            sc1000_engine->scratch_deck.player.target_position += (inputtime - lastinputtime);
			}

			lastinputtime = inputtime;
		}

		//usleep(scsettings.update_rate);
	}
}

void *sc_input_thread( void *ptr )
{
   return run_sc_input_thread(&g_sc1000_engine, &g_sc1000_settings);
}

// Start the input thread
void start_sc_input_thread()
{
	pthread_t thread1;
	const char *message1 = "Thread 1";
	int iret1;

	iret1 = pthread_create(&thread1, NULL, sc_input_thread, ( void* ) message1);

	if (iret1)
	{
		fprintf(stderr, "Error - pthread_create() return code: %d\n", iret1);
		exit(EXIT_FAILURE);
	}
}
