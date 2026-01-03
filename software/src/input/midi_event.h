/*
 * MIDI Event structure for inter-thread communication
 *
 * C code only sees the C API functions.
 * C++ code gets the full implementation.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// C API for the realtime thread to push events
// Returns 1 on success, 0 if queue full
int midi_event_queue_push(const unsigned char* midi_bytes, int shifted);

// C API for the input thread to pop events
// Returns 1 if event was available, 0 if queue empty
int midi_event_queue_pop(unsigned char* midi_bytes, int* shifted);

#ifdef __cplusplus
}

// C++ only: full implementation details
#include "../util/spsc_queue.h"

namespace sc {

struct MidiEvent {
    unsigned char bytes[3];
    bool shifted;  // Shift state at time of event

    MidiEvent() : bytes{0, 0, 0}, shifted(false) {}

    MidiEvent(const unsigned char* buf, bool shift_state)
        : bytes{buf[0], buf[1], buf[2]}, shifted(shift_state) {}
};

// Queue size: 64 events should be more than enough
// (at 1000Hz polling, this is 64ms of buffer)
// Using moodycamel::ReaderWriterQueue under the hood
using MidiEventQueue = moodycamel::ReaderWriterQueue<MidiEvent>;

} // namespace sc

#endif // __cplusplus
