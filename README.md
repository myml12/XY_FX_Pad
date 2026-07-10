# DJ XY Pad

DJ XY Pad is a JUCE-based desktop FX pad for experimenting with mouse-controlled and physical-controller-driven DJ effects.

The app lets you assign one effect to the X axis and another effect to the Y axis. Effects are applied only while the pad is pressed, making it useful for momentary Kaoss Pad style gestures.

## Features

- Mouse-draggable square XY pad
- Separate effect assignment for X and Y axes
- File playback from audio files placed in the project directory
- External input mode for live audio processing
- Playback position display and seeking
- Play/Pause button plus space-key transport toggle
- Nightcore mode that raises pitch and tempo together
- Lightweight BPM estimation for tempo-synced `Gate`, `Echo`, and `Roll`
- Separate serial-to-UDP bridge for the physical 4-point pressure controller

## Effects

Available effects:

- `Gate`
- `Echo`
- `Reverb`
- `Filter`
- `Low Pass`
- `High Pass`
- `Band Pass`
- `Notch`
- `Flanger`
- `Phaser`
- `Tremolo`
- `Drive`
- `BitCrusher`
- `Roll`

See [docs/TECHNICAL_SPEC.md](docs/TECHNICAL_SPEC.md) for DSP mappings and tempo-analysis details.

## Requirements

- macOS, currently tested locally on Apple Clang/CMake
- CMake 3.22+
- C++17 compiler
- JUCE

JUCE is intentionally not vendored in this repository. Use one of these approaches:

- Clone JUCE into `./JUCE`
- Install JUCE so `find_package(JUCE CONFIG)` can locate it
- Configure with `-DDJXYPAD_FETCH_JUCE=ON` in a network-enabled environment

## Build

With a local JUCE checkout at `./JUCE`:

```sh
cmake -S . -B build
cmake --build build
open build/DJXYPad_artefacts/DJ\ XY\ Pad.app
```

With CMake fetching JUCE:

```sh
cmake -S . -B build -DDJXYPAD_FETCH_JUCE=ON
cmake --build build
open build/DJXYPad_artefacts/DJ\ XY\ Pad.app
```

## Audio Files

Put local test audio files in the project root before building:

```text
*.m4a
*.mp3
*.wav
*.aiff
```

The CMake build copies those files into the app bundle resources. Audio files are ignored by git so copyrighted or local-only material is not published accidentally.

## Physical Controller

The Arduino firmware is in [firmware.ino](firmware.ino). It emits four pressure values at around 80 Hz.

Build the project, then run the bridge separately:

```sh
build/ControllerBridge
```

The default serial port is `/dev/cu.usbmodem1101`. The bridge converts the four pressure values into normalized XY coordinates and sends them to the app over UDP. See [docs/CONTROLLER.md](docs/CONTROLLER.md) for the protocol and coordinate math.

## Project Structure

```text
.
├── CMakeLists.txt
├── Source/
│   ├── Main.cpp
│   ├── MainComponent.h/.cpp
│   ├── XYPad.h/.cpp
│   ├── Effects.h/.cpp
│   ├── Tempo.h/.cpp
│   ├── ControllerReceiver.h/.cpp
│   └── ControllerTypes.h
├── Tools/
│   └── ControllerBridge.cpp
├── docs/
│   ├── TECHNICAL_SPEC.md
│   └── CONTROLLER.md
└── firmware.ino
```

## License

This project is released under the MIT License. JUCE and any other third-party dependencies remain under their own licenses.
