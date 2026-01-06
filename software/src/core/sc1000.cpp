#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "../platform/alsa.h"
#include "../util/debug.h"
#include "../util/log.h"

#include "../player/deck.h"
#include "../player/track.h"

#include "../engine/audio_engine.h"
#include "sc1000.h"
#include "sc_settings.h"

void sc1000_setup(sc1000* engine, struct rt* rt, const char* root_path)
{
    LOG_INFO("SC1000 engine init (root: %s)", root_path);

    engine->settings = std::make_unique<sc_settings>();
    engine->mappings.clear();

    // Store root path in settings for use by other components
    engine->settings->root_path = root_path;

    sc_settings_load_user_configuration(engine->settings.get(), engine->mappings);

    // Verify root_path wasn't corrupted by settings loading
    LOG_DEBUG("After settings load, root_path = '%s'", engine->settings->root_path.c_str());

    // Print loaded mappings for debugging
    sc_settings_print_gpio_mappings(engine->mappings);

    // Create two decks, both pointed at the same audio device
    engine->scratch_deck.init(engine->settings.get());
    engine->beat_deck.init(engine->settings.get());

    // Set deck numbers for loop navigation
    engine->beat_deck.deck_no = 0;
    engine->scratch_deck.deck_no = 1;

    // Tell deck0 to just play without considering inputs
    engine->beat_deck.player.just_play = true;

    // Initialize audio hardware (creates AudioHardware instance)
    engine->audio = alsa_create(engine, engine->settings.get());
    rt_set_sc1000(rt, engine);

    alsa_clear_config_cache();
}

void sc1000_load_sample_folders(struct sc1000* engine)
{
    const std::string& root = engine->settings->root_path;
    std::string samples_path = root + "/samples";
    std::string beats_path = root + "/beats";

    LOG_DEBUG("sc1000_load_sample_folders called, root_path = '%s'", root.c_str());
    LOG_DEBUG("samples_path = '%s', beats_path = '%s'", samples_path.c_str(), beats_path.c_str());

    // Check for samples folder (only do USB mount dance for default /media/sda)
    if (root == "/media/sda" && access(samples_path.c_str(), F_OK) == -1) {
        // Not there, so presumably the boot script didn't manage to mount the drive
        // Maybe it hasn't initialized yet, or at least wasn't at boot time
        // We have to do it ourselves

        // Timeout after 12 sec, in which case emergency samples will be loaded
        for (int uscnt = 0; uscnt < 12; uscnt++) {
            LOG_INFO("Waiting for USB stick...");
            // Wait for /dev/sda1 to show up and then mount it
            if (access("/dev/sda1", F_OK) != -1) {
                LOG_INFO("Found USB stick, mounting!");
                (void)system("/bin/mount /dev/sda1 /media/sda");
                break;
            } else {
                // If not here yet, wait a second then check again
                sleep(1);
            }
        }
    }

    LOG_INFO("Loading beats from: %s", beats_path.c_str());
    LOG_INFO("Loading samples from: %s", samples_path.c_str());

    engine->beat_deck.load_folder(beats_path.c_str());
    engine->scratch_deck.load_folder(samples_path.c_str());

    if (!engine->scratch_deck.files_present) {
        // Load the default sentence if no sample files found on usb stick
        engine->scratch_deck.player.set_track(
                         track_acquire_by_import(engine->scratch_deck.importer.c_str(), "/var/scratchsentence.mp3"));
        LOG_DEBUG("Set default track ok");
        cues_load_from_file(&engine->scratch_deck.cues, engine->scratch_deck.player.track->path);
        LOG_DEBUG("Set cues ok");
        // Set the time back a bit so the sample doesn't start too soon
        engine->scratch_deck.player.target_position = -4.0;
        engine->scratch_deck.player.position = -4.0;
    }
}

void sc1000_clear(sc1000* engine)
{
    engine->beat_deck.clear();
    engine->scratch_deck.clear();

    // Audio hardware cleaned up automatically via unique_ptr
    engine->audio.reset();

    // Settings cleaned up automatically via unique_ptr
    engine->settings.reset();
}

void sc1000_audio_start(sc1000* engine)
{
    if (engine->audio) {
        engine->audio->start();
    }
}

void sc1000_audio_stop(sc1000* engine)
{
    if (engine->audio) {
        engine->audio->stop();
    }
}

ssize_t sc1000_audio_pollfds(sc1000* engine, struct pollfd* pe, size_t z)
{
    if (engine->audio) {
        return engine->audio->pollfds(pe, z);
    }
    return 0;
}

void sc1000_audio_handle(sc1000* engine)
{
    if (engine->fault || !engine->audio) {
        return;
    }

    if (engine->audio->handle() != 0) {
        engine->fault = true;
        LOG_ERROR("Error handling audio device; disabling it");
    }
}

// Helper to handle recording for a single deck
static void handle_deck_recording(sc1000* engine, deck* dk, int deck_no)
{
    player* pl = &dk->player;

    if (!engine->audio) return;

    // Start recording if requested
    if (pl->recording_started && !pl->recording) {
        if (engine->audio->start_recording(deck_no, pl->position)) {
            pl->recording = true;
            pl->playing_beep = BEEP_RECORDINGSTART;
        } else {
            // Failed to start recording
            pl->recording_started = false;
            pl->playing_beep = BEEP_RECORDINGERROR;
        }
    }

    // Stop recording if requested
    if (!pl->recording_started && pl->recording) {
        // Check if this was a first recording (will define loop) or punch-in
        bool was_first_recording = !engine->audio->has_loop(deck_no);

        // Stop recording
        engine->audio->stop_recording(deck_no);

        // Navigate to loop position (position 0 in track list)
        dk->current_file_idx = -1;

        // Switch player to use loop track (RT-safe: just a bool flag)
        // Audio engine will read from loop buffer instead of player->track
        pl->use_loop = true;  // Always switch to loop after recording
        LOG_DEBUG("Recording stopped on deck %d, set use_loop=true, current_file_idx=-1", deck_no);
        if (was_first_recording) {
            pl->position = 0;
            pl->target_position = 0;
            pl->offset = 0;
        }

        pl->recording = false;
        pl->playing_beep = BEEP_RECORDINGSTOP;
        LOG_DEBUG("Recording stopped on deck %d, navigated to loop (position 0)", deck_no);
    }
}

void sc1000_handle_deck_recording(sc1000* engine)
{
    // Handle loop buffer recording for both decks (memory-based, for immediate scratching)
    handle_deck_recording(engine, &engine->beat_deck, 0);     // Beat deck = 0
    handle_deck_recording(engine, &engine->scratch_deck, 1);  // Scratch deck = 1
}
