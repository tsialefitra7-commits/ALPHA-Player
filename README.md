# ALPHA Player

ALPHA Player is a native Windows 10/11 audio player written in C++20 with a dark, colorful Win32 interface.

## Live audio path

The Windows player routes audio through an actual processing pipeline:

1. Media Foundation Source Reader decodes the selected file into floating-point PCM using the current Windows output device sample rate and channel count.
2. PCM samples pass through a 10-band peaking equalizer (31 Hz–16 kHz, ±12 dB).
3. Processed samples are converted to the device mix format and sent to the default output through WASAPI shared mode.
4. The visualizer analyzes those processed PCM blocks and displays a rolling 32-bar RMS level view.

The ten vertical sliders change EQ gains during playback. Volume is applied to the PCM stream before WASAPI output. The audio engine runs on a worker thread; the UI refreshes independently and does not require a dedicated GPU.

## Supported audio

The file picker recognizes MP3, WAV, FLAC, AAC, M4A, WMA, OGG and OPUS. Actual decoding depends on Media Foundation codecs available on the Windows installation; FLAC, OGG and OPUS support is not guaranteed on every system.

## Build on Windows

Requirements: Visual Studio 2022 with Desktop development with C++, Windows 10/11 SDK, and CMake 3.20+.

Build commands:

    cmake -S . -B build -A x64
    cmake --build build --config Release --parallel
    ctest --test-dir build -C Release --output-on-failure

The executable is produced at build\Release\AlphaPlayer.exe.

## Portable DSP tests

The EQ and visualizer core can be tested on Windows or Linux:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    cmake --build build --parallel
    ctest --test-dir build --output-on-failure

## Source layout

- src/main.cpp — native Win32 interface and controls
- src/platform/audio_engine_win32.* — Media Foundation decoder, PCM equalizer path and WASAPI renderer
- src/core/ — equalizer DSP and visualizer analysis
- tests/ — portable DSP regression tests

No third-party audio framework is required.