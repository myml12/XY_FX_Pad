# Technical Specification

This document captures the implementation details for the JUCE FX pad app.

## Source Layout

| File | Role |
| --- | --- |
| `Source/Main.cpp` | JUCEApplication and main window entry point |
| `Source/MainComponent.h/.cpp` | GUI, file/input mode, transport, audio routing, controller updates |
| `Source/XYPad.h/.cpp` | Square XY pad drawing and mouse interaction |
| `Source/Effects.h/.cpp` | Effect enum, labels, DSP implementation, delay/roll buffers |
| `Source/Tempo.h/.cpp` | BPM estimation and tempo-section map |
| `Source/ControllerReceiver.h/.cpp` | UDP receiver for normalized controller XY samples |
| `Tools/ControllerBridge.cpp` | Serial-to-UDP bridge for the physical 4-point pressure controller |

## Audio Routing

`MainComponent` derives from `juce::AudioAppComponent` and opens stereo input/output with `setAudioChannels (2, 2)`.

- `File` mode uses `juce::AudioTransportSource` as the source, then applies effects before output.
- `Input` mode passes the input buffer to output and applies effects only while the XY pad is pressed.
- X and Y each own a separate `EffectProcessor`, so stateful effects do not share delay/reverb/phaser/roll state across axes.
- Effects are momentary. Releasing the pad resets momentary state for delay/reverb/phaser/gate/roll style effects.
- Shutdown order is timer stop, controller stop, key listener removal, transport stop, source release, reader release, then `shutdownAudio()`.

## Effects

All effect controls receive a normalized axis value in `0.0..1.0`. Most parameter curves use `smoothstep` or tempo-quantized steps so the endpoints are more controllable.

| Effect | Mapping |
| --- | --- |
| `Gate` | Fixed 16th-note BPM-synced gate. Axis value increases depth/wet. |
| `Echo` | BPM-synced delay time plus feedback/wet. |
| `Reverb` | Room size and wet mix. |
| `Filter` | LPF on the left, normal around center, HPF on the right. |
| `Low Pass` | Swept cutoff. |
| `High Pass` | Swept cutoff. |
| `Band Pass` | Swept center frequency with lower Q and output compensation. |
| `Notch` | Swept center frequency. |
| `Flanger` | Rate, delay depth, wet, and feedback. |
| `Phaser` | Rate, center frequency, feedback, and mix. |
| `Tremolo` | LFO depth and rate. |
| `Drive` | Soft-clipping drive and wet with output trim and a simple limiter. |
| `BitCrusher` | Quantization amount and wet mix. |
| `Roll` | Tempo-synced buffer repeat from `1/32` to `1 bar` with wet mix. |

## Tempo Analysis

`TempoAnalyzer` analyzes audio when a file is loaded and produces a global BPM plus a small tempo-section map.

- Analysis range: first 600 seconds.
- Hop size: 1024 samples.
- Feature: frame-to-frame difference of channel RMS energy, used as an onset-like envelope.
- BPM search range: 70.0..190.0 BPM in 0.5 BPM steps.
- Global BPM: highest autocorrelation score over the full analysis range.
- Local BPM: 24 second window, 4 second step, blended as 28% global BPM and 72% local BPM.
- Stabilization: snap values near global, half-time, or double-time BPM; then apply a 3-point median filter.
- Sectioning: ignore changes under 6 BPM and changes shorter than 1 second.
- Change limit: at most 2 tempo changes per track.
- Playback: use the tempo section containing the current transport position.
- UI/audio tempo state: switch BPM only when the same candidate remains stable for at least 1 second.

This is intentionally lightweight and DJ-oriented. It is designed for tracks with a stable main section and, at most, a small number of large tempo changes such as a slow intro followed by a faster body.

## Nightcore Mode

Nightcore mode is file-playback only. It reconnects the `AudioTransportSource` with a source sample-rate correction of `1.18x`, so pitch and tempo rise together without independent time stretching.

Approximate pitch shift:

```text
12 * log2(1.18) ~= +2.9 semitones
```

When toggled, the app preserves the source playback position. BPM display and tempo-synced effects use the playback-rate-adjusted BPM.
