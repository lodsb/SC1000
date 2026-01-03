cmake_minimum_required(VERSION 3.15...4.0)

project(SC1000BUILD VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(NATIVE "NATIVE" OFF)

include(ExternalProject)
include(FindALSA)

add_executable(sc1000
        src/app/sc1000_ops.h)

target_sources(sc1000
        PRIVATE
        src/main.cpp
        src/util/external.cpp
        src/util/debug.h
        src/util/status.cpp
        src/thread/realtime.cpp
        src/thread/rig.cpp
        src/thread/thread.cpp
        src/player/cues.cpp
        src/player/deck.cpp
        src/input/midi_controller.cpp
        src/player/player.cpp
        src/player/playlist.cpp
        src/player/track.cpp
        src/input/controller.cpp
        src/input/midi.cpp
        src/input/midi_event.cpp
        src/audio/alsa.cpp
        src/app/sc_settings.cpp
        src/app/sc_input.cpp
        src/app/global.cpp
        src/app/global.h
        src/app/audio_engine.cpp
        src/app/sc1000.cpp
)

if(${NATIVE})
    target_compile_options(sc1000 PRIVATE -flto -ftree-vectorize)
else()
    target_compile_options(sc1000 PRIVATE -mcpu=cortex-a8 -mfpu=neon -flto -ftree-vectorize)
endif()

target_include_directories(sc1000 PRIVATE deps)

target_link_libraries(sc1000 asound m -static-libstdc++ -static-libgcc)

