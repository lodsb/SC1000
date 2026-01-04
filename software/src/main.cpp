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

#include <cassert>
#include <clocale>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <getopt.h>

#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/i2c-dev.h>

#include "core/sc_control_mapping.h"
#include "input/controller.h"

#include "core/sc_input.h"
#include "core/sc_settings.h"
#include "core/global.h"

#include "player/track.h"
#include "thread/realtime.h"
#include "thread/thread.h"
#include "thread/rig.h"

#include "util/log.h"
#include "main.h"

// Global root path (set from command line, used by sc1000_setup)
static const char* g_root_path = "/media/sda";

// Command-line option parsing
static void print_usage(const char* program) {
    fprintf(stderr, "Usage: %s [OPTIONS]\n\n", program);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --root PATH            Root directory for samples/settings (default: /media/sda)\n");
    fprintf(stderr, "  --log-console          Log to console (default)\n");
    fprintf(stderr, "  --log-file             Log to {root}/sc1000.log\n");
    fprintf(stderr, "  --log-file-path PATH   Log to specified file path\n");
    fprintf(stderr, "  --log-level LEVEL      Set log level (debug, info, warn, error)\n");
    fprintf(stderr, "  --show-stats           Enable FPS/DSP stats output\n");
    fprintf(stderr, "  --help                 Show this help message\n");
}

static sc::log::Level parse_log_level(const char* str) {
    if (strcmp(str, "debug") == 0) return sc::log::Level::LVL_DEBUG;
    if (strcmp(str, "info") == 0)  return sc::log::Level::LVL_INFO;
    if (strcmp(str, "warn") == 0)  return sc::log::Level::LVL_WARN;
    if (strcmp(str, "error") == 0) return sc::log::Level::LVL_ERROR;
    fprintf(stderr, "Unknown log level '%s', using 'info'\n", str);
    return sc::log::Level::LVL_INFO;
}

static void parse_args(int argc, char* argv[], sc::log::Config* log_config) {
    static struct option long_options[] = {
        {"root",           required_argument, nullptr, 'r'},
        {"log-console",    no_argument,       nullptr, 'c'},
        {"log-file",       no_argument,       nullptr, 'f'},
        {"log-file-path",  required_argument, nullptr, 'p'},
        {"log-level",      required_argument, nullptr, 'l'},
        {"show-stats",     no_argument,       nullptr, 's'},
        {"help",           no_argument,       nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "r:cfp:l:sh", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'r':
                g_root_path = optarg;
                break;
            case 'c':
                log_config->use_file = false;
                break;
            case 'f':
                log_config->use_file = true;
                break;
            case 'p':
                log_config->use_file = true;
                log_config->file_path = optarg;
                break;
            case 'l':
                log_config->min_level = parse_log_level(optarg);
                break;
            case 's':
                log_config->show_stats = true;
                break;
            case 'h':
                print_usage(argv[0]);
                exit(0);
            default:
                print_usage(argv[0]);
                exit(1);
        }
    }
}

static void sig_handler(int signo)
{
    if (signo == SIGINT) {
        printf("received SIGINT\n");
        exit(0);
    }
}

int main(int argc, char* argv[])
{
    int rc = -1, priority;
    bool use_mlock;

    // Parse command-line arguments
    sc::log::Config log_config;
    parse_args(argc, argv, &log_config);

    // Initialize logging system first
    sc::log::init(log_config);

    if (signal(SIGINT, sig_handler) == SIG_ERR) {
        SC_LOG_ERROR("Can't catch SIGINT");
        exit(1);
    }

    if (setlocale(LC_ALL, "") == nullptr) {
        SC_LOG_ERROR("Could not honour the local encoding");
        return -1;
    }
    if (thread_global_init() == -1) {
        return -1;
    }
    if (rig_init() == -1) {
        return -1;
    }
    rt_init(&g_rt);

    use_mlock = false;

    sc1000_setup(&g_sc1000_engine, &g_rt, g_root_path);
    sc1000_load_sample_folders(&g_sc1000_engine);

    rc = EXIT_FAILURE; /* until clean exit */

    // Start input processing thread
    start_sc_input_thread();

    // Start realtime stuff
    priority = 0;

    if (rt_start(&g_rt, priority) == -1) {
        return -1;
    }

    if (use_mlock && mlockall(MCL_CURRENT) == -1) {
        perror("mlockall");
        goto out_rt;
    }

    // Main loop
    SC_LOG_INFO("Entering main loop");

    if (rig_main() == -1) {
        goto out_interface;
    }

    // Exit
    rc = EXIT_SUCCESS;
    SC_LOG_INFO("Exiting cleanly...");

out_interface:
out_rt:
    rt_stop(&g_rt);

    sc1000_clear(&g_sc1000_engine);

    rig_clear();
    thread_global_clear();

    // Shutdown logging
    sc::log::shutdown();

    if (rc == EXIT_SUCCESS) {
        fprintf(stderr, "Done.\n");
    }

    return rc;
}
