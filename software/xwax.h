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

#ifndef XWAX_H
#define XWAX_H

#include "player/deck.h"


extern struct deck decks[];


typedef struct SC_SETTINGS
{

   // output buffer size, probably 256
   int buffer_size;

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
   int midi_delay;

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


} SC_SETTINGS;

extern SC_SETTINGS scsettings;

#endif
