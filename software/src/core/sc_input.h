#pragma once

#include <array>

#define CONTROL_NOTE 1
#define CONTROL_CC 2


// Defines a mapping between a MIDI event and an action

enum IOType
{
   MIDI,
   IO
};

enum EventType
{
   BUTTON_RELEASED = 0,
   BUTTON_PRESSED = 1,
   BUTTON_HOLDING = 2,
   BUTTON_PRESSED_SHIFTED = 3,
   BUTTON_HOLDING_SHIFTED = 4,
   BUTTON_RELEASED_SHIFTED = 5,
};

enum MIDIStatusType
{
   MIDI_NOTE_OFF = 8,
   MIDI_NOTE_ON = 9,
   MIDI_CC = 11,
   MIDI_PB = 14,
};

enum ActionType
{
   CUE,
   SHIFTON,
   SHIFTOFF,
   STARTSTOP,
   START,
   STOP,
   PITCH,
   NOTE,
   GND,
   VOLUME,
   NEXTFILE,
   PREVFILE,
   RANDOMFILE,
   NEXTFOLDER,
   PREVFOLDER,
   RECORD,
   LOOPERASE,    // Long-hold to erase loop recording
   LOOPRECALL,   // Recall last loop recording
   VOLUP,
   VOLDOWN,
   JOGPIT,
   DELETECUE,
   SC500,
   VOLUHOLD,
   VOLDHOLD,
   JOGPSTOP,
   JOGREVERSE,
   BEND,
   NOTHING,
};

struct mapping {

   // Event type (MIDI or IO)
   IOType type = IOType::MIDI;

   // IO event info
   unsigned char pin = 0;            // IO Pin Number
   bool pullup = false;              // Whether or not to pull the pin up
   EventType edge_type = EventType::BUTTON_PRESSED;  // Edge (1 for unpressed-to-pressed)

   // GPIO event info
   unsigned char gpio_port = 0;      // GPIO port number

   // MIDI event info
   std::array<unsigned char, 3> midi_command_bytes = {0, 0, 0};

   // Action
   unsigned char   deck_no = 0;      // Which deck to apply this action to
   ActionType action_type = ActionType::NOTHING;  // The action to take - cue, shift etc
   unsigned char   parameter = 0;    // for example the output note

   int debounce = 0;
   bool shifted_at_press = false;    // Latched shift state when button was pressed
};

#ifdef __cplusplus
extern "C" {
#endif

void start_sc_input_thread(void);

#ifdef __cplusplus
}
#endif

// Action dispatch and mapping lookup are now in control/actions.h