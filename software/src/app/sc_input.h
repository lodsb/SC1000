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

enum EventType
{
   BUTTON_RELEASED = 0,
   BUTTON_PRESSED = 1,
   BUTTON_HOLDING = 2,
   BUTTON_PRESSED_SHIFTED = 3,
   BUTTON_HOLDING_SHIFTED = 4,
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
   enum EventType edge_type;         // Edge (1 for unpressed-to-pressed)

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

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif
EXTERNC void start_sc_input_thread();

EXTERNC struct mapping *find_midi_mapping( struct mapping *mappings, unsigned char buf[3], enum EventType edge );
EXTERNC struct mapping *find_io_mapping( struct mapping *mappings, unsigned char port, unsigned char pin, enum EventType edge );

EXTERNC void io_event( struct mapping *map, unsigned char midi_buffer[3], struct sc1000* sc1000_engine, struct sc_settings* settings );

#undef EXTERNC