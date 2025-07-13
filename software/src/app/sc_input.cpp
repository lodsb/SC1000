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

#include "../audio/alsa.h"

#include "../player/playlist.h"
#include "../player/dicer.h"
#include "../thread/rig.h"

#include "../input/controller.h"
#include "../input/midi.h"


#include "global.h"
#include "sc_input.h"
#include "sc_control_mapping.h"

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

void i2c_read_address(int file_i2c, unsigned char address, unsigned char* result)
{
    *result = address;
    if (write(file_i2c, result, 1) != 1)
    {
        printf("I2C read error (write)\n");
        //exit(1);
    }

    if (read(file_i2c, result, 1) != 1)
    {
        printf("I2C read error\n");
        //exit(1);
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
    struct mapping* new_map = g_sc1000_engine.mappings;
    while (new_map != NULL)
    {
        printf("Dump Mapping - ty:%d po:%d pn%x pl:%x ed%x mid:%x:%x:%x- dn:%d, a:%d, p:%d\n", new_map->type,
               new_map->gpio_port, new_map->pin, new_map->pullup, new_map->edge_type, new_map->midi_command_bytes[0],
               new_map->midi_command_bytes[1], new_map->midi_command_bytes[2], new_map->deck_no, new_map->action_type,
               new_map->parameter);
        new_map = new_map->next;
    }
}

int setup_i2c(const char* path, unsigned char address)
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

void add_new_midi_devices(struct sc1000* sc1000_engine, char mididevices[64][64], int mididevicenum)
{
    bool alreadyAdded;
    // Search to see which devices we've already added
    for (int devc = 0; devc < mididevicenum; devc++)
    {
        alreadyAdded = 0;

        for (int controlc = 0; controlc < numControllers; controlc++)
        {
            char* controlName = ((struct dicer*)(midiControllers[controlc].local))->PortName;
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
volatile void* gpio_addr;

bool first_time = 1;

void init_io(struct sc1000* sc1000_engine)
{
    int i, j;
    struct mapping* map;

    // Initialise external MCP23017 GPIO on I2C1
    if ((file_i2c_gpio = setup_i2c("/dev/i2c-1", 0x20)) < 0)
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
            map = find_io_mapping(sc1000_engine->mappings, 0, i, EventType::BUTTON_PRESSED);
            // If pin is marked as ground
            if (map != NULL && map->action_type == GND)
            {
                //printf("Grounding pin %d\n", i);
                iodirs &= ~(0x0001 << i);
            }

            // If pin's pullup is disabled
            if (map != NULL && !map->pullup)
            {
                //printf("Disabling pin %d pullup\n", i);
                pullups &= ~(0x0001 << i);
            }
            else printf("Pulling up pin %d\n", i);
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
                map = find_io_mapping(sc1000_engine->mappings, j, i, EventType::BUTTON_PRESSED);

                if (map != NULL)
                {
                    // dirty hack, don't map J7 SCL/SDA pins if MCP is present
                    if (gpiopresent && j == 1 && (i == 15 || i == 16))
                    {
                        map->action_type = NOTHING;
                    }
                    else
                    {
                        //printf("Pulling %d %d %d\n", j, i, map->Pullup);
                        // which config register to use, 0-3
                        uint32_t configregister = i >> 3;

                        // which pull register to use, 0-1
                        uint32_t pullregister = i >> 4;

                        // how many bits to shift the config register
                        uint32_t configShift = (i % 8) * 4;

                        // how many bits to shift the pull register
                        uint32_t pullShift = (i % 16) * 2;

                        volatile uint32_t* PortConfigRegister = static_cast<volatile uint32_t*>(gpio_addr + j * 0x24 + configregister * 0x04);
                        volatile uint32_t* PortPullRegister   = static_cast<volatile uint32_t*>(gpio_addr + j * 0x24 + 0x1C + pullregister * 0x04);
                        uint32_t portConfig = *PortConfigRegister;
                        uint32_t portPull = *PortPullRegister;

                        // mask to unset the relevant pins in the registers
                        uint32_t configMask = ~(0b1111 << configShift);
                        uint32_t pullMask = ~(0b11 << pullShift);

                        // Set port as input
                        // portConfig = (portConfig & configMask) | (0b0000 << configShift); (not needed because input is 0 anyway)
                        portConfig = (portConfig & configMask);

                        portPull = (portPull & pullMask) | (map->pullup << pullShift);
                        *PortConfigRegister = portConfig;
                        *PortPullRegister = portPull;
                    }
                }
            }
        }
    }
}

void process_io(struct sc1000* sc1000_engine)
{
    struct sc_settings* settings = sc1000_engine->settings;

    // Iterate through all digital input mappings and check the appropriate pin
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

    struct mapping* last_map = sc1000_engine->mappings;

    while (last_map != NULL)
    {
        //printf("arses : %d %d\n", last_map->port, last_map->Pin);

        // Only digital pins
        if (last_map->type == IO && (!(last_map->gpio_port == 0 && !gpiopresent)))
        {
            bool pin_value = 0;
            if (last_map->gpio_port == 0 && gpiopresent) // port 0, I2C GPIO expander
            {
                pin_value = (bool)((gpios >> last_map->pin) & 0x01);
            }
            else if (mmappresent) // Ports 1-6, olimex GPIO
            {
                volatile uint32_t* port_data_reg = static_cast<volatile uint32_t*>(gpio_addr + last_map->gpio_port * 0x24 + 0x10);
                uint32_t port_data = *port_data_reg;
                port_data ^= 0xffffffff;
                pin_value = (bool)((port_data >> last_map->pin) & 0x01);
            }
            else
            {
                pin_value = 0;
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
                if (pin_value)
                {
                    printf("Button %d pressed\n", last_map->pin);
                    if (first_time && last_map->deck_no == 1 && (last_map->action_type == VOLUP || last_map->action_type
                        == VOLDOWN))
                    {
                        player_set_track(&sc1000_engine->beat_deck.player,
                                         track_acquire_by_import(sc1000_engine->beat_deck.importer,
                                                                 "/var/os-version.mp3"));
                        cues_load_from_file(&sc1000_engine->beat_deck.cues,
                                            sc1000_engine->beat_deck.player.track->path);
                        sc1000_engine->scratch_deck.player.set_volume = 0.0;
                    }
                    else
                    {
                        if ((!shifted && last_map->edge_type == BUTTON_PRESSED) || (shifted && last_map->edge_type ==
                            BUTTON_PRESSED_SHIFTED))
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
            else if (last_map->debounce >= settings->debounce_time && last_map->debounce < settings->hold_time)
            {
                // check to see if unpressed
                if (!pin_value)
                {
                    printf("Button %d released\n", last_map->pin);
                    if (last_map->edge_type == BUTTON_RELEASED)
                        io_event(last_map, NULL, sc1000_engine, settings);
                    // start the counter
                    last_map->debounce = -settings->debounce_time;
                }

                else
                    last_map->debounce++;
            }
            // Button has been held for a while
            else if (last_map->debounce == settings->hold_time)
            {
                printf("Button %d-%d held\n", last_map->gpio_port, last_map->pin);
                if ((!shifted && last_map->edge_type == BUTTON_HOLDING) || (shifted && last_map->edge_type ==
                    BUTTON_HOLDING_SHIFTED))
                    io_event(last_map, NULL, sc1000_engine, settings);
                last_map->debounce++;
            }

            // Button still holding, check for release
            else if (last_map->debounce > settings->hold_time)
            {
                if (pin_value)
                {
                    if (last_map->action_type == VOLUHOLD || last_map->action_type == VOLDHOLD)
                    {
                        // keep running the vol up/down actions if they're held
                        if ((!shifted && last_map->edge_type == BUTTON_HOLDING) || (shifted && last_map->edge_type ==
                            BUTTON_HOLDING_SHIFTED))
                            io_event(last_map, NULL, sc1000_engine, settings);
                    }
                }
                // check to see if unpressed
                else
                {
                    printf("Button %d released\n", last_map->pin);
                    if (last_map->edge_type == BUTTON_RELEASED)
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
    if (queued_midi_command != NULL)
    {
        io_event(queued_midi_command, queued_midi_buffer, sc1000_engine, settings);
        queued_midi_command = NULL;
    }
}

int file_i2c_rot, file_i2c_pic;

int pitch_mode = 0; // If we're in pitch-change mode
int old_pitch_mode = 0;
bool cap_is_touched = 0;
unsigned char buttons[4] = {0, 0, 0, 0}, totalbuttons[4] = {0, 0, 0, 0};
unsigned int ADCs[4] = {0, 0, 0, 0};
unsigned char buttonState = 0;
unsigned int butCounter = 0;
unsigned char fader_open1 = 0, fader_open2 = 0;

void process_pic(struct sc1000* sc1000_engine)
{
    struct sc_settings* settings = sc1000_engine->settings;

    unsigned int i;

    unsigned char result;

    unsigned int fader_cut_point1, fader_cut_point2;

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
    cap_is_touched = (result >> 4 & 0x01);

    process_io(sc1000_engine);

    // Apply volume and fader

    if (!settings->disable_volume_adc)
    {
        sc1000_engine->beat_deck.player.set_volume = ((double)ADCs[2]) / 1024;
        sc1000_engine->scratch_deck.player.set_volume = ((double)ADCs[3]) / 1024;
    }

    // Fader Hysteresis
    fader_cut_point1 = (unsigned int)(fader_open1 ? settings->fader_close_point : settings->fader_open_point);
    fader_cut_point2 = (unsigned int)(fader_open2 ? settings->fader_close_point : settings->fader_open_point);

    fader_open1 = 1;
    fader_open2 = 1;

    fadertarget0 = sc1000_engine->beat_deck.player.set_volume;
    fadertarget1 = sc1000_engine->scratch_deck.player.set_volume;


    if (ADCs[0] < fader_cut_point1)
    {
        if (settings->cut_beats == 1) fadertarget0 = 0.0;
        else fadertarget1 = 0.0;
        fader_open1 = 0;
    }
    if (ADCs[1] < fader_cut_point2)
    {
        if (settings->cut_beats == 2) fadertarget0 = 0.0;
        else fadertarget1 = 0.0;
        fader_open2 = 0;
    }

    sc1000_engine->beat_deck.player.fader_target = fadertarget0;
    sc1000_engine->scratch_deck.player.fader_target = fadertarget1;

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

                if (first_time)
                {
                    player_set_track(&sc1000_engine->beat_deck.player,
                                     track_acquire_by_import(sc1000_engine->beat_deck.importer, "/var/os-version.mp3"));
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
            if (butCounter > settings->hold_time)
            {
                butCounter = 0;
                buttonState = BUTTONSTATE_ACTING_HELD;
            }

            break;

        // Act on instantaneous (i.e. not held) button press
        case BUTTONSTATE_ACTING_INSTANT:

            // Any button to stop pitch mode
            if (pitch_mode)
            {
                pitch_mode = 0;
                old_pitch_mode = 0;
                printf("Pitch mode Disabled\n");
            }
            else if (totalbuttons[0] && !totalbuttons[1] && !totalbuttons[2] && !totalbuttons[3] && sc1000_engine->
                scratch_deck.files_present)
                deck_prev_file(&sc1000_engine->scratch_deck, settings);
            else if (!totalbuttons[0] && totalbuttons[1] && !totalbuttons[2] && !totalbuttons[3] && sc1000_engine->
                scratch_deck.files_present)
                deck_next_file(&sc1000_engine->scratch_deck, settings);
            else if (totalbuttons[0] && totalbuttons[1] && !totalbuttons[2] && !totalbuttons[3] && sc1000_engine->
                scratch_deck.files_present)
                pitch_mode = 2;
            else if (!totalbuttons[0] && !totalbuttons[1] && totalbuttons[2] && !totalbuttons[3] && sc1000_engine->
                beat_deck.files_present)
                deck_prev_file(&sc1000_engine->beat_deck, settings);
            else if (!totalbuttons[0] && !totalbuttons[1] && !totalbuttons[2] && totalbuttons[3] && sc1000_engine->
                beat_deck.files_present)
                deck_next_file(&sc1000_engine->beat_deck, settings);
            else if (!totalbuttons[0] && !totalbuttons[1] && totalbuttons[2] && totalbuttons[3] && sc1000_engine->
                beat_deck.files_present)
                pitch_mode = 1;
            else if (totalbuttons[0] && totalbuttons[1] && totalbuttons[2] && totalbuttons[3])
                shiftLatched = 1;
            else
                printf("Sod knows what you were trying to do there\n");

            buttonState = BUTTONSTATE_WAITING;

            break;

        // Act on whatever buttons are being held down when the timeout happens
        case BUTTONSTATE_ACTING_HELD:
            if (buttons[0] && !buttons[1] && !buttons[2] && !buttons[3] && sc1000_engine->scratch_deck.files_present)
                deck_prev_folder(&sc1000_engine->scratch_deck, settings);
            else if (!buttons[0] && buttons[1] && !buttons[2] && !buttons[3] && sc1000_engine->scratch_deck.
                files_present)
                deck_next_folder(&sc1000_engine->scratch_deck, settings);
            else if (buttons[0] && buttons[1] && !buttons[2] && !buttons[3] && sc1000_engine->scratch_deck.
                files_present)
                deck_random_file(&sc1000_engine->scratch_deck, settings);
            else if (!buttons[0] && !buttons[1] && buttons[2] && !buttons[3] && sc1000_engine->beat_deck.files_present)
                deck_prev_folder(&sc1000_engine->beat_deck, settings);
            else if (!buttons[0] && !buttons[1] && !buttons[2] && buttons[3] && sc1000_engine->beat_deck.files_present)
                deck_next_folder(&sc1000_engine->beat_deck, settings);
            else if (!buttons[0] && !buttons[1] && buttons[2] && buttons[3] && sc1000_engine->beat_deck.files_present)
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
double average_speed = 0.0;
unsigned int num_blips = 0;

void process_rot(struct sc1000* sc1000_engine)
{
    struct sc_settings* settings = sc1000_engine->settings;

    unsigned char result;
    int8_t crossed_zero;
    // 0 when we haven't crossed zero, -1 when we've crossed in anti-clockwise direction, 1 when crossed in clockwise
    int wrapped_angle = 0x0000;

    // Handle rotary sensor

    i2c_read_address(file_i2c_rot, 0x0c, &result);
    sc1000_engine->scratch_deck.new_encoder_angle = ((int)result) << 8;
    i2c_read_address(file_i2c_rot, 0x0d, &result);
    sc1000_engine->scratch_deck.new_encoder_angle = (sc1000_engine->scratch_deck.new_encoder_angle & 0x0f00) | (int)
        result;

    if (settings->jog_reverse)
    {
        //printf("%d,",deck[1].newEncoderAngle);
        sc1000_engine->scratch_deck.new_encoder_angle = 4095 - sc1000_engine->scratch_deck.new_encoder_angle;
        //printf("%d\n",deck[1].newEncoderAngle);
    }

    // First time, make sure there's no difference
    if (sc1000_engine->scratch_deck.encoder_angle == 0xffff)
        sc1000_engine->scratch_deck.encoder_angle = sc1000_engine->scratch_deck.new_encoder_angle;

    // Handle wrapping at zero

    if (sc1000_engine->scratch_deck.new_encoder_angle < 1024 && sc1000_engine->scratch_deck.encoder_angle >= 3072)
    {
        // We crossed zero in the positive direction

        crossed_zero = 1;
        wrapped_angle = sc1000_engine->scratch_deck.encoder_angle - 4096;
    }
    else if (sc1000_engine->scratch_deck.new_encoder_angle >= 3072 && sc1000_engine->scratch_deck.encoder_angle < 1024)
    {
        // We crossed zero in the negative direction
        crossed_zero = -1;
        wrapped_angle = sc1000_engine->scratch_deck.encoder_angle + 4096;
    }
    else
    {
        crossed_zero = 0;
        wrapped_angle = sc1000_engine->scratch_deck.encoder_angle;
    }

    // rotary sensor sometimes returns incorrect values, if we skip more than 100 ignore that value
    // If we see 3 blips in a row, then I guess we better accept the new value
    if (abs(sc1000_engine->scratch_deck.new_encoder_angle - wrapped_angle) > 100 && num_blips < 2)
    {
        //printf("blip! %d %d %d\n", deck[1].newEncoderAngle, deck[1].encoderAngle, wrappedAngle);
        num_blips++;
    }
    else
    {
        num_blips = 0;
        sc1000_engine->scratch_deck.encoder_angle = sc1000_engine->scratch_deck.new_encoder_angle;

        if (pitch_mode)
        {
            if (!old_pitch_mode)
            {
                // We just entered pitchmode, set offset etc

                if (pitch_mode == 0)
                {
                    sc1000_engine->beat_deck.player.note_pitch = 1.0;
                }
                else
                {
                    sc1000_engine->scratch_deck.player.note_pitch = 1.0;
                }

                sc1000_engine->scratch_deck.angle_offset = -sc1000_engine->scratch_deck.encoder_angle;
                old_pitch_mode = 1;
                sc1000_engine->scratch_deck.player.cap_touch = 0;
            }

            // Handle wrapping at zero

            if (crossed_zero > 0)
            {
                sc1000_engine->scratch_deck.angle_offset += 4096;
            }
            else if (crossed_zero < 0)
            {
                sc1000_engine->scratch_deck.angle_offset -= 4096;
            }

            // Use the angle of the platter to control sample pitch

            if (pitch_mode == 0)
            {
                sc1000_engine->scratch_deck.player.note_pitch = (((double)(sc1000_engine->scratch_deck.encoder_angle +
                    sc1000_engine->scratch_deck.angle_offset)) / 16384) + 1.0;
            }
            else
            {
                sc1000_engine->scratch_deck.player.note_pitch = (((double)(sc1000_engine->scratch_deck.encoder_angle +
                    sc1000_engine->scratch_deck.angle_offset)) / 16384) + 1.0;
            }
        }
        else
        {
            if (settings->platter_enabled)
            {
                // Handle touch sensor
                if (cap_is_touched || sc1000_engine->scratch_deck.player.motor_speed == 0.0)
                {
                    // Positive touching edge
                    if (!sc1000_engine->scratch_deck.player.cap_touch || (old_pitch_mode && !sc1000_engine->scratch_deck
                        .player.stopped))
                    {
                        sc1000_engine->scratch_deck.angle_offset = (int32_t)(
                            (sc1000_engine->scratch_deck.player.position * settings->platter_speed) -
                            sc1000_engine->scratch_deck.encoder_angle);

                        printf("touch!\n");
                        sc1000_engine->scratch_deck.player.target_position = sc1000_engine->scratch_deck.player.
                            position;
                        sc1000_engine->scratch_deck.player.cap_touch = 1;
                    }
                }
                else
                {
                    sc1000_engine->scratch_deck.player.cap_touch = 0;
                }
            }

            else
                sc1000_engine->scratch_deck.player.cap_touch = 1;

            /*if (deck[1].player.capTouch) we always want to dump the target position so we can do lasers etc
            {*/

            // Handle wrapping at zero

            if (crossed_zero > 0)
            {
                sc1000_engine->scratch_deck.angle_offset += 4096;
            }
            else if (crossed_zero < 0)
            {
                sc1000_engine->scratch_deck.angle_offset -= 4096;
            }

            // Convert the raw value to track position and set player to that pos

            sc1000_engine->scratch_deck.player.target_position = (double)(sc1000_engine->scratch_deck.encoder_angle +
                sc1000_engine->scratch_deck.angle_offset) / settings->platter_speed;

            // Loop when track gets to end

            /*if (deck[1].player.target_position > ((double)deck[1].player.track->length / (double)deck[1].player.track->rate))
                    {
                        deck[1].player.target_position = 0;
                        angleOffset = encoderAngle;
                    }*/
        }
        //}
        old_pitch_mode = pitch_mode;
    }
}

void* run_sc_input_thread(struct sc1000* sc1000_engine)
{
    struct sc_settings* settings = sc1000_engine->settings;

    unsigned char picskip = 0;
    unsigned char picpresent = 1;
    //unsigned char rotarypresent = 1;

    char mididevices[64][64];
    int mididevicenum = 0, oldmididevicenum = 0;

    // Initialise rotary sensor on I2C0

    if ((file_i2c_rot = setup_i2c("/dev/i2c-0", 0x36)) < 0)
    {
        printf("Couldn't init rotary sensor\n");
        //rotarypresent = 0;
    }

    // Initialise PIC input processor on I2C2

    if ((file_i2c_pic = setup_i2c("/dev/i2c-2", 0x69)) < 0)
    {
        printf("Couldn't init input processor\n");
        picpresent = 0;
    }

    init_io(sc1000_engine);

    //detect SC500 by seeing if G11 is pulled low

    if (mmappresent)
    {
        volatile uint32_t* port_data_reg = static_cast<volatile uint32_t*>(gpio_addr + 6 * 0x24 + 0x10);
        uint32_t port_data = *port_data_reg;
        port_data ^= 0xffffffff;
        if ((port_data >> 11) & 0x01)
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

            //printf("\033[H\033[J"); // Clear Screen

            printf(
                "\nFPS: %06u - ADCS: %04u, %04u, %04u, %04u, %04u\nButtons: %01u,%01u,%01u,%01u,%01u\nTP: %f, P : %f\n%f -- %f\n",
                frameCount, ADCs[0], ADCs[1], ADCs[2], ADCs[3], sc1000_engine->scratch_deck.encoder_angle,
                buttons[0], buttons[1], buttons[2], buttons[3], cap_is_touched,
                sc1000_engine->scratch_deck.player.target_position, sc1000_engine->scratch_deck.player.position,
                sc1000_engine->beat_deck.player.volume, sc1000_engine->scratch_deck.player.volume);
            //dump_maps();

            //printf("\nFPS: %06u\n", frameCount);
            frameCount = 0;

            // list midi devices
            for (int cunt = 0; cunt < numControllers; cunt++)
            {
                printf("MIDI : %s\n", ((struct dicer*)(midiControllers[cunt].local))->PortName);
            }

            // Wait 10 seconds to enumerate MIDI devices
            // Give them a little time to come up properly
            if (secondCount < settings->midi_init_delay)
                secondCount++;
            else if (secondCount == settings->midi_init_delay)
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
                process_pic(sc1000_engine);
                first_time = 0;
            }

            process_rot(sc1000_engine);
        }
        else // couldn't find input processor, just play the tracks
        {
            sc1000_engine->scratch_deck.player.cap_touch = 1;
            sc1000_engine->beat_deck.player.fader_target = 0.0;
            sc1000_engine->scratch_deck.player.fader_target = 0.5;
            sc1000_engine->beat_deck.player.just_play = 1;
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

void* sc_input_thread(void* ptr)
{
    return run_sc_input_thread(&g_sc1000_engine);
}

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



////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start the input thread
void start_sc_input_thread()
{
    pthread_t thread1;
    const char* message1 = "Thread 1";
    int iret1;

    printf("Starting GPIO input thread\n");

    iret1 = pthread_create(&thread1, NULL, sc_input_thread, (void*)message1);

    if (iret1)
    {
        fprintf(stderr, "Error - pthread_create() return code: %d\n", iret1);
        exit(EXIT_FAILURE);
    }
}
