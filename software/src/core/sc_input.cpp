// SC1000 input handler
// Thread that grabs data from the rotary sensor and PIC input processor and processes it

#include <cassert>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <cstdint>
#include <sys/time.h>
#include <cmath>
#include <ctime>
#include <pthread.h>
#include <vector>
#include <memory>

#include "../platform/platform.h"
#include "../platform/alsa.h"

#include "../player/playlist.h"
#include "../input/midi_controller.h"
#include "../thread/rig.h"

#include "../input/controller.h"
#include "../platform/midi.h"
#include "../input/midi_event.h"

#include "global.h"
#include "sc_input.h"
#include "sc_control_mapping.h"
#include "../control/actions.h"
#include "../engine/audio_engine.h"

using namespace sc::platform;

// Use control module functions and state
using sc::control::shifted;
using sc::control::pitch_mode;
using sc::control::find_io_mapping;
using sc::control::find_midi_mapping;

namespace sc {
namespace input {

/*
 * InputContext holds all mutable state for the input thread.
 * This consolidates what were previously scattered globals.
 * Future refactoring can pass this through functions or make it a proper class.
 */
struct InputContext {
    // Platform hardware (GPIO, encoder, PIC)
    HardwareState hw;

    // Shift key state
    bool shift_latched = false;

    // MIDI controllers (owned via unique_ptr)
    std::vector<std::unique_ptr<MidiController>> midi_controllers;

    // First-time initialization flag
    bool first_time = true;

    // Pitch mode transition tracking (pitch_mode itself is in control module)
    int old_pitch_mode = 0;

    // PIC readings cache
    PicReadings pic_readings = {};
    unsigned char total_buttons[4] = {0, 0, 0, 0};
    unsigned char button_state = 0;
    unsigned int button_counter = 0;

    // Fader state
    unsigned char fader_open1 = 0;
    unsigned char fader_open2 = 0;

    // Rotary sensor filtering
    double average_speed = 0.0;
    unsigned int num_blips = 0;
};

// Singleton input context - later can be moved into sc1000 struct or passed explicitly
static InputContext g_input_ctx;

// Convenience accessors for backwards compatibility during transition
#define shift_latched g_input_ctx.shift_latched
#define gpiopresent g_input_ctx.hw.gpio.mcp23017_present
#define mmappresent g_input_ctx.hw.gpio.mmap_present
#define first_time g_input_ctx.first_time
#define old_pitch_mode g_input_ctx.old_pitch_mode
#define cap_is_touched g_input_ctx.pic_readings.cap_touched
#define buttons g_input_ctx.pic_readings.buttons
#define totalbuttons g_input_ctx.total_buttons
#define ADCs g_input_ctx.pic_readings.adc
#define buttonState g_input_ctx.button_state
#define butCounter g_input_ctx.button_counter
#define fader_open1 g_input_ctx.fader_open1
#define fader_open2 g_input_ctx.fader_open2
#define average_speed g_input_ctx.average_speed
#define num_blips g_input_ctx.num_blips

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

void add_new_midi_devices(struct sc1000* sc1000_engine, char mididevices[64][64], int mididevicenum)
{
    auto& controllers = g_input_ctx.midi_controllers;

    // Search to see which devices we've already added
    for (int devc = 0; devc < mididevicenum; devc++)
    {
        bool already_added = false;

        for (const auto& controller : controllers)
        {
            if (strcmp(mididevices[devc], controller->port_name()) == 0) {
                already_added = true;
                break;
            }
        }

        if (!already_added)
        {
            auto controller = create_midi_controller(&g_rt, mididevices[devc]);
            if (controller)
            {
                printf("Adding MIDI device %zu - %s\n", controllers.size(), mididevices[devc]);
                controller_add_deck(controller.get(), &sc1000_engine->beat_deck);
                controller_add_deck(controller.get(), &sc1000_engine->scratch_deck);
                controllers.push_back(std::move(controller));
            }
        }
    }
}

// Old globals now in InputContext struct (via macros for compatibility)

void init_io(struct sc1000* sc1000_engine)
{
    struct mapping* map;
    GpioState* gpio = &g_input_ctx.hw.gpio;

    // Initialize MCP23017 GPIO expander
    gpio_init_mcp23017(gpio);

    // Configure MCP23017 pins based on mappings
    if (gpio->mcp23017_present)
    {
        for (int i = 0; i < 16; i++)
        {
            map = find_io_mapping(sc1000_engine->mappings, 0, i, EventType::BUTTON_PRESSED);

            // If pin is marked as ground, set as output
            if (map != NULL && map->action_type == GND)
            {
                gpio_mcp23017_set_direction(gpio, i, false);  // output
            }
            else
            {
                gpio_mcp23017_set_direction(gpio, i, true);   // input
            }

            // Configure pullup
            bool pullup = (map == NULL || map->pullup);
            gpio_mcp23017_set_pullup(gpio, i, pullup);
            if (pullup) printf("Pulling up pin %d\n", i);
        }
    }

    // Initialize A13 memory-mapped GPIO
    gpio_init_a13_mmap(gpio);

    // Configure A13 GPIO pins based on mappings
    if (gpio->mmap_present)
    {
        for (int j = 1; j <= 6; j++)
        {
            for (int i = 0; i < 28; i++)
            {
                map = find_io_mapping(sc1000_engine->mappings, j, i, EventType::BUTTON_PRESSED);

                if (map != NULL)
                {
                    // dirty hack, don't map J7 SCL/SDA pins if MCP is present
                    if (gpio->mcp23017_present && j == 1 && (i == 15 || i == 16))
                    {
                        map->action_type = NOTHING;
                    }
                    else
                    {
                        gpio_a13_configure_input(gpio, j, i, map->pullup);
                    }
                }
            }
        }
    }
}

void process_io(struct sc1000* sc1000_engine)
{
    struct sc_settings* settings = sc1000_engine->settings;
    GpioState* gpio = &g_input_ctx.hw.gpio;

    // Read all MCP23017 pins at once (already inverted by platform layer)
    uint16_t mcp_pins = 0;
    if (gpio->mcp23017_present)
    {
        mcp_pins = gpio_mcp23017_read_all(gpio);
    }

    struct mapping* last_map = sc1000_engine->mappings;

    while (last_map != NULL)
    {
        // Only digital pins
        if (last_map->type == IO && (!(last_map->gpio_port == 0 && !gpio->mcp23017_present)))
        {
            bool pin_value = false;
            if (last_map->gpio_port == 0 && gpio->mcp23017_present) // port 0, I2C GPIO expander
            {
                pin_value = (mcp_pins >> last_map->pin) & 0x01;
            }
            else if (gpio->mmap_present) // Ports 1-6, A13 GPIO
            {
                pin_value = gpio_a13_read_pin(gpio, last_map->gpio_port, last_map->pin);
            }
            else
            {
                pin_value = false;
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

    // Process MIDI events from the lock-free queue
    unsigned char midi_bytes[3];
    int midi_shifted;
    while (midi_event_queue_pop(midi_bytes, &midi_shifted)) {
        EventType edge = midi_shifted ? BUTTON_PRESSED_SHIFTED : BUTTON_PRESSED;
        struct mapping* midi_map = find_midi_mapping(sc1000_engine->mappings, midi_bytes, edge);
        if (midi_map != nullptr) {
            io_event(midi_map, midi_bytes, sc1000_engine, settings);
        }
    }
}

// Old globals (file_i2c_rot, pitch_mode, buttons, etc.) now in InputContext struct

void process_pic(struct sc1000* sc1000_engine)
{
    struct sc_settings* settings = sc1000_engine->settings;

    unsigned int i;

    unsigned int fader_cut_point1, fader_cut_point2;

    double fadertarget0, fadertarget1;

    // Read all PIC inputs using platform module
    g_input_ctx.pic_readings = pic_read_all(&g_input_ctx.hw.pic);

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
                shift_latched = 1;
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

// average_speed and num_blips now in InputContext struct

void process_rot(struct sc1000* sc1000_engine)
{
    struct sc_settings* settings = sc1000_engine->settings;

    int8_t crossed_zero;
    // 0 when we haven't crossed zero, -1 when we've crossed in anti-clockwise direction, 1 when crossed in clockwise
    int wrapped_angle = 0x0000;

    // Read encoder angle using platform module
    sc1000_engine->scratch_deck.new_encoder_angle = encoder_read_angle(&g_input_ctx.hw.encoder);

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
    HardwareState* hw = &g_input_ctx.hw;

    unsigned char picskip = 0;

    char mididevices[64][64];
    int mididevicenum = 0, oldmididevicenum = 0;

    // Initialize encoder (rotary sensor on I2C0)
    if (!encoder_init(&hw->encoder))
    {
        printf("Couldn't init rotary sensor\n");
    }
    else
    {
        printf("Encoder initialized OK, present=%d\n", hw->encoder.present);
    }

    // Initialize PIC input processor on I2C2
    if (!pic_init(&hw->pic))
    {
        printf("Couldn't init input processor\n");
    }
    else
    {
        printf("PIC initialized OK, present=%d\n", hw->pic.present);
    }

    init_io(sc1000_engine);

    // Print settings for debugging
    printf("Settings: platter_enabled=%d, platter_speed=%d, jog_reverse=%d\n",
           settings->platter_enabled, settings->platter_speed, settings->jog_reverse);

    // Detect SC500 by seeing if G11 is pulled low
    if (hw->gpio.mmap_present)
    {
        if (gpio_a13_read_pin(&hw->gpio, 6, 11))
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

            // Get DSP stats
            struct dsp_stats dsp;
            audio_engine_get_stats(&dsp);

            printf(
                "\nFPS: %06u - ADCS: %04u, %04u, %04u, %04u\n"
                "DSP: %.1f%% (peak: %.1f%%, %.0fus/%.0fus, xruns: %lu)\n"
                "Enc: %04d (new: %04d) Cap: %d CapTouch: %d\n"
                "Buttons: %01u,%01u,%01u,%01u\n"
                "TP: %f, P: %f\nVol: %f -- %f\n",
                frameCount, ADCs[0], ADCs[1], ADCs[2], ADCs[3],
                dsp.load_percent, dsp.load_peak, dsp.process_time_us, dsp.budget_time_us, dsp.xruns,
                sc1000_engine->scratch_deck.encoder_angle, sc1000_engine->scratch_deck.new_encoder_angle,
                cap_is_touched, sc1000_engine->scratch_deck.player.cap_touch,
                buttons[0], buttons[1], buttons[2], buttons[3],
                sc1000_engine->scratch_deck.player.target_position, sc1000_engine->scratch_deck.player.position,
                sc1000_engine->beat_deck.player.volume, sc1000_engine->scratch_deck.player.volume);
            //dump_maps();

            //printf("\nFPS: %06u\n", frameCount);
            frameCount = 0;

            // list midi devices
            for (const auto& controller : g_input_ctx.midi_controllers)
            {
                printf("MIDI : %s\n", controller->port_name());
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

        if (hw->pic.present)
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

} // namespace input
} // namespace sc


////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Start the input thread
void start_sc_input_thread()
{
    pthread_t thread1;
    int iret1;

    printf("Starting GPIO input thread\n");

    iret1 = pthread_create(&thread1, nullptr, sc::input::sc_input_thread, nullptr);

    if (iret1)
    {
        fprintf(stderr, "Error - pthread_create() return code: %d\n", iret1);
        exit(EXIT_FAILURE);
    }
}
