cmake_minimum_required(VERSION 3.15...4.0)

project(SC1000BUILD VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(NATIVE "NATIVE" OFF)

include(ExternalProject)
include(FindALSA)

add_executable(sc1000)

target_sources(sc1000
        PRIVATE
        src/main.c
        src/util/external.c
        src/util/debug.h
        src/util/status.c
        src/thread/realtime.c
        src/thread/rig.c
        src/thread/thread.c
        src/player/cues.c
        src/player/deck.c
        src/player/dicer.c
        src/player/player.c
        src/player/playlist.c
        src/player/track.c
        src/input/controller.c
        src/input/midi.c
        src/audio/alsa.c
        src/app/sc_settings.cpp
        src/app/sc_input.c
        src/app/sc_control_mapping.c
        src/app/global.c
        src/app/global.h
        src/app/audio_engine.cpp
        src/app/sc1000.c
)

if(${NATIVE})
    target_compile_options(sc1000 PRIVATE -flto -ftree-vectorize)
else()
    target_compile_options(sc1000 PRIVATE -mcpu=cortex-a8 -mfpu=neon -flto -ftree-vectorize)
endif()

target_include_directories(sc1000 PRIVATE deps)

target_link_libraries(sc1000 asound m -static-libstdc++ -static-libgcc)

