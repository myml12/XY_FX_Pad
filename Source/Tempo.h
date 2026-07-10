#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <vector>

struct TempoPoint
{
    double timeSeconds = 0.0;
    float bpm = 120.0f;
};

struct TempoAnalysis
{
    float globalBpm = 120.0f;
    std::vector<TempoPoint> map;

    float bpmAtTime (double seconds) const;
};

class TempoAnalyzer
{
public:
    static TempoAnalysis analyse (const juce::File& file, juce::AudioFormatManager& formatManager);
};
