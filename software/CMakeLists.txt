cmake_minimum_required(VERSION 3.15...4.0)

project(SC1000BUILD VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(NATIVE "Build for native host instead of ARM" OFF)

# Source files organized by directory
set(CORE_SOURCES
        src/core/sc1000.cpp
        src/core/sc_settings.cpp
        src/core/sc_input.cpp
        src/core/global.cpp
)

set(CONTROL_SOURCES
        src/control/actions.cpp
)

set(ENGINE_SOURCES
        src/engine/audio_engine.cpp
        src/engine/cv_engine.cpp
)

set(PLATFORM_SOURCES
        src/platform/alsa.cpp
        src/platform/midi.cpp
        src/platform/gpio.cpp
        src/platform/i2c.cpp
        src/platform/encoder.cpp
        src/platform/pic.cpp
)

set(INPUT_SOURCES
        src/input/controller.cpp
        src/input/midi_controller.cpp
        src/input/midi_event.cpp
)

set(PLAYER_SOURCES
        src/player/cues.cpp
        src/player/deck.cpp
        src/player/player.cpp
        src/player/playlist.cpp
        src/player/track.cpp
)

set(THREAD_SOURCES
        src/thread/realtime.cpp
        src/thread/rig.cpp
        src/thread/thread.cpp
)

set(UTIL_SOURCES
        src/util/external.cpp
        src/util/status.cpp
)

add_executable(sc1000
        src/main.cpp
        ${CORE_SOURCES}
        ${CONTROL_SOURCES}
        ${ENGINE_SOURCES}
        ${PLATFORM_SOURCES}
        ${INPUT_SOURCES}
        ${PLAYER_SOURCES}
        ${THREAD_SOURCES}
        ${UTIL_SOURCES}
)

# Compiler flags
if(NATIVE)
    message(STATUS "Building for NATIVE host")
    target_compile_options(sc1000 PRIVATE -flto -ftree-vectorize)
else()
    message(STATUS "Building for ARM (SC1000 device)")
    target_compile_options(sc1000 PRIVATE -mcpu=cortex-a8 -mfpu=neon -flto -ftree-vectorize)
endif()

# Common warning flags
target_compile_options(sc1000 PRIVATE
        -Wall
        -Wextra
        -Wconversion
        -Wno-unused-parameter
)

# Include directories
target_include_directories(sc1000 PRIVATE
        deps
        src
)

# Link libraries
# Static linking for C/C++ runtime, dynamic for ALSA (needs dlopen for plugins)
target_link_options(sc1000 PRIVATE
        -static-libstdc++
        -static-libgcc
)

target_link_libraries(sc1000
        asound
        m
        pthread
)
