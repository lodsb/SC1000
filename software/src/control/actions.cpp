// Action dispatch for SC1000 control events

#include "actions.h"

#include <cmath>
#include <cstdio>

#include "../player/cues.h"
#include "../player/deck.h"
#include "../player/playlist.h"
#include "../player/track.h"
#include "../core/sc1000.h"
#include "../core/sc_settings.h"
#include "../platform/alsa.h"
#include "../util/log.h"

namespace sc {
namespace control {

// Static member definitions for ActionState
bool ActionState::shifted = false;
int ActionState::pitch_mode = 0;

void perform_action_for_deck(deck* deck, mapping* map,
                             const unsigned char midi_buffer[3],
                             sc1000* engine, sc_settings* settings)
{
    if (map->action_type == CUE) {
        unsigned int cuenum = 0;
        if (map->type == MIDI)
            cuenum = map->midi_command_bytes[1];
        else
            cuenum = (map->gpio_port * 32) + map->pin + 128;
        deck->cue(cuenum);
    }
    else if (map->action_type == DELETECUE) {
        unsigned int cuenum = 0;
        if (map->type == MIDI)
            cuenum = map->midi_command_bytes[1];
        else
            cuenum = (map->gpio_port * 32) + map->pin + 128;
        deck->unset_cue(cuenum);
    }
    else if (map->action_type == NOTE) {
        // Check for note-off: status 0x80 or note-on with velocity 0
        bool is_note_off = (midi_buffer[0] & 0xF0) == 0x80 ||
                           ((midi_buffer[0] & 0xF0) == 0x90 && midi_buffer[2] == 0);
        if (is_note_off) {
            // Reset pitch to normal on note-off
            deck->player.note_pitch = 1.0;
            LOG_DEBUG("NOTE action: note-off, pitch reset to 1.0");
        } else {
            // Equal temperament: 2^(1/12) per semitone, 0x3C = middle C
            double new_pitch = pow(pow(2.0, 1.0 / 12.0), midi_buffer[1] - 0x3C);
            deck->player.note_pitch = new_pitch;
            LOG_INFO("NOTE action: note=%d -> pitch=%.3f", midi_buffer[1], new_pitch);
        }
    }
    else if (map->action_type == STARTSTOP) {
        deck->player.stopped = !deck->player.stopped;
    }
    else if (map->action_type == SHIFTON) {
        LOG_DEBUG("SHIFTON action fired, shifted: %d -> true", shifted);
        shifted = true;
    }
    else if (map->action_type == SHIFTOFF) {
        LOG_DEBUG("SHIFTOFF action fired, shifted: %d -> false", shifted);
        shifted = false;
    }
    else if (map->action_type == NEXTFILE) {
        deck->next_file(engine, settings);
    }
    else if (map->action_type == PREVFILE) {
        deck->prev_file(engine, settings);
    }
    else if (map->action_type == RANDOMFILE) {
        deck->random_file(engine, settings);
    }
    else if (map->action_type == NEXTFOLDER) {
        deck->next_folder(engine, settings);
    }
    else if (map->action_type == PREVFOLDER) {
        deck->prev_folder(engine, settings);
    }
    else if (map->action_type == VOLUME) {
        deck->player.set_volume = static_cast<double>(midi_buffer[2]) / 128.0;
    }
    else if (map->action_type == PITCH) {
        if (map->type == MIDI) {
            double pitch = 1.0;
            // Check if parameter specifies semitone range (0 = use global pitch_range percentage)
            int semitone_range = map->parameter;

            // Pitch bend message: use 14-bit accuracy
            if ((midi_buffer[0] & 0xF0) == 0xE0) {
                unsigned int pval = (static_cast<unsigned int>(midi_buffer[2]) << 7) |
                                    static_cast<unsigned int>(midi_buffer[1]);
                // Normalized position: -1.0 to +1.0
                double normalized = (static_cast<double>(pval) - 8192.0) / 8192.0;

                if (semitone_range > 0) {
                    // Semitone mode: pitch = 2^(semitones/12)
                    double semitones = normalized * static_cast<double>(semitone_range);
                    pitch = std::pow(2.0, semitones / 12.0);
                    LOG_DEBUG("PITCH action: 14-bit pval=%u norm=%.3f semi=%.1f pitch=%.4f deck=%d",
                             pval, normalized, semitones, pitch, map->deck_no);
                } else {
                    // Legacy percentage mode
                    pitch = (normalized * (static_cast<double>(settings->pitch_range) / 100.0)) + 1.0;
                    LOG_DEBUG("PITCH action: 14-bit pval=%u pitch=%.4f range=%d%% deck=%d",
                             pval, pitch, settings->pitch_range, map->deck_no);
                }
            }
            // Otherwise 7-bit CC
            else {
                double normalized = (static_cast<double>(midi_buffer[2]) - 64.0) / 64.0;

                if (semitone_range > 0) {
                    double semitones = normalized * static_cast<double>(semitone_range);
                    pitch = std::pow(2.0, semitones / 12.0);
                    LOG_DEBUG("PITCH action: 7-bit val=%d semi=%.1f pitch=%.4f deck=%d",
                             midi_buffer[2], semitones, pitch, map->deck_no);
                } else {
                    pitch = (normalized * (static_cast<double>(settings->pitch_range) / 100.0)) + 1.0;
                    LOG_DEBUG("PITCH action: 7-bit val=%d pitch=%.4f range=%d%% deck=%d",
                             midi_buffer[2], pitch, settings->pitch_range, map->deck_no);
                }
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

void dispatch_event(mapping* map, unsigned char midi_buffer[3],
                    sc1000* engine, sc_settings* settings)
{
    if (map == nullptr) return;

    // Determine target deck from mapping (0=beat, 1=scratch)
    deck* target = (map->deck_no == 0)
        ? &engine->beat_deck
        : &engine->scratch_deck;

    if (map->action_type == RECORD) {
        // Toggle loop recording for the target deck
        target->record();
    }
    else if (map->action_type == LOOPERASE) {
        // Long-hold RECORD erases the loop and navigates to first file
        LOG_DEBUG("LOOPERASE triggered on deck %d, was use_loop=%d, was current_file_idx=%d",
                  map->deck_no, target->player.use_loop, target->current_file_idx);
        if (engine->audio) engine->audio->reset_loop(map->deck_no);
        target->player.use_loop = false;

        // Navigate to first file (position 1, index 0)
        target->current_file_idx = 0;
        LOG_DEBUG("LOOPERASE set use_loop=false, current_file_idx=0");
        if (target->files_present) {
            sc_file* file = target->playlist->get_file(target->current_folder_idx, 0);
            if (file != nullptr) {
                // Load the first file in current folder
                target->player.set_track(track_acquire_by_import(target->importer.c_str(), file->full_path.c_str()));
                target->player.position = 0;
                target->player.target_position = 0;
                target->player.offset = 0;
                cues_load_from_file(&target->cues, target->player.track->path);
            }
        }

        target->player.playing_beep = BEEP_RECORDINGERROR;  // Use error beep as "erased" feedback
        LOG_DEBUG("Loop erased on deck %d, navigated to file 0", map->deck_no);
    }
    else if (map->action_type == LOOPRECALL) {
        // Recall the last recorded loop
        LOG_DEBUG("Loop recall triggered on deck %d", map->deck_no);
        if (target->recall_loop(settings)) {
            target->player.playing_beep = BEEP_RECORDINGSTART;  // Success feedback
        } else {
            target->player.playing_beep = BEEP_RECORDINGERROR;  // No loop to recall
        }
    }
    else {
        perform_action_for_deck(target, map, midi_buffer, engine, settings);
    }
}

} // namespace control
} // namespace sc
