#pragma once

#include "sc_settings.h"
#include "sc_input.h"
#include "sc1000.h"
#include "../thread/realtime.h"

#define DEVICE_CHANNELS 2
#define TARGET_SAMPLE_RATE 48000                 // 48khz
#define TARGET_SAMPLE_FORMAT SND_PCM_FORMAT_S16  // 16-bit signed little-endian format

#define DEFAULT_IMPORTER "/root/xwax-import"

extern struct sc1000      g_sc1000_engine;
extern struct rt          g_rt;