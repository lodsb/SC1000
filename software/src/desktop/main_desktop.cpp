/*
 * SC1000 Desktop Test Application
 *
 * A simple terminal-based test application that simulates the SC1000
 * hardware inputs via keyboard controls. This allows testing the software
 * without actual hardware.
 *
 * Currently provides:
 * - Mock encoder (platter position) via arrow keys
 * - Mock fader (crossfader) via arrow keys
 * - Mock cap touch via spacebar
 * - Mock buttons via number keys
 *
 * Future: Could connect to actual SC1000 audio engine for full testing.
 */

#include "platform_desktop.h"

#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <unistd.h>

using namespace sc::desktop;

static DesktopPlatformState g_state;

static void signal_handler(int sig)
{
    (void)sig;
    g_state.running = false;
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    // Set up signal handlers for clean exit
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("SC1000 Desktop Test Application\n");
    printf("================================\n\n");
    printf("This is a mock platform for testing SC1000 controls.\n");
    printf("Press any key to start (or Q to quit)...\n\n");
    print_controls();

    // Wait for initial keypress
    getchar();

    // Initialize terminal for raw input
    terminal_init();

    // Initial draw
    draw_state(&g_state);

    // Main loop
    while (g_state.running) {
        // Process keyboard input
        if (!process_keyboard(&g_state)) {
            break;
        }

        // Update display if state changed
        draw_state(&g_state);

        // Small sleep to avoid burning CPU
        usleep(10000); // 10ms = 100Hz update rate
    }

    // Cleanup
    terminal_cleanup();

    printf("Goodbye!\n");
    return 0;
}
