#pragma once

#pragma once

#include <stdbool.h>

#define CONTROL_NOTE 1
#define CONTROL_CC 2


// Defines a mapping between a MIDI event and an action

enum IOType
{
   MIDI,
   IO
};

enum EdgeType
{
   RELEASED = 0,
   PRESSED = 1,
   HOLDING = 2,
   PRESSED_SHIFT = 3,
   HOLDING_SHIFT = 4,
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
   enum IOType type;

   // IO event info
   unsigned char pin;               // IO Pin Number
   bool pullup;                     // Whether or not to pull the pin up
   enum EdgeType edge_type;         // Edge (1 for unpressed-to-pressed)

   // GPIO event info
   unsigned char gpio_port;         // GPIO port number

   // MIDI event info
   unsigned char midi_command_bytes[3];

   // Action
   unsigned char   deck_no;     // Which deck to apply this action to
   enum ActionType action_type; // The action to take - cue, shift etc
   unsigned char   parameter;   // for example the output note

   int debounce;

   struct mapping *next;
};

void start_sc_input_thread();