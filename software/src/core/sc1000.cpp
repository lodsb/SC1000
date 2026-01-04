#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "../platform/alsa.h"
#include "../util/debug.h"

#include "../player/deck.h"
#include "../player/track.h"

#include "../engine/audio_engine.h"
#include "sc1000.h"
#include "sc_settings.h"

static const char* BEEPS[3] = {
    "----------",          // Start Recording
    "- - - - - - - - -",   // Stop Recording
    "--__--__--__--__--__" // Recording error
};

void sc1000_setup(struct sc1000* engine, struct rt* rt)
{
    printf("sc1000_init\n");

    auto* settings = static_cast<struct sc_settings*>(malloc(sizeof(struct sc_settings)));

    engine->settings = settings;
    engine->mappings = nullptr;

    sc_settings_load_user_configuration(engine->settings, &engine->mappings);

    // Create two decks, both pointed at the same audio device
    deck_init(&engine->scratch_deck, settings);
    deck_init(&engine->beat_deck, settings);

    // Tell deck0 to just play without considering inputs
    engine->beat_deck.player.just_play = true;

    alsa_init(engine, settings);
    rt_set_sc1000(rt, engine);

    alsa_clear_config_cache();
}

void sc1000_load_sample_folders(struct sc1000* engine)
{
    // Check for samples folder
    if (access("/media/sda/samples", F_OK) == -1) {
        // Not there, so presumably the boot script didn't manage to mount the drive
        // Maybe it hasn't initialized yet, or at least wasn't at boot time
        // We have to do it ourselves

        // Timeout after 12 sec, in which case emergency samples will be loaded
        for (int uscnt = 0; uscnt < 12; uscnt++) {
            printf("Waiting for USB stick...\n");
            // Wait for /dev/sda1 to show up and then mount it
            if (access("/dev/sda1", F_OK) != -1) {
                printf("Found USB stick, mounting!\n");
                (void)system("/bin/mount /dev/sda1 /media/sda");
                break;
            } else {
                // If not here yet, wait a second then check again
                sleep(1);
            }
        }
    }

    deck_load_folder(&engine->beat_deck, const_cast<char*>("/media/sda/beats/"));
    deck_load_folder(&engine->scratch_deck, const_cast<char*>("/media/sda/samples/"));

    if (!engine->scratch_deck.files_present) {
        // Load the default sentence if no sample files found on usb stick
        player_set_track(&engine->scratch_deck.player,
                         track_acquire_by_import(engine->scratch_deck.importer, "/var/scratchsentence.mp3"));
        printf("set track ok");
        cues_load_from_file(&engine->scratch_deck.cues, engine->scratch_deck.player.track->path);
        printf("set cues ok");
        // Set the time back a bit so the sample doesn't start too soon
        engine->scratch_deck.player.target_position = -4.0;
        engine->scratch_deck.player.position = -4.0;
    }
}

void sc1000_clear(struct sc1000* engine)
{
    deck_clear(&engine->beat_deck);
    deck_clear(&engine->scratch_deck);

    if (engine->ops->clear != nullptr) {
        engine->ops->clear(engine);
    }
}

void sc1000_audio_engine_init(struct sc1000* engine, struct sc1000_ops* ops)
{
    debug("%p", engine);
    engine->fault = false;
    engine->ops = ops;
}

/*
 * Start the device inputting and outputting audio
 */
void sc1000_audio_engine_start(struct sc1000* engine)
{
    if (engine->ops->start != nullptr) {
        engine->ops->start(engine);
    }
}

/*
 * Stop the device
 */
void sc1000_audio_engine_stop(struct sc1000* engine)
{
    if (engine->ops->stop != nullptr) {
        engine->ops->stop(engine);
    }
}

/*
 * Get file descriptors which should be polled for this device
 *
 * Do not return anything for callback-based audio systems. If the
 * return value is > 0, there must be a handle() function available.
 *
 * Return: the number of pollfd filled, or -1 on error
 */
ssize_t sc1000_audio_engine_pollfds(struct sc1000* engine, struct pollfd* pe, size_t z)
{
    if (engine->ops->pollfds != nullptr) {
        return engine->ops->pollfds(engine, pe, z);
    } else {
        return 0;
    }
}

/*
 * Handle any available input or output on the device
 *
 * This function can be called when there is activity on any file
 * descriptor, not specifically one returned by this device.
 */
void sc1000_audio_engine_handle(struct sc1000* engine)
{
    if (engine->fault) {
        return;
    }

    if (engine->ops->handle == nullptr) {
        return;
    }

    if (engine->ops->handle(engine) != 0) {
        engine->fault = true;
        fputs("Error handling audio device; disabling it\n", stderr);
    }
}

// Helper to handle recording for a single deck
static void handle_deck_recording(struct sc1000* engine, struct deck* deck, int deck_no)
{
    struct player* pl = &deck->player;

    // Start recording if requested
    if (pl->recording_started && !pl->recording) {
        if (alsa_start_recording(engine, deck_no)) {
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
        bool was_first_recording = !alsa_has_loop(engine, deck_no);

        // Stop recording
        alsa_stop_recording(engine, deck_no);

        // Switch player to use loop track (RT-safe: just a bool flag)
        // Audio engine will read from loop buffer instead of player->track
        pl->use_loop = true;  // Always switch to loop after recording
        if (was_first_recording) {
            pl->position = 0;
            pl->target_position = 0;
            pl->offset = 0;
        }

        pl->recording = false;
        pl->playing_beep = BEEP_RECORDINGSTOP;
    }
}

void sc1000_audio_engine_process(struct sc1000* engine, signed short* pcm, unsigned long frames)
{
    // Handle loop buffer recording for both decks (memory-based, for immediate scratching)
    handle_deck_recording(engine, &engine->beat_deck, 0);     // Beat deck = 0
    handle_deck_recording(engine, &engine->scratch_deck, 1);  // Scratch deck = 1

    audio_engine_process(engine, pcm, frames);
}
