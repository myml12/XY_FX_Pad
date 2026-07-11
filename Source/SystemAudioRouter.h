#pragma once

#include <juce_core/juce_core.h>

/** macOS system default output routing helpers for BlackHole capture. */
namespace SystemAudioRouter
{
    /** Find a device whose name contains the given text (case-insensitive). */
    juce::String findDeviceNameContaining (const juce::String& needle, bool wantOutput);

    /** Current system default output device name (empty if unavailable). */
    juce::String getDefaultOutputDeviceName();

    /** Prefer a real speaker/headphone over BlackHole for system default output. */
    juce::String findPreferredPlaybackOutputName();

    /**
     * If the system default output is BlackHole (e.g. leftover from a crash),
     * switch it to a normal playback device. No-op when already on a speaker.
     */
    bool ensureSystemOutputAvoidsBlackHole (juce::String& errorMessage);

    /** Save current default output devices, then point them at BlackHole. */
    bool routeSystemOutputToBlackHole (juce::String& errorMessage);

    /** Restore default output devices saved by routeSystemOutputToBlackHole(). */
    bool restoreSystemOutput (juce::String& errorMessage);

    bool hasSavedSystemOutput();
}
