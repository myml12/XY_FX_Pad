#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <vector>

enum class EffectType
{
    gate = 1,
    echo,
    reverb,
    filter,
    lowPass,
    highPass,
    bandPass,
    notch,
    flanger,
    phaser,
    tremolo,
    drive,
    bitCrusher,
    roll
};

juce::String effectName (EffectType effect);

struct DelayBuffer
{
    juce::AudioBuffer<float> buffer;
    int writePosition = 0;

    void prepare (double sampleRate, double seconds);
    void clear();
    float read (int channel, int delaySamples) const;
    float readFractional (int channel, float delaySamples) const;
    void write (int channel, float value);
    void advance();
};

class EffectProcessor
{
public:
    void prepare (int samplesPerBlockExpected, double sampleRate);
    void resetAll();
    void resetMomentary();
    void writeRollHistory (const juce::AudioSourceChannelInfo& info);
    void process (const juce::AudioSourceChannelInfo& info, EffectType effect, float amount, float bpm);

private:
    static float smoothstep (float edge0, float edge1, float value);
    static float pickSteppedValue (float x, const std::vector<float>& values);

    void applyGate (const juce::AudioSourceChannelInfo& info, float amount, float bpm);
    void applyEcho (const juce::AudioSourceChannelInfo& info, float amount, float bpm);
    void applyReverb (const juce::AudioSourceChannelInfo& info, float amount);
    void applyFilter (const juce::AudioSourceChannelInfo& info, float amount);
    void applyLowPass (const juce::AudioSourceChannelInfo& info, float amount);
    void applyHighPass (const juce::AudioSourceChannelInfo& info, float amount);
    void applyBandPass (const juce::AudioSourceChannelInfo& info, float amount);
    void applyNotch (const juce::AudioSourceChannelInfo& info, float amount);
    void applyFlanger (const juce::AudioSourceChannelInfo& info, float amount);
    void applyPhaser (const juce::AudioSourceChannelInfo& info, float amount);
    void applyTremolo (const juce::AudioSourceChannelInfo& info, float amount);
    void applyDrive (const juce::AudioSourceChannelInfo& info, float amount);
    void applyBitCrusher (const juce::AudioSourceChannelInfo& info, float amount);
    void applyRoll (const juce::AudioSourceChannelInfo& info, float amount, float bpm);

    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                   juce::dsp::IIR::Coefficients<float>> filter
        { juce::dsp::IIR::Coefficients<float>::makeAllPass (44100.0, 1000.0) };
    juce::dsp::Reverb reverb;
    juce::dsp::Phaser<float> phaser;

    DelayBuffer echoDelay;
    DelayBuffer flangerDelay;
    DelayBuffer rollBuffer;

    double currentSampleRate = 0.0;
    float gatePhase = 0.0f;
    float flangerPhase = 0.0f;
    float tremoloPhase = 0.0f;
    int rollReadOffset = 0;
};
