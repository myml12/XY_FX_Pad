#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>

/** 荷重（筆圧）を音量メーター風に表示。ピークホールドで「メモリ」を残す。 */
class PressureMeter final : public juce::Component,
                            private juce::Timer
{
public:
    PressureMeter()
    {
        startTimerHz (30);
    }

    void setLevel (float normalised01)
    {
        level.store (juce::jlimit (0.0f, 1.0f, normalised01));
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff101217));
        g.fillRoundedRectangle (bounds, 6.0f);

        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

        auto meter = bounds.reduced (5.0f, 8.0f);
        const auto labelH = 16.0f;
        auto labelArea = meter.removeFromBottom (labelH);
        meter.removeFromBottom (4.0f);

        // 背景トラック
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (meter, 4.0f);

        const auto track = meter;
        const auto fillH = track.getHeight() * displayLevel;
        const auto fill = track.withTrimmedTop (track.getHeight() - fillH);

        juce::ColourGradient grad (juce::Colour (0xff3d9a6a), fill.getCentreX(), track.getBottom(),
                                   juce::Colour (0xffe8c547), fill.getCentreX(), track.getY(), false);
        grad.addColour (0.75, juce::Colour (0xffe07a3a));
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, 4.0f);

        // ピークホールド線（メモリ）
        if (peakHold > 0.02f)
        {
            const auto peakY = track.getBottom() - track.getHeight() * peakHold;
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.fillRect (track.getX(), peakY - 1.0f, track.getWidth(), 2.0f);
        }

        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("PRESS", labelArea, juce::Justification::centred);
    }

private:
    void timerCallback() override
    {
        const auto target = level.load();
        // 立ち上がりは速く、立ち下がりは少し残す
        if (target > displayLevel)
            displayLevel = target;
        else
            displayLevel += (target - displayLevel) * 0.25f;

        if (displayLevel > peakHold)
        {
            peakHold = displayLevel;
            peakHoldTicks = 18; // ~0.6s hold @ 30Hz
        }
        else if (peakHoldTicks > 0)
        {
            --peakHoldTicks;
        }
        else
        {
            peakHold *= 0.92f; // ゆっくり減衰するメモリ
        }

        repaint();
    }

    std::atomic<float> level { 0.0f };
    float displayLevel = 0.0f;
    float peakHold = 0.0f;
    int peakHoldTicks = 0;
};
