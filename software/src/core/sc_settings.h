#pragma once

#include <stdbool.h>
#include <stdint.h>

struct mapping;

// Maximum number of audio interfaces and output channels
#define MAX_AUDIO_INTERFACES 4
#define MAX_OUTPUT_CHANNELS 16

// Audio interface types
enum audio_interface_type {
   AUDIO_TYPE_MAIN = 0,     // Main stereo output (simple, like sun4i-codec)
   AUDIO_TYPE_USB = 1,      // USB audio device (multi-channel, CV capable)
   AUDIO_TYPE_CUSTOM = 2    // Custom/other
};

// Logical output channel types
enum output_channel_type {
   OUT_NONE = 0,            // Unmapped/unused

   // Audio outputs (stereo pair starting at mapped channel)
   OUT_AUDIO,               // Main stereo mix (scratch + beat)

   // CV sources (mono, calculated only if mapped)
   OUT_CV_PLATTER_SPEED,    // Bipolar: platter rotation speed (-1 to +1)
   OUT_CV_SAMPLE_POSITION,  // Unipolar: relative position in sample (0 to 1)
   OUT_CV_CROSSFADER,       // Crossfader position
   OUT_CV_GATE_A,           // Gate: high when fader at A end (scratch open)
   OUT_CV_GATE_B,           // Gate: high when fader at B end (beat open)
   OUT_CV_PLATTER_ANGLE,    // Unipolar: absolute platter angle (0 to 1, saw LFO)
   OUT_CV_PLATTER_ACCEL,    // Bipolar: platter acceleration
   OUT_CV_DIRECTION_PULSE,  // Trigger: pulse on platter direction change
};

// Audio interface configuration
// Devices are listed in priority order - first available match is used
struct audio_interface {
   char name[64];                 // Human-readable name, e.g., "Bitwig Connect"
   char device[64];               // ALSA device name, e.g., "hw:0", "plughw:1"
   enum audio_interface_type type;
   int channels;                  // Number of hardware channels
   int sample_rate;               // Sample rate (48000 default)
   int period_size;               // Period size (256 default)
   int buffer_period_factor;      // Buffer = period * factor (4 default)
   bool supports_cv;              // Whether this device can output CV

   // Input channel configuration (for recording)
   int input_channels;            // Number of capture channels (0 = no capture)
   int input_left;                // Which capture channel is left (default 0)
   int input_right;               // Which capture channel is right (default 1)

   // Output channel mapping: output_map[hw_channel] = logical_type
   // e.g., output_map[4] = OUT_CV1 means hardware channel 4 outputs CV1
   enum output_channel_type output_map[MAX_OUTPUT_CHANNELS];
   int num_mapped_outputs;        // How many channels are mapped
};

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

   // Audio interfaces
   struct audio_interface audio_interfaces[MAX_AUDIO_INTERFACES];
   int num_audio_interfaces;

   // Loop recording settings
   int loop_max_seconds;        // Maximum loop recording duration (default 60)
};

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

EXTERNC void sc_settings_load_user_configuration( struct sc_settings* settings, struct mapping** mappings );
EXTERNC struct audio_interface* sc_settings_get_audio_interface( struct sc_settings* settings, enum audio_interface_type type );
EXTERNC void sc_settings_init_default_audio( struct sc_settings* settings );
EXTERNC int sc_settings_get_output_channel( struct audio_interface* iface, enum output_channel_type logical );
EXTERNC struct audio_interface* sc_settings_find_cv_interface( struct sc_settings* settings );

#undef EXTERNC

