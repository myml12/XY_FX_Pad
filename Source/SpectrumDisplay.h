#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>

/** 最終出力をFFT解析して、低域〜高域のエネルギーを常時表示する。 */
class SpectrumDisplay final : public juce::Component,
                              private juce::Timer
{
public:
    static constexpr int fftOrder = 10;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int barCount = 48;

    SpectrumDisplay()
        : fft (fftOrder), window (fftSize, juce::dsp::WindowingFunction<float>::hann)
    {
        startTimerHz (30);
    }

    /** audio callbackから呼ぶ。メモリ確保・ロック・FFT計算は行わない。 */
    void pushAudioBlock (const juce::AudioSourceChannelInfo& info)
    {
        if (info.buffer == nullptr || nextBlockReady.load (std::memory_order_acquire))
            return;

        const auto channels = info.buffer->getNumChannels();

        for (int sample = 0; sample < info.numSamples; ++sample)
        {
            float mono = 0.0f;
            for (int channel = 0; channel < channels; ++channel)
                mono += info.buffer->getSample (channel, info.startSample + sample);

            fifo[static_cast<size_t> (fifoIndex++)] = mono / static_cast<float> (juce::jmax (1, channels));

            if (fifoIndex == fftSize)
            {
                std::copy (fifo.begin(), fifo.end(), fftData.begin());
                fifoIndex = 0;
                nextBlockReady.store (true, std::memory_order_release);
                return;
            }
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff101214));
        g.fillRoundedRectangle (bounds, 14.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (bounds, 14.0f, 1.0f);

        auto graph = bounds.reduced (14.0f, 12.0f);
        auto labelArea = graph.removeFromTop (16.0f);
        g.setColour (juce::Colours::white.withAlpha (0.62f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("LIVE SPECTRUM", labelArea, juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("LOW", labelArea, juce::Justification::centredRight);
        g.drawText ("HIGH", labelArea.withTrimmedRight (34.0f), juce::Justification::centredRight);

        for (int line = 1; line < 3; ++line)
        {
            const auto y = graph.getY() + graph.getHeight() * static_cast<float> (line) / 3.0f;
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.drawHorizontalLine (juce::roundToInt (y), graph.getX(), graph.getRight());
        }

        const auto barWidth = graph.getWidth() / static_cast<float> (barCount);
        for (int bar = 0; bar < barCount; ++bar)
        {
            const auto level = displayedLevels[static_cast<size_t> (bar)];
            const auto height = graph.getHeight() * level;
            auto rect = juce::Rectangle<float> (graph.getX() + barWidth * static_cast<float> (bar) + 1.0f,
                                                graph.getBottom() - height,
                                                juce::jmax (1.0f, barWidth - 2.0f), height);
            g.setColour (juce::Colour (0xffc6ef73).withAlpha (0.25f + level * 0.70f));
            g.fillRoundedRectangle (rect, 1.5f);
        }
    }

private:
    void timerCallback() override
    {
        if (nextBlockReady.load (std::memory_order_acquire))
        {
            window.multiplyWithWindowingTable (fftData.data(), fftSize);
            fft.performFrequencyOnlyForwardTransform (fftData.data());

            for (int bar = 0; bar < barCount; ++bar)
            {
                const auto normalised = static_cast<float> (bar) / static_cast<float> (barCount);
                const auto nextNormalised = static_cast<float> (bar + 1) / static_cast<float> (barCount);
                const auto startBin = juce::jlimit (1, fftSize / 2 - 1,
                                                    static_cast<int> (std::pow (fftSize / 2.0f, normalised)));
                const auto endBin = juce::jlimit (startBin + 1, fftSize / 2,
                                                  static_cast<int> (std::pow (fftSize / 2.0f, nextNormalised)));
                float magnitude = 0.0f;

                for (int bin = startBin; bin < endBin; ++bin)
                    magnitude = juce::jmax (magnitude, fftData[static_cast<size_t> (bin)]);

                const auto target = juce::jlimit (0.0f, 1.0f, juce::Decibels::gainToDecibels (magnitude / fftSize, -72.0f)
                                                               / 72.0f + 1.0f);
                displayedLevels[static_cast<size_t> (bar)] = juce::jmax (target,
                    displayedLevels[static_cast<size_t> (bar)] * 0.82f);
            }

            // FFTデータの読取りが終わってから、audio callbackに次のブロックを書かせる。
            nextBlockReady.store (false, std::memory_order_release);
        }
        else
        {
            for (auto& level : displayedLevels)
                level *= 0.94f;
        }

        repaint();
    }

    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::array<float, fftSize> fifo {};
    std::array<float, fftSize * 2> fftData {};
    std::array<float, barCount> displayedLevels {};
    int fifoIndex = 0;
    std::atomic<bool> nextBlockReady { false };
};
