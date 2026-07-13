#include <algorithm>
#include <array>
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <optional>
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
// 端の補正を控えめにして、位置変化が急に端へ吸い付く感覚を抑える。
constexpr float padMargin = 0.05f;
constexpr int channelCount = 4;

// SC616C 500 g load cell / HX711 gain 128.
constexpr float loadCellCapacityGrams = 500.0f;
constexpr float loadCellRatedOutputVoltsPerVolt = 0.0007f;
constexpr float hx711Gain = 128.0f;
constexpr float hx711Counts = 16777216.0f;
constexpr float gramsPerRawCount = loadCellCapacityGrams
                                    / (loadCellRatedOutputVoltsPerVolt * hx711Gain * hx711Counts);

constexpr int calibrationWarmupSamples = 80;
constexpr int calibrationSamples = 140;

constexpr float minChannelGrams = -50.0f;
constexpr float maxChannelGrams = 250.0f;
constexpr float minTotalGrams = -20.0f;
constexpr float maxTotalGrams = 250.0f;

// 以前は超過サンプルを丸ごと破棄していたが、急な筆圧変化で
// 「以降ずっと棄却 → 表示が止まる」フリーズの主因になっていた。
// いまは差分をクランプして通し、絶対範囲外だけ落とす。
constexpr float maxStepPerChannelGrams = 40.0f;
constexpr float maxStepTotalGrams = 60.0f;
// この差分を超える変化は、連続2サンプルで確認できるまで採用しない。
// 1点だけのノイズをタッチとして送るのを防ぐ。
constexpr float confirmStepPerChannelGrams = 20.0f;
constexpr float confirmStepTotalGrams = 30.0f;
constexpr float confirmSampleToleranceGrams = 15.0f;
constexpr float emaAlpha = 0.35f;

constexpr int serialSilenceTimeoutMs = 750;
constexpr int serialReadChunk = 256;

using Clock = std::chrono::steady_clock;

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
    options.c_cc[VTIME] = 0;

    if (::tcsetattr (fd, TCSANOW, &options) != 0)
    {
        ::close (fd);
        return -1;
    }

    ::tcflush (fd, TCIFLUSH);
    return fd;
}

void makeStdinNonBlocking()
{
    const auto flags = ::fcntl (STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0)
        ::fcntl (STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}

std::optional<char> pollStdinKey()
{
    char key = 0;
    const auto bytes = ::read (STDIN_FILENO, &key, 1);

    if (bytes == 1)
        return key;

    return std::nullopt;
}

float expandPadMargin (float value)
{
    return std::clamp ((value - padMargin) / (1.0f - padMargin * 2.0f), 0.0f, 1.0f);
}

bool parseCsv4 (const std::string& line, std::array<float, channelCount>& values)
{
    std::stringstream stream (line);
    std::string token;

    for (int i = 0; i < channelCount; ++i)
    {
        if (! std::getline (stream, token, ','))
            return false;

        char* end = nullptr;
        const auto value = std::strtof (token.c_str(), &end);

        if (end == token.c_str() || *end != '\0' || ! std::isfinite (value))
            return false;

        values[static_cast<size_t> (i)] = value;
    }

    return ! std::getline (stream, token, ',');
}

float totalOf (const std::array<float, channelCount>& values)
{
    float total = 0.0f;
    for (const auto value : values)
        total += value;
    return total;
}

std::array<float, channelCount> medianOfColumns (
    std::array<std::array<float, calibrationSamples>, channelCount> samples)
{
    std::array<float, channelCount> result {};

    for (int channel = 0; channel < channelCount; ++channel)
    {
        auto& values = samples[static_cast<size_t> (channel)];
        std::nth_element (values.begin(), values.begin() + calibrationSamples / 2, values.end());
        result[static_cast<size_t> (channel)] = values[static_cast<size_t> (calibrationSamples / 2)];
    }

    return result;
}

class SampleFilter
{
public:
    void reset()
    {
        history = {};
        historySize = 0;
        lastValid.reset();
        pendingStep.reset();
        emaValue.reset();
    }

    std::optional<std::array<float, channelCount>> process (const std::array<float, channelCount>& sample)
    {
        auto clamped = sample;
        const auto total = totalOf (clamped);

        // 絶対範囲外のみ棄却（ここだけ落とす）
        for (const auto value : clamped)
            if (value < minChannelGrams || value > maxChannelGrams)
                return std::nullopt;

        if (total < minTotalGrams || total > maxTotalGrams)
            return std::nullopt;

        // 大きな変化は2サンプル連続で確認する。単発スパイクをクランプすると、
        // 一瞬のノイズが短い筆圧としてUDPに出るため、確認前は送信しない。
        if (lastValid.has_value())
        {
            const auto requiresConfirmation = exceedsStep (clamped, *lastValid,
                                                           confirmStepPerChannelGrams,
                                                           confirmStepTotalGrams);

            if (requiresConfirmation)
            {
                if (! pendingStep.has_value() || ! isConsistentWithPending (clamped, *pendingStep))
                {
                    pendingStep = clamped;
                    return std::nullopt;
                }

                pendingStep.reset();
            }
            else
            {
                pendingStep.reset();
            }

            // 確認済みの急変は、最大ステップまでに抑えて滑らかに追従する。
            for (int channel = 0; channel < channelCount; ++channel)
            {
                const auto previous = (*lastValid)[static_cast<size_t> (channel)];
                const auto delta = clamped[static_cast<size_t> (channel)] - previous;
                clamped[static_cast<size_t> (channel)] = previous
                    + std::clamp (delta, -maxStepPerChannelGrams, maxStepPerChannelGrams);
            }

            const auto previousTotal = totalOf (*lastValid);
            const auto newTotal = totalOf (clamped);
            const auto totalDelta = newTotal - previousTotal;

            if (std::abs (totalDelta) > maxStepTotalGrams)
            {
                const auto scale = maxStepTotalGrams / std::abs (totalDelta);
                for (int channel = 0; channel < channelCount; ++channel)
                {
                    const auto previous = (*lastValid)[static_cast<size_t> (channel)];
                    clamped[static_cast<size_t> (channel)] = previous
                        + (clamped[static_cast<size_t> (channel)] - previous) * scale;
                }
            }
        }

        lastValid = clamped;
        history[historySize % history.size()] = clamped;
        ++historySize;

        const auto count = std::min (historySize, history.size());
        std::array<float, channelCount> median {};
        for (int channel = 0; channel < channelCount; ++channel)
        {
            std::array<float, 3> values {};
            for (size_t i = 0; i < count; ++i)
                values[i] = history[i][static_cast<size_t> (channel)];

            std::sort (values.begin(), values.begin() + static_cast<std::ptrdiff_t> (count));
            median[static_cast<size_t> (channel)] = values[count / 2];
        }

        if (! emaValue.has_value())
            emaValue = median;
        else
            for (int channel = 0; channel < channelCount; ++channel)
                (*emaValue)[static_cast<size_t> (channel)] = emaAlpha * median[static_cast<size_t> (channel)]
                                                              + (1.0f - emaAlpha) * (*emaValue)[static_cast<size_t> (channel)];

        return emaValue;
    }

private:
    static bool exceedsStep (const std::array<float, channelCount>& a,
                             const std::array<float, channelCount>& b,
                             float perChannelLimit, float totalLimit)
    {
        for (int channel = 0; channel < channelCount; ++channel)
            if (std::abs (a[static_cast<size_t> (channel)] - b[static_cast<size_t> (channel)]) >= perChannelLimit)
                return true;

        return std::abs (totalOf (a) - totalOf (b)) >= totalLimit;
    }

    static bool isConsistentWithPending (const std::array<float, channelCount>& sample,
                                         const std::array<float, channelCount>& pending)
    {
        for (int channel = 0; channel < channelCount; ++channel)
            if (std::abs (sample[static_cast<size_t> (channel)] - pending[static_cast<size_t> (channel)])
                > confirmSampleToleranceGrams)
                return false;

        return std::abs (totalOf (sample) - totalOf (pending))
               <= confirmSampleToleranceGrams * 2.0f;
    }

    std::array<std::array<float, channelCount>, 3> history {};
    size_t historySize = 0;
    std::optional<std::array<float, channelCount>> lastValid;
    std::optional<std::array<float, channelCount>> pendingStep;
    std::optional<std::array<float, channelCount>> emaValue;
};

enum class CalibState
{
    warmup,
    collecting,
    ready
};

struct Calibration
{
    CalibState state = CalibState::warmup;
    int warmupRemaining = calibrationWarmupSamples;
    int sampleIndex = 0;
    std::array<std::array<float, calibrationSamples>, channelCount> values {};
    std::array<float, channelCount> rawOffset {};

    void begin (const char* reason)
    {
        state = CalibState::warmup;
        warmupRemaining = calibrationWarmupSamples;
        sampleIndex = 0;
        values = {};
        std::cout << "\n" << reason << " keep the pad unloaded...\n"
                  << "  (warmup " << calibrationWarmupSamples
                  << " + median " << calibrationSamples << " samples)\n";
    }

    bool ingest (const std::array<float, channelCount>& raw)
    {
        if (state == CalibState::ready)
            return false;

        if (state == CalibState::warmup)
        {
            --warmupRemaining;
            if (warmupRemaining <= 0)
                state = CalibState::collecting;
            return false;
        }

        for (int channel = 0; channel < channelCount; ++channel)
            values[static_cast<size_t> (channel)][static_cast<size_t> (sampleIndex)]
                = raw[static_cast<size_t> (channel)];

        ++sampleIndex;

        if (sampleIndex >= calibrationSamples)
        {
            rawOffset = medianOfColumns (values);
            state = CalibState::ready;
            std::cout << "zero point calibrated  (press 'c' anytime to recalibrate)\n";
            return true;
        }

        return false;
    }

    bool isReady() const { return state == CalibState::ready; }
};

void handleParsedLine (const std::array<float, channelCount>& raw,
                       Calibration& calibration,
                       SampleFilter& filter,
                       int udpFd,
                       const sockaddr_in& destination)
{
    if (! calibration.isReady())
    {
        calibration.ingest (raw);
        return;
    }

    std::array<float, channelCount> grams {};
    for (int channel = 0; channel < channelCount; ++channel)
        grams[static_cast<size_t> (channel)]
            = (raw[static_cast<size_t> (channel)] - calibration.rawOffset[static_cast<size_t> (channel)])
              * gramsPerRawCount;

    if (const auto filtered = filter.process (grams))
    {
        const auto topRight = (*filtered)[0];
        const auto topLeft = (*filtered)[1];
        const auto bottomRight = (*filtered)[3];
        const auto total = totalOf (*filtered);
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
                  reinterpret_cast<const sockaddr*> (&destination), sizeof (destination));

        if (touching)
            std::cout << "\rXY " << x << "," << y << " total=" << total << "g       " << std::flush;
        else
            std::cout << "\r検出なし total=" << total << "g       " << std::flush;
    }
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

    makeStdinNonBlocking();

    std::cout << "ControllerBridge serial=" << serialPort
              << " -> udp=" << udpHost << ":" << udpPort << "\n";
    std::cout << "format: x,y,touch,totalGrams    threshold=" << touchThresholdGrams << "g\n";
    std::cout << "filter: median=3 EMA=" << emaAlpha
              << " step-clamp=" << maxStepPerChannelGrams << "g/ch, " << maxStepTotalGrams << "g total\n";
    std::cout << "keys: c = recalibrate zero point (pad unloaded), q = quit\n";

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
        Calibration calibration;
        SampleFilter filter;
        calibration.begin ("calibrating zero point:");
        auto lastByteTime = Clock::now();
        bool warnedSilence = false;

        while (true)
        {
            if (const auto key = pollStdinKey())
            {
                if (*key == 'q' || *key == 'Q')
                {
                    std::cout << "\nquit\n";
                    ::close (serialFd);
                    ::close (udpFd);
                    return 0;
                }

                if (*key == 'c' || *key == 'C')
                {
                    filter.reset();
                    calibration.begin ("recalibrating zero point:");
                }
            }

            char chunk[serialReadChunk];
            const auto bytes = ::read (serialFd, chunk, sizeof (chunk));

            if (bytes > 0)
            {
                lastByteTime = Clock::now();
                warnedSilence = false;

                for (ssize_t i = 0; i < bytes; ++i)
                {
                    const auto ch = chunk[i];

                    if (ch == '\n' || ch == '\r')
                    {
                        if (! line.empty())
                        {
                            std::array<float, channelCount> raw {};

                            if (parseCsv4 (line, raw))
                                handleParsedLine (raw, calibration, filter, udpFd, destination);

                            line.clear();
                        }
                    }
                    else if (line.size() < 128)
                    {
                        line.push_back (ch);
                    }
                    else
                    {
                        // 壊れた行は捨てて復帰
                        line.clear();
                    }
                }
            }
            else if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                std::cout << "\nserial disconnected (" << std::strerror (errno) << ")\n";
                break;
            }
            else
            {
                const auto silentMs = std::chrono::duration_cast<std::chrono::milliseconds> (
                    Clock::now() - lastByteTime).count();

                if (silentMs > serialSilenceTimeoutMs)
                {
                    if (! warnedSilence)
                    {
                        std::cout << "\nserial silence >" << serialSilenceTimeoutMs
                                  << "ms — waiting for data (check USB / firmware)\n";
                        warnedSilence = true;
                    }

                    // 長沈黙は切断扱いで再接続（USBリセット後の復旧）
                    if (silentMs > serialSilenceTimeoutMs * 4)
                    {
                        std::cout << "reconnecting serial...\n";
                        break;
                    }
                }

                ::usleep (1000);
            }
        }

        ::close (serialFd);
    }
}
