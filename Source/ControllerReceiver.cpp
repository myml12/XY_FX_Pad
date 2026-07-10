#include "ControllerReceiver.h"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

ControllerReceiver::ControllerReceiver (int port)
    : juce::Thread ("ControllerReceiver"),
      udpPort (port)
{
}

ControllerReceiver::~ControllerReceiver()
{
    stop();
}

void ControllerReceiver::start()
{
    startThread();
}

void ControllerReceiver::stop()
{
    signalThreadShouldExit();
    stopThread (1000);
    closeSocket();
}

void ControllerReceiver::run()
{
    while (! threadShouldExit())
    {
        if (! openSocket())
        {
            publishConnection (false, "Controller UDP unavailable");
            wait (1000);
            continue;
        }

        publishConnection (true, "Controller UDP listening :" + juce::String (udpPort));

        char buffer[256] {};

        while (! threadShouldExit())
        {
            const auto bytes = ::recv (socketFd, buffer, sizeof (buffer) - 1, 0);

            if (bytes > 0)
            {
                buffer[bytes] = '\0';
                handlePacket (juce::String::fromUTF8 (buffer).trim());
            }
            else if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                break;
            }
            else
            {
                wait (2);
            }
        }

        closeSocket();
        publishConnection (false, "Controller UDP stopped");
    }
}

bool ControllerReceiver::openSocket()
{
    closeSocket();

    socketFd = ::socket (AF_INET, SOCK_DGRAM, 0);

    if (socketFd < 0)
        return false;

    const int reuse = 1;
    ::setsockopt (socketFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof (reuse));

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons (static_cast<uint16_t> (udpPort));
    address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

    if (::bind (socketFd, reinterpret_cast<sockaddr*> (&address), sizeof (address)) != 0)
    {
        closeSocket();
        return false;
    }

    const auto flags = ::fcntl (socketFd, F_GETFL, 0);
    ::fcntl (socketFd, F_SETFL, flags | O_NONBLOCK);

    connected.store (true);
    return true;
}

void ControllerReceiver::closeSocket()
{
    if (socketFd >= 0)
    {
        ::close (socketFd);
        socketFd = -1;
    }

    connected.store (false);
}

void ControllerReceiver::handlePacket (const juce::String& packet)
{
    const auto parts = juce::StringArray::fromTokens (packet, ",", "");

    if (parts.size() < 4)
        return;

    ControllerSample sample;
    sample.x = juce::jlimit (0.0f, 1.0f, parts[0].getFloatValue());
    sample.y = juce::jlimit (0.0f, 1.0f, parts[1].getFloatValue());
    sample.touching = parts[2].getIntValue() != 0;
    sample.totalGrams = parts[3].getFloatValue();

    if (onSample != nullptr)
        onSample (sample);
}

void ControllerReceiver::publishConnection (bool isConnected, const juce::String& message)
{
    if (onConnectionChanged != nullptr)
        onConnectionChanged (isConnected, message);
}
