# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

SC1000 is a real-time DJ mixing/scratching software for the SC1000 digital scratch instrument hardware. Originally adapted from the xwax project, now significantly rewritten. Runs on ARM Cortex-A8 Linux. The codebase is C++17.

## Build Commands

### CMake (primary)
```bash
# Debug build (native x86 for development)
cmake -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug

# Release build
cmake -B cmake-build-release -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-build-release
```

### Cross-compilation for SC1000/SC500 hardware
The device uses uClibc (not glibc), requiring the buildroot toolchain. Use Docker:
```bash
cd ../docker
./build-docker.sh          # One-time: build Docker image
./run-docker.sh            # Enter build environment
./sc1000/docker/build-sc1000.sh  # Build inside container
```

Output: `software/build-buildroot/sc1000`
Deploy by copying `sc1000` to USB stick root, or use `./deploy.sh` for automated deployment.

See `../docker/README.md` for full details.

## Dependencies

- ALSA libraries (libasound)
- libm
- `deps/json.hpp` (header-only JSON library, bundled)

## Architecture

### Thread Model
Three main threads with distinct responsibilities:
1. **Realtime Thread** (`src/thread/realtime.c`): Low-latency audio processing, ALSA callbacks
2. **RIG Thread** (`src/thread/rig.c`): Track import/export, file I/O, main event loop
3. **SC Input Thread** (`src/core/sc_input.cpp`): Hardware input handling (I2C, GPIO, MIDI)

### Core Components

```
src/
├── main.c                    # Entry point
├── core/
│   ├── sc1000.cpp           # Central engine, manages two decks
│   ├── sc_input.cpp         # Input processing (I2C rotary, GPIO buttons, MIDI)
│   ├── sc_settings.cpp      # JSON configuration loader
│   └── global.cpp           # Global state (settings, engine refs)
├── control/
│   └── actions.cpp          # Button/MIDI action handlers
├── engine/
│   └── audio_engine.cpp     # DSP: mixing, pitch shifting, dithering
├── platform/
│   ├── alsa.cpp             # ALSA device driver (48kHz, 16-bit PCM)
│   ├── gpio.cpp             # GPIO button inputs
│   ├── i2c.cpp              # I2C bus communication
│   ├── midi.cpp             # MIDI controller support
│   ├── encoder.cpp          # AS5601 rotary encoder
│   └── pic.cpp              # PIC18 microcontroller communication
├── player/
│   ├── player.c             # Playback control (pitch, volume, seeking)
│   ├── deck.c               # Deck interface, file browser, cue points
│   ├── track.c              # Audio track data (ref-counted, 64x2MB blocks)
│   └── playlist.c           # File/folder navigation
├── input/
│   └── controller.c         # Generic controller interface
└── thread/
    ├── realtime.c           # RT thread management, POSIX priority
    ├── rig.c                # File handling thread
    └── spin.h               # Lock-free spinlock for RT sync
```

### Two-Deck System
- **Scratch Deck**: Interactive DJ scratching with platter simulation
- **Beat Deck**: Background beat playback ("just_play" mode)

### Configuration
JSON-based settings in `sc_settings.json`. Key parameters include audio period size, fader options (VCA, cut, hamster), platter speed/reverse/brake, and MIDI mappings.

## Hardware Interface

- **I2C**: Rotary sensor/PIC processor for platter position
- **GPIO**: Button inputs on ports 2 and 6
- **MIDI**: Up to 8 mapped controller devices
- **USB Storage**: Samples loaded from `/media/sda/samples`

## C++ Namespaces

The C++ code uses the `sc` namespace (not `sc1000` - that's a struct name):
- `sc::audio` - Audio engine, DSP, mixing (`src/engine/audio_engine.cpp`)
- `sc::config` - JSON configuration loading (`src/core/sc_settings.cpp`)
- `sc::input` - Input thread internals (`src/core/sc_input.cpp`)
- `sc::platform` - Hardware abstraction (`src/platform/*.cpp`)
- `sc::control` - Action handlers (`src/control/actions.cpp`)

Extern "C" functions remain at global scope for C interoperability.

## Key Patterns

- Real-time code uses spinlocks (`src/thread/spin.h`) instead of mutexes
- Tracks are reference-counted and loaded asynchronously via external importer (`/root/sc1000-import`)
- Audio processing uses NEON vectorization on ARM (enabled via `-mfpu=neon`)
- C files use `gnu11`, C++ files use C++17 (CMake) or `gnu++11` (Makefile)
- Original xwax code is in `original_version/` for reference

## Refactoring Fixes (2026-01-04)

Issues fixed during C++ refactoring:

### 1. JSON Enum Parsing Failure
- **Symptom**: All 77 GPIO + 24 MIDI mappings failed with "type must be number, but is string"
- **Root cause**: `NLOHMANN_JSON_SERIALIZE_ENUM` macros were inside `sc::config` namespace, but enums (`EventType`, `MIDIStatusType`, `ActionType`) are defined in global namespace in `sc_input.h`. ADL couldn't find the serialization functions.
- **Fix**: Moved the three `NLOHMANN_JSON_SERIALIZE_ENUM` macro invocations to global namespace in `src/core/sc_settings.cpp` (before the `namespace sc {` declaration)

### 2. Audio Distortion (Vector Clamp Bug)
- **Symptom**: Audio output was heavily distorted, sounded like extreme DSP overload
- **Root cause**: `clamp_vector_2()` used `__builtin_shuffle` incorrectly - passed comparison bitmasks (-1/0) as shuffle indices instead of actual indices (0-3)
- **Fix**: Replaced with simple scalar `clamp_scalar()` function in `src/engine/audio_engine.cpp`

### 3. MMAP Buffer Pointer Arithmetic
- **Symptom**: ~80Hz tone mixed with audio output
- **Root cause**: `buffer()` function cast to `signed short*` before adding offset, causing offset to be multiplied by 2 (sizeof short)
- **Fix**: Calculate byte offset first, then cast: `reinterpret_cast<signed short*>(static_cast<char*>(area->addr) + bitofs / 8)`
- **Location**: `src/platform/alsa.cpp`

### 4. MMAP Playback State Machine
- **Symptom**: Silence with MMAP mode
- **Root cause**: PCM wasn't being started after first write, and avail wasn't checked before mmap_begin
- **Fix**: Added `snd_pcm_start()` after first commit, `snd_pcm_avail_update()` check before mmap_begin, and proper state tracking with `alsa->started` flag

## Future Improvements

Potential enhancements to implement:

### Audio/DSP
- [ ] **Multi-channel CV output** - Add support for CV outputs alongside stereo audio
- [ ] **Proper NEON SIMD** - Implement ARM NEON intrinsics for audio engine (replace auto-vectorization)
- [ ] **Effects chain** - Add filter/delay/reverb effects
- [x] **Sinc interpolation** - 16-tap anti-aliased resampling (see below)

### Hardware Support
- [ ] **USB audio class compliance** - Improved USB audio device support
- [ ] **Additional encoder types** - Support for different rotary encoders
- [ ] **Display output** - Track info display support

### Features
- [ ] **More cue points** - Expand beyond current cue point limit
- [ ] **Loop functionality** - Auto-loop and loop roll features
- [ ] **BPM detection** - Automatic tempo detection
- [ ] **Recording improvements** - Better recording quality/format options

### Code Quality
- [ ] **Remove remaining C-style casts** - Convert to C++ casts throughout
- [ ] **Fix compiler warnings** - Address -Wconversion, -Wold-style-cast warnings
- [ ] **Unit tests** - Add test coverage for audio engine and settings parsing
- [ ] **Documentation** - API documentation for key classes

## Memory Looper / Live Sampler (2026-01-04)

Records audio input into an in-memory buffer that can be immediately scratched/looped. The loop is stored in memory using the existing track block system (not saved to file).

### Button Controls
| Action | Scratch Deck | Beats Deck |
|--------|--------------|------------|
| **Start/Stop Recording** | SHIFT + PLAY | SHIFT + PLAY |
| **Erase Loop** | SHIFT + PLAY (hold 3 sec) | SHIFT + PLAY (hold 3 sec) |

### Recording Workflow
1. **First RECORD press**: Starts capturing audio from input
2. **RECORD press again**: Stops recording, defines loop length, loads onto deck
3. **Subsequent RECORD presses**: Punch-in mode - overwrites circularly from current position
4. **Long-hold RECORD (~1 sec)**: Erases the loop, next RECORD starts fresh

### Configuration (`sc_settings.json`)
```json
{
  "sc1000": {
    "hold_time": 150,            // ~1 second for long-hold gesture
    "loop_max_seconds": 60       // Maximum recording duration
  },
  "audio_devices": [{
    "input_channels": 2,         // Number of capture channels (0 = disabled)
    "input_left": 0,             // Which capture channel is left
    "input_right": 1             // Which capture channel is right
  }]
}
```

### New Action Types
- `record` - Toggle loop recording (BUTTON_RELEASED_SHIFTED)
- `loop_erase` - Erase loop for fresh recording (BUTTON_HOLDING_SHIFTED)
- `loop_recall` - Recall last recorded loop after loading another track

### Button Event Types

The system supports these button event types for GPIO mappings:

| Event Type | When it fires |
|------------|---------------|
| `button_pressed` | On button press (unshifted) |
| `button_pressed_shifted` | On button press while shift held |
| `button_released` | On button release (unshifted) |
| `button_released_shifted` | On button release while shift was held at press time |
| `button_holding` | After hold_time reached (unshifted) |
| `button_holding_shifted` | After hold_time reached while shift was held at press time |

**Important**: The shifted state is latched when the button is first pressed (`shifted_at_press`). This allows:
- `button_released_shifted` to fire correctly even if shift is released before the button
- `button_holding_shifted` to fire correctly for long-hold gestures

### Record/Erase Workflow

To prevent state conflicts between RECORD (toggle) and LOOPERASE (hold), the record action uses `button_released_shifted` instead of `button_pressed_shifted`:

1. **Shift+press** → Nothing happens yet (shifted state is latched)
2. **Shift+release quickly** → RECORD fires (toggles recording)
3. **Shift+hold ~1 sec** → LOOPERASE fires (RECORD does NOT fire)

This ensures you can't accidentally toggle recording when attempting to erase.

### Files Added/Modified
| File | Change |
|------|--------|
| `src/engine/loop_buffer.h` | **New** - Loop buffer API |
| `src/engine/loop_buffer.cpp` | **New** - Loop buffer implementation |
| `src/platform/alsa.cpp` | Opens capture PCM, feeds loop buffer |
| `src/platform/alsa.h` | Recording control functions |
| `src/player/deck.h` | Added loop_track field |
| `src/player/deck.cpp` | Implemented loop recall |
| `src/player/track.h` | Added track_acquire_for_recording() |
| `src/player/track.cpp` | Recording support functions |
| `src/core/sc1000.cpp` | Recording workflow integration |
| `src/core/sc_settings.h` | Loop config settings |
| `src/core/sc_settings.cpp` | New action type serialization |
| `src/control/actions.cpp` | LOOPERASE/LOOPRECALL handlers |
| `src/core/sc_input.h` | LOOPERASE/LOOPRECALL action types, BUTTON_RELEASED_SHIFTED event |
| `src/core/sc_input.cpp` | Shifted release/hold detection with latched state |

### Technical Details
- **Sample Rate**: 48kHz (device native, no resampling)
- **Storage**: Memory buffer using track block system (64 blocks × ~2MB max)
- **Memory Budget**: 60 sec @ 48kHz × 2ch × 2 bytes = ~11.5MB per deck
- **Graceful Degradation**: Devices without audio inputs play error beep

### Loop as Track 0 (2026-01-04)

The loop recording is integrated into the playlist navigation system. The loop becomes "position 0" in the track list, with real files starting at position 1.

#### Navigation Behavior

| State | Behavior                                                      |
|-------|---------------------------------------------------------------|
| Startup (no loop) | First file plays let(position 1), can't navigate to position 0 |
| After recording | Playback jumps to position 0 (loop), prev/next works normally |
| prev from file 0 | Goes to loop (position 0) if loop exists                      |
| next from loop | Goes to first file (position 1)                               |
| Folder change | Stays at loop position if currently there                     |
| Loop erase | Jumps to first file (position 1)                              |

Both beat and scratch decks have this behavior.

#### Implementation Details
- `current_file_idx` is `int` type: -1 = loop track, 0+ = file tracks
- `deck_no` field identifies which deck (0=beat, 1=scratch)
- Navigation functions check `alsa_has_loop()` before allowing prev to loop
- `goto_loop()` helper resets position and updates `use_loop` flag
- Recording completion automatically navigates to loop position

#### Files Modified
| File | Change |
|------|--------|
| `src/player/deck.h` | `current_file_idx` changed to `int`, added `deck_no`, `is_at_loop()`, `goto_loop()` |
| `src/player/deck.cpp` | Loop-aware navigation in `next_file()`, `prev_file()`, `next_folder()`, `prev_folder()` |
| `src/core/sc1000.cpp` | Set `deck_no` on init, recording completion sets `current_file_idx = -1` |
| `src/control/actions.cpp` | Navigation calls pass `engine`, LOOPERASE loads first file |
| `src/core/sc_input.cpp` | Navigation calls pass `sc1000_engine` |

---

## Future Refactoring

### Code Style Cleanup

#### a) ~~Remove C-style bool comparisons~~ (DONE 2026-01-05)
Fixed `bool` variable assignments and comparisons in `audio_engine.cpp` and `sc_input.cpp`:
- `pl->just_play == 1` → `pl->just_play`
- `pl->cap_touch == 0` → `!pl->cap_touch`
- `shift_latched = 1` → `shift_latched = true`
- `first_time = 0` → `first_time = false`
- `.cap_touch = 1/0` → `.cap_touch = true/false`
- `.just_play = 1` → `.just_play = true`

Note: Some `int` settings still use 0/1 (e.g., `disable_volume_adc`, `cut_beats`). These should be converted to `bool` in a future pass.

#### b) ~~Review C interface wrappers~~ (DONE 2026-01-05)
Removed all C wrapper functions from `deck` and `player` structs. All callers now use C++ member functions directly:

**Removed from `deck.cpp`/`deck.h`:**
- `deck_init`, `deck_clear`, `deck_is_locked`, `deck_recue`, `deck_clone`
- `deck_cue`, `deck_unset_cue`, `deck_punch_in`, `deck_punch_out`
- `deck_load_folder`, `deck_next_file`, `deck_prev_file`
- `deck_next_folder`, `deck_prev_folder`, `deck_random_file`
- `deck_goto_loop`, `deck_record`, `deck_recall_loop`, `deck_has_loop`

**Removed from `player.cpp`/`player.h`:**
- `player_init`, `player_clear`, `player_set_track`, `player_clone`
- `player_get_elapsed`, `player_is_active`, `player_seek_to`, `player_recue`

**Note:** `cues_*` functions remain as they're used directly (not wrapped around a class).

### Application Configuration

#### c) ~~Add root directory command-line parameter~~ (DONE 2026-01-04)
Implemented `--root` CLI argument for configurable sample paths:
```bash
./sc1000 --root /media/sda          # Hardware (default)
./sc1000 --root ~/Music/samples     # Desktop development
```
- Modified `main.cpp` to parse `--root` argument
- Root path stored in `sc_settings.root_path`
- `sc1000_load_sample_folders()` uses root path for samples/beats directories

### MIDI Implementation

#### d) Verify MIDI functionality and device configs
Current MIDI implementation needs testing and potentially device-specific configurations.

**TODO:**
- Test with actual MIDI controllers (e.g., Bitwig Connect interface)
- Verify note on/off, CC, and pitch bend handling
- Consider device profiles for different controllers (mapping presets)
- Check if current generic mapping approach works across devices

**Files:** `src/input/midi_controller.cpp`, `sc_settings.json` MIDI mappings

### Audio Quality

#### e) ~~Improve resampling/interpolation~~ (DONE 2026-01-06)

Implemented 16-tap windowed sinc interpolation with anti-aliasing for high-pitch playback.

**Command-line options:**
```bash
./sc1000 --sinc    # Default - 16-tap anti-aliased interpolation
./sc1000 --cubic   # 4-tap Catmull-Rom, faster but no anti-aliasing
```

**Anti-aliasing test results:**
| Interpolation | Aliased Energy | Notes |
|---------------|----------------|-------|
| Cubic (4-tap) | 28.5% | Fast, but aliases fold back into audible range |
| Sinc (16-tap) | 4.8% | **5.9x improvement**, proper low-pass filtering |

**Implementation details:**
- 16 taps, 32 phases, 3 bandwidth variants (6KB lookup tables)
- Bandwidth selected based on pitch ratio:
  - pitch ≤ 1.0: full bandwidth (no filtering needed)
  - pitch 1.0-2.0: 0.5 cutoff
  - pitch > 2.0: 0.25 cutoff (aggressive anti-aliasing for fast scratching)
- Phase interpolation between table entries for smooth sub-sample accuracy
- Dual-deck processing with independent pitch/bandwidth per deck

**Files added/modified:**
| File | Change |
|------|--------|
| `src/dsp/sinc_table.h` | **New** - Pre-computed sinc tables (generated) |
| `src/dsp/sinc_interpolate.h` | **New** - Sinc interpolation with bandwidth selection |
| `src/dsp/audio_interpolation.h` | **New** - Unified interface for cubic/sinc |
| `src/engine/audio_engine.h` | Added `interpolation_mode_t` enum and API |
| `src/engine/audio_engine.cpp` | Added `process_add_players_sinc()` with 16-sample collection |
| `src/main.cpp` | Added `--cubic`/`--sinc` CLI arguments |
| `tools/generate_sinc_table.py` | **New** - Python script to regenerate sinc tables |
| `tools/resampler_test.cpp` | **New** - Test harness for A/B comparison |
| `tools/analyze_resampler.py` | **New** - Python spectral analysis with STFT plots |

**Testing on device:**
- Compare DSP load between modes via MIDI interface
- Listen for aliasing artifacts during fast scratching (pitch > 2x)
- Sinc should sound cleaner on high-pitched playback, cubic may sound "crunchy"

**Future optimization:**
- ARM NEON SIMD version for sinc convolution (currently scalar)
- Could reduce taps to 12 if CPU budget tight

#### g) Instant MIDI Pitch Response (2026-01-10)

Refactored pitch path to separate external pitch control (MIDI notes, pitch bend, semitone buttons) from platter/slipmat behavior.

**Problem:**
All pitch sources were mixed through `motor_speed` and then smoothed with an IIR filter (0.1/0.9), causing unwanted glide/portamento on MIDI note changes (~120ms to reach target pitch).

**Solution:**
Detect external speed changes and skip smoothing for instant response:

```cpp
// In setup_player() - audio_engine.cpp:209-254
double external_speed = pl->note_pitch * pl->fader_pitch * pl->bend_pitch;
bool external_changed = std::fabs(external_speed - pl->last_external_speed) > 0.01;

if (external_changed) {
    // Instant response: snap pitch to new external speed
    target_pitch = pl->motor_speed;
    pl->pitch = pl->motor_speed;
}
```

**Behavior by mode:**

| Mode | Pitch Source | Smoothing |
|------|--------------|-----------|
| Platter released | External (MIDI) | Instant on change, slipmat otherwise |
| Platter touched | Position error | IIR filter (0.1/0.9) for smooth scratch feel |
| Beat deck | External (MIDI) | Instant on change, slipmat otherwise |

**Files modified:**
| File | Change |
|------|--------|
| `src/player/player.h` | Added `last_external_speed` field |
| `src/player/player.cpp` | Initialize `last_external_speed = 1.0` |
| `src/engine/audio_engine.cpp` | Refactored `setup_player()` for separate pitch paths |

**Future enhancement:**
Optional configurable LPF for MIDI pitch glide (for synth-style portamento effect).

### Hardware Investigation

#### f) Gate B and ADC values
Gate B CV output doesn't work. Need to understand the ADC inputs.

**Current state:**
- 4 ADC values are read from PIC: `ADCs[0..3]`
- ADCs[0], ADCs[1] appear to be crossfader-related
- ADCs[2], ADCs[3] appear to be volume pots
- Gate B output logic exists but doesn't trigger correctly

**Investigation needed:**
- Find Innofader Mini OEM specification (if available)
- Trace what each ADC actually measures
- Check PIC firmware for ADC channel assignments
- Verify Gate B hardware connection and logic levels

**Files:** `src/core/sc_input.cpp` (ADC processing), `src/platform/alsa.cpp` (CV output)
