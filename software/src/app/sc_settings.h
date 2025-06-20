#pragma once

#include <stdbool.h>

struct mapping;

struct sc_settings
{
   // output buffer size, probably 256
   unsigned int period_size;
   unsigned int buffer_period_factor;

   // sample rate, probably 48000
   int sample_rate;

   // fader options
   char single_vca;
   char double_cut;
   char hamster;

   // fader thresholds for hysteresis
   int fader_open_point; // value required to open the fader (when fader is closed)
   int fader_close_point; // value required to close the fader (when fader is open)

   // delay between iterations of the input loop
   int update_rate;

   // 1 when enabled, 0 when not
   int platter_enabled;

   // specifies the ratio of platter movement to sample movement
   // 4096 = 1 second for every platter rotation
   // Default 3072 = 1.33 seconds for every platter rotation
   int platter_speed;

   // How long to debounce external GPIO switches
   int debounce_time;

   // How long a button press counts as a hold
   int hold_time;

   // Virtual slipmat slippiness - how quickly the sample returns to normal speed after you let go of the jog wheel
   // Higher values are slippier
   int slippiness;

   // How long the the platter takes to stop after you hit a stop button, higher values are longer
   int brake_speed;

   // Pitch range of MIDI commands
   int pitch_range;

   // How many seconds to wait before initializing MIDI
   unsigned int midi_init_delay;

   // How many seconds to wait before initializing Audio
   unsigned int audio_init_delay;

   // whether or not to take input from the volume knobs (sc500 should enable this setting)
   int disable_volume_adc;

   // whether or not to take input from the PIC buttons (sc500 should enable this setting)
   int disable_pic_buttons;

   // how much to adjust the volumes when the volume up/down buttons are pressed or held
   double volume_amount;
   double volume_amount_held;

   // whether or not to reverse the jogwheel
   int jog_reverse;

   // whether or not the fader cuts the beats
   int cut_beats;

   double initial_volume;

   bool midi_remapped;
   bool io_remapped;

   const char* importer;
};

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

EXTERNC void sc_settings_load_user_configuration( struct sc_settings* settings, struct mapping** mappings );

#undef EXTERNC

