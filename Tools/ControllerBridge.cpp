#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <string>
#include <termios.h>
#include <unistd.h>

namespace
{
constexpr const char* defaultSerialPort = "/dev/cu.usbmodem1101";
constexpr const char* udpHost = "127.0.0.1";
constexpr int udpPort = 45454;
constexpr float touchThresholdGrams = 5.0f;
constexpr float padMargin = 0.10f;

int openSerial (const char* path)
{
    const auto fd = ::open (path, O_RDONLY | O_NOCTTY | O_NONBLOCK);

    if (fd < 0)
        return -1;

    termios options {};
    if (::tcgetattr (fd, &options) != 0)
    {
        ::close (fd);
        return -1;
    }

    ::cfmakeraw (&options);
    ::cfsetspeed (&options, B115200);
    options.c_cflag |= static_cast<tcflag_t> (CLOCAL | CREAD);
    options.c_cflag &= static_cast<tcflag_t> (~CRTSCTS);
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 1;

    if (::tcsetattr (fd, TCSANOW, &options) != 0)
    {
        ::close (fd);
        return -1;
    }

    ::tcflush (fd, TCIFLUSH);
    return fd;
}

float positiveGram (float value)
{
    return std::max (0.0f, value);
}

float expandPadMargin (float value)
{
    return std::clamp ((value - padMargin) / (1.0f - padMargin * 2.0f), 0.0f, 1.0f);
}

bool parseCsv4 (const std::string& line, float (&g)[4])
{
    std::stringstream stream (line);
    std::string token;

    for (int i = 0; i < 4; ++i)
    {
        if (! std::getline (stream, token, ','))
            return false;

        g[i] = positiveGram (std::strtof (token.c_str(), nullptr));
    }

    return true;
}
}

int main (int argc, char** argv)
{
    const auto* serialPort = argc > 1 ? argv[1] : defaultSerialPort;

    const auto udpFd = ::socket (AF_INET, SOCK_DGRAM, 0);
    if (udpFd < 0)
    {
        std::cerr << "UDP socket failed: " << std::strerror (errno) << "\n";
        return 1;
    }

    sockaddr_in destination {};
    destination.sin_family = AF_INET;
    destination.sin_port = htons (static_cast<uint16_t> (udpPort));
    ::inet_pton (AF_INET, udpHost, &destination.sin_addr);

    std::cout << "ControllerBridge serial=" << serialPort
              << " -> udp=" << udpHost << ":" << udpPort << "\n";
    std::cout << "format: x,y,touch,totalGrams    threshold=" << touchThresholdGrams << "g\n";

    while (true)
    {
        const auto serialFd = openSerial (serialPort);

        if (serialFd < 0)
        {
            std::cout << "\r検出なし serial open failed: " << serialPort << "        " << std::flush;
            ::usleep (1000000);
            continue;
        }

        std::cout << "\nserial connected: " << serialPort << "\n";

        std::string line;
        char ch = 0;

        while (true)
        {
            const auto bytes = ::read (serialFd, &ch, 1);

            if (bytes == 1)
            {
                if (ch == '\n' || ch == '\r')
                {
                    if (! line.empty())
                    {
                        float g[4] {};

                        if (parseCsv4 (line, g))
                        {
                            const auto topRight = g[0];
                            const auto topLeft = g[1];
                            const auto bottomLeft = g[2];
                            const auto bottomRight = g[3];
                            const auto total = topLeft + topRight + bottomLeft + bottomRight;
                            const auto touching = total >= touchThresholdGrams;
                            auto x = 0.5f;
                            auto y = 0.0f;

                            if (touching)
                            {
                                const auto rawX = std::clamp ((topRight + bottomRight) / total, 0.0f, 1.0f);
                                const auto rawY = std::clamp ((topLeft + topRight) / total, 0.0f, 1.0f);
                                x = expandPadMargin (rawX);
                                y = expandPadMargin (rawY);
                            }

                            char packet[96] {};
                            std::snprintf (packet, sizeof (packet), "%.6f,%.6f,%d,%.3f\n",
                                           x, y, touching ? 1 : 0, total);
                            ::sendto (udpFd, packet, std::strlen (packet), 0,
                                      reinterpret_cast<sockaddr*> (&destination), sizeof (destination));

                            if (touching)
                                std::cout << "\rXY " << x << "," << y << " total=" << total << "g       " << std::flush;
                            else
                                std::cout << "\r検出なし total=" << total << "g       " << std::flush;
                        }

                        line.clear();
                    }
                }
                else if (line.size() < 128)
                {
                    line.push_back (ch);
                }
            }
            else if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                std::cout << "\nserial disconnected\n";
                break;
            }
            else
            {
                ::usleep (1000);
            }
        }

        ::close (serialFd);
    }
}
