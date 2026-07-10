#include "Effects.h"

#include <algorithm>
#include <cmath>

juce::String effectName (EffectType effect)
{
    switch (effect)
    {
        case EffectType::gate:       return "Gate";
        case EffectType::echo:       return "Echo";
        case EffectType::reverb:     return "Reverb";
        case EffectType::filter:     return "Filter";
        case EffectType::lowPass:    return "Low Pass";
        case EffectType::highPass:   return "High Pass";
        case EffectType::bandPass:   return "Band Pass";
        case EffectType::notch:      return "Notch";
        case EffectType::flanger:    return "Flanger";
        case EffectType::phaser:     return "Phaser";
        case EffectType::tremolo:    return "Tremolo";
        case EffectType::drive:      return "Drive";
        case EffectType::bitCrusher: return "BitCrusher";
        case EffectType::roll:       return "Roll";
    }

    return "Unknown";
}

void DelayBuffer::prepare (double sampleRate, double seconds)
{
    buffer.setSize (2, juce::jmax (2, static_cast<int> (sampleRate * seconds)));
    clear();
}

void DelayBuffer::clear()
{
    buffer.clear();
    writePosition = 0;
}

float DelayBuffer::read (int channel, int delaySamples) const
{
    const auto size = buffer.getNumSamples();
    const auto readPosition = (writePosition + size - juce::jlimit (1, size - 1, delaySamples)) % size;
    return buffer.getSample (juce::jmin (channel, buffer.getNumChannels() - 1), readPosition);
}

float DelayBuffer::readFractional (int channel, float delaySamples) const
{
    const auto size = buffer.getNumSamples();
    auto readPosition = static_cast<float> (writePosition)
                        - juce::jlimit (1.0f, static_cast<float> (size - 2), delaySamples);

    while (readPosition < 0.0f)
        readPosition += static_cast<float> (size);

    const auto index0 = static_cast<int> (readPosition) % size;
    const auto index1 = (index0 + 1) % size;
    const auto frac = readPosition - static_cast<float> (index0);
    const auto safeChannel = juce::jmin (channel, buffer.getNumChannels() - 1);
    return juce::jmap (frac, buffer.getSample (safeChannel, index0), buffer.getSample (safeChannel, index1));
}

void DelayBuffer::write (int channel, float value)
{
    buffer.setSample (juce::jmin (channel, buffer.getNumChannels() - 1), writePosition, value);
}

void DelayBuffer::advance()
{
    writePosition = (writePosition + 1) % buffer.getNumSamples();
}

void EffectProcessor::prepare (int samplesPerBlockExpected, double sampleRate)
{
    currentSampleRate = sampleRate;

    const juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (samplesPerBlockExpected), 2 };
    reverb.prepare (spec);
    phaser.prepare (spec);
    filter.reset();
    filter.prepare (spec);

    echoDelay.prepare (sampleRate, 2.2);
    flangerDelay.prepare (sampleRate, 0.08);
    rollBuffer.prepare (sampleRate, 8.0);
    resetAll();
}

void EffectProcessor::resetAll()
{
    echoDelay.clear();
    flangerDelay.clear();
    rollBuffer.clear();
    reverb.reset();
    phaser.reset();
    gatePhase = 0.0f;
    flangerPhase = 0.0f;
    tremoloPhase = 0.0f;
    rollReadOffset = 0;
}

void EffectProcessor::resetMomentary()
{
    echoDelay.clear();
    reverb.reset();
    phaser.reset();
    gatePhase = 0.0f;
    tremoloPhase = 0.0f;
    rollReadOffset = 0;
}

void EffectProcessor::writeRollHistory (const juce::AudioSourceChannelInfo& info)
{
    for (int sample = 0; sample < info.numSamples; ++sample)
    {
        for (int channel = 0; channel < juce::jmin (info.buffer->getNumChannels(), rollBuffer.buffer.getNumChannels()); ++channel)
            rollBuffer.write (channel, info.buffer->getSample (channel, info.startSample + sample));

        rollBuffer.advance();
    }
}

void EffectProcessor::process (const juce::AudioSourceChannelInfo& info, EffectType effect, float amount, float bpm)
{
    bpm = juce::jlimit (70.0f, 190.0f, bpm);

    switch (effect)
    {
        case EffectType::gate:       applyGate (info, amount, bpm); break;
        case EffectType::echo:       applyEcho (info, amount, bpm); break;
        case EffectType::reverb:     applyReverb (info, amount); break;
        case EffectType::filter:     applyFilter (info, amount); break;
        case EffectType::lowPass:    applyLowPass (info, amount); break;
        case EffectType::highPass:   applyHighPass (info, amount); break;
        case EffectType::bandPass:   applyBandPass (info, amount); break;
        case EffectType::notch:      applyNotch (info, amount); break;
        case EffectType::flanger:    applyFlanger (info, amount); break;
        case EffectType::phaser:     applyPhaser (info, amount); break;
        case EffectType::tremolo:    applyTremolo (info, amount); break;
        case EffectType::drive:      applyDrive (info, amount); break;
        case EffectType::bitCrusher: applyBitCrusher (info, amount); break;
        case EffectType::roll:       applyRoll (info, amount, bpm); break;
    }
}

float EffectProcessor::smoothstep (float edge0, float edge1, float value)
{
    const auto x = juce::jlimit (0.0f, 1.0f, (value - edge0) / (edge1 - edge0));
    return x * x * (3.0f - 2.0f * x);
}

float EffectProcessor::pickSteppedValue (float x, const std::vector<float>& values)
{
    const auto index = juce::jlimit (0, static_cast<int> (values.size()) - 1,
                                     static_cast<int> (std::floor (x * static_cast<float> (values.size()))));
    return values[static_cast<size_t> (index)];
}

void EffectProcessor::applyGate (const juce::AudioSourceChannelInfo& info, float amount, float bpm)
{
    const auto periodBeats = 0.25f;
    const auto periodSeconds = 60.0f / bpm * periodBeats;
    const auto phaseDelta = 1.0f / static_cast<float> (currentSampleRate * periodSeconds);
    const auto response = smoothstep (0.0f, 0.8f, amount);
    const auto wet = juce::jmap (response, 0.0f, 1.0f, 0.18f, 1.0f);
    const auto floorGain = juce::jmap (response, 0.0f, 1.0f, 0.48f, 0.02f);
    const auto duty = juce::jmap (response, 0.0f, 1.0f, 0.68f, 0.34f);
    const auto edge = 0.025f;

    for (int sample = 0; sample < info.numSamples; ++sample)
    {
        gatePhase += phaseDelta;
        if (gatePhase >= 1.0f)
            gatePhase -= 1.0f;

        const auto openEdge = smoothstep (0.0f, edge, gatePhase);
        const auto closeEdge = 1.0f - smoothstep (duty, duty + edge, gatePhase);
        const auto gate = floorGain + (1.0f - floorGain) * juce::jmin (openEdge, closeEdge);
        const auto gain = juce::jmap (wet, 1.0f, gate);

        for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel)
            info.buffer->getWritePointer (channel, info.startSample)[sample] *= gain;
    }
}

void EffectProcessor::applyEcho (const juce::AudioSourceChannelInfo& info, float amount, float bpm)
{
    const auto delayBeats = pickSteppedValue (amount, { 0.0625f, 0.125f, 0.25f, 0.375f, 0.5f, 0.75f, 1.0f, 2.0f });
    const auto delaySamples = static_cast<int> ((60.0f / bpm) * delayBeats * currentSampleRate);
    const auto feedback = juce::jmap (smoothstep (0.0f, 1.0f, amount), 0.0f, 1.0f, 0.08f, 0.82f);
    const auto wet = juce::jmap (amount, 0.0f, 1.0f, 0.28f, 0.72f);

    for (int sample = 0; sample < info.numSamples; ++sample)
    {
        for (int channel = 0; channel < juce::jmin (info.buffer->getNumChannels(), echoDelay.buffer.getNumChannels()); ++channel)
        {
            auto* data = info.buffer->getWritePointer (channel, info.startSample);
            const auto input = data[sample];
            const auto delayed = echoDelay.read (channel, delaySamples);
            data[sample] = input * (1.0f - wet * 0.35f) + delayed * wet;
            echoDelay.write (channel, input + delayed * feedback);
        }

        echoDelay.advance();
    }
}

void EffectProcessor::applyReverb (const juce::AudioSourceChannelInfo& info, float amount)
{
    juce::dsp::Reverb::Parameters params;
    params.roomSize = juce::jmap (smoothstep (0.0f, 1.0f, amount), 0.0f, 1.0f, 0.18f, 0.98f);
    params.damping = juce::jmap (amount, 0.0f, 1.0f, 0.62f, 0.18f);
    params.wetLevel = juce::jmap (smoothstep (0.0f, 1.0f, amount), 0.0f, 1.0f, 0.0f, 0.68f);
    params.dryLevel = 1.0f - params.wetLevel * 0.35f;
    params.width = 1.0f;
    reverb.setParameters (params);

    auto block = juce::dsp::AudioBlock<float> (*info.buffer)
                     .getSubBlock (static_cast<size_t> (info.startSample),
                                   static_cast<size_t> (info.numSamples));
    juce::dsp::ProcessContextReplacing<float> context (block);
    reverb.process (context);
}

void EffectProcessor::applyFilter (const juce::AudioSourceChannelInfo& info, float amount)
{
    const auto q = 2.6f;

    if (amount < 0.48f)
    {
        const auto normalised = juce::jmap (amount, 0.0f, 0.48f, 0.0f, 1.0f);
        const auto cutoff = juce::jmap (normalised * normalised, 90.0f, 19000.0f);
        *filter.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (currentSampleRate, cutoff, q);
    }
    else if (amount > 0.52f)
    {
        const auto normalised = juce::jmap (amount, 0.52f, 1.0f, 0.0f, 1.0f);
        const auto cutoff = juce::jmap (normalised * normalised, 25.0f, 7600.0f);
        *filter.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, cutoff, q);
    }
    else
    {
        *filter.state = *juce::dsp::IIR::Coefficients<float>::makeAllPass (currentSampleRate, 1000.0f);
    }

    auto block = juce::dsp::AudioBlock<float> (*info.buffer)
                     .getSubBlock (static_cast<size_t> (info.startSample),
                                   static_cast<size_t> (info.numSamples));
    juce::dsp::ProcessContextReplacing<float> context (block);
    filter.process (context);
}

void EffectProcessor::applyLowPass (const juce::AudioSourceChannelInfo& info, float amount)
{
    const auto cutoff = juce::jmap (amount * amount, 180.0f, 19000.0f);
    *filter.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass (currentSampleRate, cutoff, 1.25f);

    auto block = juce::dsp::AudioBlock<float> (*info.buffer)
                     .getSubBlock (static_cast<size_t> (info.startSample),
                                   static_cast<size_t> (info.numSamples));
    juce::dsp::ProcessContextReplacing<float> context (block);
    filter.process (context);
}

void EffectProcessor::applyHighPass (const juce::AudioSourceChannelInfo& info, float amount)
{
    const auto cutoff = juce::jmap (amount * amount, 24.0f, 8200.0f);
    *filter.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, cutoff, 1.25f);

    auto block = juce::dsp::AudioBlock<float> (*info.buffer)
                     .getSubBlock (static_cast<size_t> (info.startSample),
                                   static_cast<size_t> (info.numSamples));
    juce::dsp::ProcessContextReplacing<float> context (block);
    filter.process (context);
}

void EffectProcessor::applyBandPass (const juce::AudioSourceChannelInfo& info, float amount)
{
    const auto cutoff = juce::jmap (amount * amount, 120.0f, 12000.0f);
    *filter.state = *juce::dsp::IIR::Coefficients<float>::makeBandPass (currentSampleRate, cutoff, 1.25f);

    auto block = juce::dsp::AudioBlock<float> (*info.buffer)
                     .getSubBlock (static_cast<size_t> (info.startSample),
                                   static_cast<size_t> (info.numSamples));
    juce::dsp::ProcessContextReplacing<float> context (block);
    filter.process (context);

    for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel)
        info.buffer->applyGain (channel, info.startSample, info.numSamples, 2.2f);
}

void EffectProcessor::applyNotch (const juce::AudioSourceChannelInfo& info, float amount)
{
    const auto cutoff = juce::jmap (amount * amount, 140.0f, 12000.0f);
    *filter.state = *juce::dsp::IIR::Coefficients<float>::makeNotch (currentSampleRate, cutoff, 1.1f);

    auto block = juce::dsp::AudioBlock<float> (*info.buffer)
                     .getSubBlock (static_cast<size_t> (info.startSample),
                                   static_cast<size_t> (info.numSamples));
    juce::dsp::ProcessContextReplacing<float> context (block);
    filter.process (context);
}

void EffectProcessor::applyFlanger (const juce::AudioSourceChannelInfo& info, float amount)
{
    const auto rate = juce::jmap (smoothstep (0.0f, 1.0f, amount), 0.0f, 1.0f, 0.08f, 6.5f);
    const auto depthMs = juce::jmap (smoothstep (0.0f, 1.0f, amount), 0.0f, 1.0f, 0.45f, 9.5f);
    const auto baseMs = 2.0f;
    const auto wet = juce::jmap (amount, 0.0f, 1.0f, 0.32f, 0.78f);

    for (int sample = 0; sample < info.numSamples; ++sample)
    {
        flangerPhase += juce::MathConstants<float>::twoPi * rate / static_cast<float> (currentSampleRate);
        if (flangerPhase >= juce::MathConstants<float>::twoPi)
            flangerPhase -= juce::MathConstants<float>::twoPi;

        const auto delaySamples = (baseMs + depthMs * (0.5f + 0.5f * std::sin (flangerPhase)))
                                  * 0.001f * static_cast<float> (currentSampleRate);

        for (int channel = 0; channel < juce::jmin (info.buffer->getNumChannels(), flangerDelay.buffer.getNumChannels()); ++channel)
        {
            auto* data = info.buffer->getWritePointer (channel, info.startSample);
            const auto input = data[sample];
            const auto delayed = flangerDelay.readFractional (channel, delaySamples);
            data[sample] = input * (1.0f - wet * 0.35f) + delayed * wet;
            flangerDelay.write (channel, input + delayed * 0.42f);
        }

        flangerDelay.advance();
    }
}

void EffectProcessor::applyPhaser (const juce::AudioSourceChannelInfo& info, float amount)
{
    phaser.setRate (juce::jmap (smoothstep (0.0f, 1.0f, amount), 0.0f, 1.0f, 0.12f, 9.0f));
    phaser.setDepth (0.95f);
    phaser.setCentreFrequency (juce::jmap (amount, 0.0f, 1.0f, 700.0f, 1800.0f));
    phaser.setFeedback (juce::jmap (smoothstep (0.0f, 1.0f, amount), 0.0f, 1.0f, 0.05f, 0.92f));
    phaser.setMix (0.78f);

    auto block = juce::dsp::AudioBlock<float> (*info.buffer)
                     .getSubBlock (static_cast<size_t> (info.startSample),
                                   static_cast<size_t> (info.numSamples));
    juce::dsp::ProcessContextReplacing<float> context (block);
    phaser.process (context);
}

void EffectProcessor::applyTremolo (const juce::AudioSourceChannelInfo& info, float amount)
{
    const auto depth = juce::jmap (smoothstep (0.0f, 1.0f, amount), 0.0f, 1.0f, 0.0f, 0.96f);
    const auto rate = juce::jmap (amount * amount, 0.0f, 1.0f, 0.75f, 18.0f);
    const auto phaseDelta = juce::MathConstants<float>::twoPi * rate / static_cast<float> (currentSampleRate);

    for (int sample = 0; sample < info.numSamples; ++sample)
    {
        const auto gain = 1.0f - depth * (0.5f + 0.5f * std::sin (tremoloPhase));
        tremoloPhase += phaseDelta;

        if (tremoloPhase >= juce::MathConstants<float>::twoPi)
            tremoloPhase -= juce::MathConstants<float>::twoPi;

        for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel)
            info.buffer->getWritePointer (channel, info.startSample)[sample] *= gain;
    }
}

void EffectProcessor::applyDrive (const juce::AudioSourceChannelInfo& info, float amount)
{
    const auto drive = juce::jmap (smoothstep (0.0f, 1.0f, amount), 0.0f, 1.0f, 1.0f, 7.5f);
    const auto wet = juce::jmap (amount, 0.0f, 1.0f, 0.0f, 0.72f);
    const auto outputTrim = juce::jmap (amount, 0.0f, 1.0f, 0.96f, 0.68f);

    for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel)
    {
        auto* data = info.buffer->getWritePointer (channel, info.startSample);

        for (int sample = 0; sample < info.numSamples; ++sample)
        {
            const auto input = data[sample];
            const auto driven = std::tanh (input * drive) * outputTrim;
            data[sample] = juce::jlimit (-0.98f, 0.98f, juce::jmap (wet, input, driven));
        }
    }
}

void EffectProcessor::applyBitCrusher (const juce::AudioSourceChannelInfo& info, float amount)
{
    const auto steps = juce::jmap (1.0f - smoothstep (0.0f, 1.0f, amount), 0.0f, 1.0f, 4.0f, 512.0f);
    const auto wet = juce::jlimit (0.0f, 1.0f, smoothstep (0.0f, 0.85f, amount));

    for (int channel = 0; channel < info.buffer->getNumChannels(); ++channel)
    {
        auto* data = info.buffer->getWritePointer (channel, info.startSample);

        for (int sample = 0; sample < info.numSamples; ++sample)
        {
            const auto input = data[sample];
            const auto crushed = std::round (input * steps) / steps;
            data[sample] = juce::jmap (wet, input, crushed);
        }
    }
}

void EffectProcessor::applyRoll (const juce::AudioSourceChannelInfo& info, float amount, float bpm)
{
    static constexpr std::array<float, 6> beatLengths { 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
    const auto index = juce::jlimit (0, static_cast<int> (beatLengths.size()) - 1,
                                     static_cast<int> (std::floor (amount * static_cast<float> (beatLengths.size()))));
    const auto beats = beatLengths[static_cast<size_t> (index)];
    const auto seconds = 60.0f / bpm * beats;
    const auto segmentSamples = juce::jlimit (64, rollBuffer.buffer.getNumSamples() - 2,
                                              static_cast<int> (seconds * currentSampleRate));
    const auto wet = juce::jlimit (0.0f, 1.0f, smoothstep (0.0f, 0.85f, amount));

    for (int sample = 0; sample < info.numSamples; ++sample)
    {
        const auto delay = segmentSamples - (rollReadOffset % segmentSamples);

        for (int channel = 0; channel < juce::jmin (info.buffer->getNumChannels(), rollBuffer.buffer.getNumChannels()); ++channel)
        {
            auto* data = info.buffer->getWritePointer (channel, info.startSample);
            const auto input = data[sample];
            const auto rolled = rollBuffer.read (channel, delay);
            data[sample] = juce::jmap (wet, input, rolled);
        }

        ++rollReadOffset;
    }
}
