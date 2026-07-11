#include "SystemAudioRouter.h"

#if JUCE_MAC
 #include <CoreAudio/CoreAudio.h>
#endif

namespace SystemAudioRouter
{
namespace
{
#if JUCE_MAC
    AudioDeviceID savedDefaultOutput = kAudioObjectUnknown;
    AudioDeviceID savedDefaultSystemOutput = kAudioObjectUnknown;
    bool hasSavedDefaults = false;

    AudioObjectPropertyAddress makeAddress (AudioObjectPropertySelector selector,
                                            AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal)
    {
        return { selector, scope, kAudioObjectPropertyElementMain };
    }

    juce::String deviceName (AudioDeviceID device)
    {
        if (device == kAudioObjectUnknown)
            return {};

        CFStringRef cfName = nullptr;
        UInt32 size = sizeof (cfName);
        auto address = makeAddress (kAudioObjectPropertyName);

        if (AudioObjectGetPropertyData (device, &address, 0, nullptr, &size, &cfName) != noErr || cfName == nullptr)
            return {};

        juce::String name = juce::String::fromCFString (cfName);
        CFRelease (cfName);
        return name;
    }

    juce::Array<AudioDeviceID> allDevices()
    {
        auto address = makeAddress (kAudioHardwarePropertyDevices);
        UInt32 size = 0;

        if (AudioObjectGetPropertyDataSize (kAudioObjectSystemObject, &address, 0, nullptr, &size) != noErr || size == 0)
            return {};

        juce::Array<AudioDeviceID> devices;
        devices.resize (static_cast<int> (size / sizeof (AudioDeviceID)));

        if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &address, 0, nullptr, &size, devices.getRawDataPointer()) != noErr)
            return {};

        return devices;
    }

    bool deviceHasScope (AudioDeviceID device, AudioObjectPropertyScope scope)
    {
        auto address = makeAddress (kAudioDevicePropertyStreamConfiguration, scope);
        UInt32 size = 0;

        if (AudioObjectGetPropertyDataSize (device, &address, 0, nullptr, &size) != noErr || size == 0)
            return false;

        juce::HeapBlock<char> storage (size);
        auto* bufferList = reinterpret_cast<AudioBufferList*> (storage.getData());

        if (AudioObjectGetPropertyData (device, &address, 0, nullptr, &size, bufferList) != noErr)
            return false;

        for (UInt32 i = 0; i < bufferList->mNumberBuffers; ++i)
            if (bufferList->mBuffers[i].mNumberChannels > 0)
                return true;

        return false;
    }

    AudioDeviceID getDefaultDevice (AudioObjectPropertySelector selector)
    {
        AudioDeviceID device = kAudioObjectUnknown;
        UInt32 size = sizeof (device);
        auto address = makeAddress (selector);
        AudioObjectGetPropertyData (kAudioObjectSystemObject, &address, 0, nullptr, &size, &device);
        return device;
    }

    bool setDefaultDevice (AudioObjectPropertySelector selector, AudioDeviceID device)
    {
        auto address = makeAddress (selector);
        return AudioObjectSetPropertyData (kAudioObjectSystemObject, &address, 0, nullptr, sizeof (device), &device) == noErr;
    }

    AudioDeviceID findDeviceIdContaining (const juce::String& needle, bool wantOutput)
    {
        const auto scope = wantOutput ? kAudioObjectPropertyScopeOutput : kAudioObjectPropertyScopeInput;

        for (const auto device : allDevices())
        {
            if (! deviceHasScope (device, scope))
                continue;

            if (deviceName (device).containsIgnoreCase (needle))
                return device;
        }

        return kAudioObjectUnknown;
    }

    AudioDeviceID findPreferredPlaybackDeviceId()
    {
        const auto current = getDefaultDevice (kAudioHardwarePropertyDefaultOutputDevice);
        const auto currentName = deviceName (current);

        if (current != kAudioObjectUnknown && ! currentName.containsIgnoreCase ("BlackHole"))
            return current;

        AudioDeviceID fallback = kAudioObjectUnknown;

        for (const auto device : allDevices())
        {
            if (! deviceHasScope (device, kAudioObjectPropertyScopeOutput))
                continue;

            const auto name = deviceName (device);

            if (name.containsIgnoreCase ("BlackHole"))
                continue;

            // Prefer built-in / headphone-style names when recovering from BlackHole.
            if (name.containsIgnoreCase ("MacBook")
                || name.containsIgnoreCase ("Built-in")
                || name.containsIgnoreCase ("Speaker")
                || name.containsIgnoreCase ("Headphone")
                || name.containsIgnoreCase ("AirPods"))
                return device;

            if (fallback == kAudioObjectUnknown)
                fallback = device;
        }

        return fallback;
    }
#endif
} // namespace

juce::String findDeviceNameContaining (const juce::String& needle, bool wantOutput)
{
   #if JUCE_MAC
    const auto id = findDeviceIdContaining (needle, wantOutput);
    return deviceName (id);
   #else
    juce::ignoreUnused (needle, wantOutput);
    return {};
   #endif
}

juce::String getDefaultOutputDeviceName()
{
   #if JUCE_MAC
    return deviceName (getDefaultDevice (kAudioHardwarePropertyDefaultOutputDevice));
   #else
    return {};
   #endif
}

juce::String findPreferredPlaybackOutputName()
{
   #if JUCE_MAC
    return deviceName (findPreferredPlaybackDeviceId());
   #else
    return {};
   #endif
}

bool ensureSystemOutputAvoidsBlackHole (juce::String& errorMessage)
{
   #if JUCE_MAC
    const auto current = getDefaultDevice (kAudioHardwarePropertyDefaultOutputDevice);

    if (current == kAudioObjectUnknown || ! deviceName (current).containsIgnoreCase ("BlackHole"))
        return true;

    const auto preferred = findPreferredPlaybackDeviceId();

    if (preferred == kAudioObjectUnknown)
    {
        errorMessage = "No non-BlackHole output device found.";
        return false;
    }

    if (! setDefaultDevice (kAudioHardwarePropertyDefaultOutputDevice, preferred)
        || ! setDefaultDevice (kAudioHardwarePropertyDefaultSystemOutputDevice, preferred))
    {
        errorMessage = "Failed to move system output off BlackHole.";
        return false;
    }

    return true;
   #else
    juce::ignoreUnused (errorMessage);
    return true;
   #endif
}

bool routeSystemOutputToBlackHole (juce::String& errorMessage)
{
   #if JUCE_MAC
    const auto blackHole = findDeviceIdContaining ("BlackHole", true);

    if (blackHole == kAudioObjectUnknown)
    {
        errorMessage = "BlackHole output device not found.";
        return false;
    }

    if (! hasSavedDefaults)
    {
        auto output = getDefaultDevice (kAudioHardwarePropertyDefaultOutputDevice);
        auto systemOutput = getDefaultDevice (kAudioHardwarePropertyDefaultSystemOutputDevice);

        // Don't remember BlackHole as the restore target (e.g. leftover from a crash).
        if (deviceName (output).containsIgnoreCase ("BlackHole"))
            output = findPreferredPlaybackDeviceId();

        if (deviceName (systemOutput).containsIgnoreCase ("BlackHole"))
            systemOutput = output;

        savedDefaultOutput = output;
        savedDefaultSystemOutput = systemOutput;
        hasSavedDefaults = true;
    }

    if (! setDefaultDevice (kAudioHardwarePropertyDefaultOutputDevice, blackHole)
        || ! setDefaultDevice (kAudioHardwarePropertyDefaultSystemOutputDevice, blackHole))
    {
        errorMessage = "Failed to set system output to BlackHole.";
        return false;
    }

    return true;
   #else
    errorMessage = "System output routing is only supported on macOS.";
    return false;
   #endif
}

bool restoreSystemOutput (juce::String& errorMessage)
{
   #if JUCE_MAC
    if (! hasSavedDefaults)
        return true;

    bool ok = true;

    if (savedDefaultOutput != kAudioObjectUnknown)
        ok = setDefaultDevice (kAudioHardwarePropertyDefaultOutputDevice, savedDefaultOutput) && ok;

    if (savedDefaultSystemOutput != kAudioObjectUnknown)
        ok = setDefaultDevice (kAudioHardwarePropertyDefaultSystemOutputDevice, savedDefaultSystemOutput) && ok;

    hasSavedDefaults = false;
    savedDefaultOutput = kAudioObjectUnknown;
    savedDefaultSystemOutput = kAudioObjectUnknown;

    if (! ok)
        errorMessage = "Failed to restore system output device.";

    return ok;
   #else
    juce::ignoreUnused (errorMessage);
    return true;
   #endif
}

bool hasSavedSystemOutput()
{
   #if JUCE_MAC
    return hasSavedDefaults;
   #else
    return false;
   #endif
}
} // namespace SystemAudioRouter
