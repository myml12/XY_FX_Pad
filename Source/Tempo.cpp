#include "Tempo.h"

#include <algorithm>
#include <cmath>

namespace
{
float snapNearGlobalTempo (float bpm, float globalBpm)
{
    const auto candidates = { globalBpm * 0.5f, globalBpm, globalBpm * 2.0f };

    for (const auto candidate : candidates)
        if (std::abs (bpm - candidate) <= 3.0f)
            return candidate;

    return std::round (bpm);
}

void stabilizeTempoMap (TempoAnalysis& analysis)
{
    constexpr auto minTempoChangeBpm = 6.0f;
    constexpr auto minChangeSpacingSeconds = 1.0;
    constexpr auto maxTempoChanges = 2;

    if (analysis.map.empty())
        return;

    for (auto& point : analysis.map)
        point.bpm = snapNearGlobalTempo (point.bpm, analysis.globalBpm);

    // Median filter over neighbouring windows to suppress one-off estimation spikes.
    auto smoothed = analysis.map;

    for (size_t i = 0; i < analysis.map.size(); ++i)
    {
        std::vector<float> values;
        const auto begin = i > 0 ? i - 1 : i;
        const auto end = std::min (analysis.map.size() - 1, i + 1);

        for (auto j = begin; j <= end; ++j)
            values.push_back (analysis.map[j].bpm);

        std::sort (values.begin(), values.end());
        smoothed[i].bpm = values[values.size() / 2];
    }

    analysis.map = std::move (smoothed);

    // Collapse tiny changes and require a large enough delta to create a new section.
    std::vector<TempoPoint> collapsed;
    collapsed.push_back (analysis.map.front());

    for (const auto& point : analysis.map)
    {
        auto& current = collapsed.back();

        if (std::abs (point.bpm - current.bpm) < minTempoChangeBpm)
        {
            current.bpm = current.bpm * 0.85f + point.bpm * 0.15f;
        }
        else if (point.timeSeconds - current.timeSeconds < minChangeSpacingSeconds
                 || static_cast<int> (collapsed.size()) > maxTempoChanges)
        {
            continue;
        }
        else
        {
            collapsed.push_back (point);
        }
    }

    analysis.map = std::move (collapsed);
}
}

float TempoAnalysis::bpmAtTime (double seconds) const
{
    if (map.empty())
        return globalBpm;

    auto bpm = map.front().bpm;

    for (const auto& point : map)
    {
        if (point.timeSeconds > seconds)
            break;

        bpm = point.bpm;
    }

    return bpm;
}

TempoAnalysis TempoAnalyzer::analyse (const juce::File& file, juce::AudioFormatManager& formatManager)
{
    TempoAnalysis result;
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr || reader->sampleRate <= 0.0)
        return result;

    const auto analysisSeconds = juce::jmin (600.0, reader->lengthInSamples / reader->sampleRate);
    const int hopSize = 1024;
    const int frames = juce::jmax (1, static_cast<int> ((analysisSeconds * reader->sampleRate) / hopSize));
    juce::AudioBuffer<float> temp (juce::jmin (2, static_cast<int> (reader->numChannels)), hopSize);
    std::vector<float> energy;
    energy.reserve (static_cast<size_t> (frames));

    for (int frame = 0; frame < frames; ++frame)
    {
        temp.clear();
        reader->read (&temp, 0, hopSize, static_cast<juce::int64> (frame) * hopSize, true, true);

        float sum = 0.0f;
        for (int channel = 0; channel < temp.getNumChannels(); ++channel)
        {
            const auto* data = temp.getReadPointer (channel);
            for (int sample = 0; sample < hopSize; ++sample)
                sum += data[sample] * data[sample];
        }

        energy.push_back (std::sqrt (sum / static_cast<float> (hopSize * temp.getNumChannels())));
    }

    if (energy.size() < 8)
        return result;

    for (size_t i = energy.size() - 1; i > 0; --i)
        energy[i] = juce::jmax (0.0f, energy[i] - energy[i - 1]);

    const auto framesPerSecond = static_cast<float> (reader->sampleRate) / static_cast<float> (hopSize);
    const auto findBestBpm = [&energy, framesPerSecond] (int beginFrame, int endFrame)
    {
        beginFrame = juce::jlimit (0, static_cast<int> (energy.size()) - 1, beginFrame);
        endFrame = juce::jlimit (beginFrame + 1, static_cast<int> (energy.size()), endFrame);

        float bestBpm = 120.0f;
        float bestScore = -1.0f;

        for (float bpm = 70.0f; bpm <= 190.0f; bpm += 0.5f)
        {
            const auto lag = static_cast<int> (std::round ((60.0f / bpm) * framesPerSecond));

            if (lag <= 0 || beginFrame + lag >= endFrame)
                continue;

            float score = 0.0f;
            for (int i = beginFrame + lag; i < endFrame; ++i)
                score += energy[static_cast<size_t> (i)] * energy[static_cast<size_t> (i - lag)];

            if (score > bestScore)
            {
                bestScore = score;
                bestBpm = bpm;
            }
        }

        return bestBpm;
    };

    result.globalBpm = findBestBpm (0, static_cast<int> (energy.size()));

    const auto windowFrames = static_cast<int> (24.0f * framesPerSecond);
    const auto stepFrames = static_cast<int> (4.0f * framesPerSecond);

    for (int start = 0; start < static_cast<int> (energy.size()); start += juce::jmax (1, stepFrames))
    {
        const auto end = juce::jmin (static_cast<int> (energy.size()), start + windowFrames);

        if (end - start < static_cast<int> (8.0f * framesPerSecond))
            break;

        const auto localBpm = findBestBpm (start, end);
        result.map.push_back ({ static_cast<double> (start) / framesPerSecond,
                                result.globalBpm * 0.28f + localBpm * 0.72f });
    }

    if (result.map.empty())
        result.map.push_back ({ 0.0, result.globalBpm });

    stabilizeTempoMap (result);
    return result;
}
