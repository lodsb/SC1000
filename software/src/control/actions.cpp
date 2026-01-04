// Action dispatch for SC1000 control events

#include "actions.h"

#include <cmath>

#include "../player/deck.h"
#include "../core/sc1000.h"
#include "../core/sc_settings.h"
#include "../platform/alsa.h"
#include "../util/log.h"

namespace sc {
namespace control {

// Global shift state
bool shifted = false;

// Pitch mode: 0=off, 1=beat deck, 2=scratch deck
int pitch_mode = 0;

void perform_action_for_deck(struct deck* deck, struct mapping* map,
                             const unsigned char midi_buffer[3],
                             struct sc_settings* settings)
{
    if (map->action_type == CUE) {
        unsigned int cuenum = 0;
        if (map->type == MIDI)
            cuenum = map->midi_command_bytes[1];
        else
            cuenum = (map->gpio_port * 32) + map->pin + 128;
        deck_cue(deck, cuenum);
    }
    else if (map->action_type == DELETECUE) {
        unsigned int cuenum = 0;
        if (map->type == MIDI)
            cuenum = map->midi_command_bytes[1];
        else
            cuenum = (map->gpio_port * 32) + map->pin + 128;
        deck_unset_cue(deck, cuenum);
    }
    else if (map->action_type == NOTE) {
        // Equal temperament: 2^(1/12) per semitone, 0x3C = middle C
        deck->player.note_pitch = pow(pow(2.0, 1.0 / 12.0), map->parameter - 0x3C);
    }
    else if (map->action_type == STARTSTOP) {
        deck->player.stopped = !deck->player.stopped;
    }
    else if (map->action_type == SHIFTON) {
        LOG_DEBUG("Shift on");
        shifted = true;
    }
    else if (map->action_type == SHIFTOFF) {
        LOG_DEBUG("Shift off");
        shifted = false;
    }
    else if (map->action_type == NEXTFILE) {
        deck_next_file(deck, settings);
    }
    else if (map->action_type == PREVFILE) {
        deck_prev_file(deck, settings);
    }
    else if (map->action_type == RANDOMFILE) {
        deck_random_file(deck, settings);
    }
    else if (map->action_type == NEXTFOLDER) {
        deck_next_folder(deck, settings);
    }
    else if (map->action_type == PREVFOLDER) {
        deck_prev_folder(deck, settings);
    }
    else if (map->action_type == VOLUME) {
        deck->player.set_volume = static_cast<double>(midi_buffer[2]) / 128.0;
    }
    else if (map->action_type == PITCH) {
        if (map->type == MIDI) {
            double pitch = 0.0;
            // Pitch bend message: use 14-bit accuracy
            if ((midi_buffer[0] & 0xF0) == 0xE0) {
                unsigned int pval = (static_cast<unsigned int>(midi_buffer[2]) << 7) |
                                    static_cast<unsigned int>(midi_buffer[1]);
                pitch = ((static_cast<double>(pval) - 8192.0) *
                        (static_cast<double>(settings->pitch_range) / 819200.0)) + 1.0;
            }
            // Otherwise 7-bit
            else {
                pitch = ((static_cast<double>(midi_buffer[2]) - 64.0) *
                        (static_cast<double>(settings->pitch_range) / 6400.0)) + 1.0;
            }
            deck->player.fader_pitch = pitch;
        }
    }
    else if (map->action_type == JOGPIT) {
        pitch_mode = map->deck_no + 1;
        LOG_DEBUG("Set Pitch Mode %d", pitch_mode);
    }
    else if (map->action_type == JOGPSTOP) {
        pitch_mode = 0;
    }
    else if (map->action_type == SC500) {
        LOG_DEBUG("SC500 detected");
    }
    else if (map->action_type == VOLUP) {
        deck->player.set_volume += settings->volume_amount;
        if (deck->player.set_volume > 1.0)
            deck->player.set_volume = 1.0;
    }
    else if (map->action_type == VOLDOWN) {
        deck->player.set_volume -= settings->volume_amount;
        if (deck->player.set_volume < 0.0)
            deck->player.set_volume = 0.0;
    }
    else if (map->action_type == VOLUHOLD) {
        deck->player.set_volume += settings->volume_amount_held;
        if (deck->player.set_volume > 1.0)
            deck->player.set_volume = 1.0;
    }
    else if (map->action_type == VOLDHOLD) {
        deck->player.set_volume -= settings->volume_amount_held;
        if (deck->player.set_volume < 0.0)
            deck->player.set_volume = 0.0;
    }
    else if (map->action_type == JOGREVERSE) {
        LOG_DEBUG("Reversed Jog Wheel: %d", settings->jog_reverse);
        settings->jog_reverse = !settings->jog_reverse;
        LOG_DEBUG(" -> %d", settings->jog_reverse);
    }
    else if (map->action_type == BEND) {
        // Temporary pitch bend on top of other pitch values
        deck->player.bend_pitch = pow(pow(2.0, 1.0 / 12.0), map->parameter - 0x3C);
    }
}

void dispatch_event(struct mapping* map, unsigned char midi_buffer[3],
                    struct sc1000* engine, struct sc_settings* settings)
{
    if (map == nullptr) return;

    // Determine target deck from mapping (0=beat, 1=scratch)
    struct deck* target = (map->deck_no == 0)
        ? &engine->beat_deck
        : &engine->scratch_deck;

    if (map->action_type == RECORD) {
        // Toggle loop recording for the target deck
        deck_record(target);
    }
    else if (map->action_type == LOOPERASE) {
        // Long-hold RECORD (3 sec) erases the loop, allowing fresh recording
        LOG_DEBUG("Loop erase triggered on deck %d", map->deck_no);
        alsa_reset_loop(engine, map->deck_no);
        target->player.use_loop = false;  // Switch back to file track
        target->player.playing_beep = BEEP_RECORDINGERROR;  // Use error beep as "erased" feedback
    }
    else if (map->action_type == LOOPRECALL) {
        // Recall the last recorded loop
        LOG_DEBUG("Loop recall triggered on deck %d", map->deck_no);
        if (deck_recall_loop(target, settings)) {
            target->player.playing_beep = BEEP_RECORDINGSTART;  // Success feedback
        } else {
            target->player.playing_beep = BEEP_RECORDINGERROR;  // No loop to recall
        }
    }
    else {
        perform_action_for_deck(target, map, midi_buffer, settings);
    }
}

struct mapping* find_midi_mapping(struct mapping* maps,
                                  unsigned char buf[3],
                                  enum EventType edge)
{
    // Interpret zero-velocity notes as note-off commands
    if (((buf[0] & 0xF0) == 0x90) && (buf[2] == 0x00)) {
        buf[0] = 0x80 | (buf[0] & 0x0F);
    }

    struct mapping* m = maps;
    while (m != nullptr) {
        if (m->type == MIDI && m->edge_type == edge) {
            // Pitch bend messages match on first byte only
            if (((m->midi_command_bytes[0] & 0xF0) == 0xE0) &&
                m->midi_command_bytes[0] == buf[0]) {
                return m;
            }
            // Everything else matches on first two bytes
            if (m->midi_command_bytes[0] == buf[0] &&
                m->midi_command_bytes[1] == buf[1]) {
                return m;
            }
        }
        m = m->next;
    }
    return nullptr;
}

struct mapping* find_io_mapping(struct mapping* mappings,
                                unsigned char port,
                                unsigned char pin,
                                enum EventType edge)
{
    struct mapping* m = mappings;
    while (m != nullptr) {
        if (m->type == IO &&
            m->pin == pin &&
            m->edge_type == edge &&
            m->gpio_port == port) {
            return m;
        }
        m = m->next;
    }
    return nullptr;
}

} // namespace control
} // namespace sc

// C-compatible wrappers
void io_event(struct mapping* map, unsigned char midi_buffer[3],
              struct sc1000* sc1000_engine, struct sc_settings* settings)
{
    sc::control::dispatch_event(map, midi_buffer, sc1000_engine, settings);
}

struct mapping* find_midi_mapping_c(struct mapping* maps,
                                    unsigned char buf[3],
                                    enum EventType edge)
{
    return sc::control::find_midi_mapping(maps, buf, edge);
}

struct mapping* find_io_mapping_c(struct mapping* mappings,
                                  unsigned char port,
                                  unsigned char pin,
                                  enum EventType edge)
{
    return sc::control::find_io_mapping(mappings, port, pin, edge);
}
