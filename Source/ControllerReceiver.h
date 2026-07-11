#pragma once

#include "ControllerTypes.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>

class ControllerReceiver final : private juce::Thread
{
public:
    explicit ControllerReceiver (int udpPort);
    ~ControllerReceiver() override;

    void start();
    void stop();

    std::function<void (const ControllerSample& sample)> onSample;
    std::function<void (bool connected, juce::String message)> onConnectionChanged;

private:
    void run() override;
    bool openSocket();
    void closeSocket();
    bool handlePacket (const juce::String& packet);
    void publishTouchRelease();
    void publishConnection (bool connected, const juce::String& message);

    int udpPort = 45454;
    int socketFd = -1;
    bool touchActive = false;
    std::atomic<bool> connected { false };
};
