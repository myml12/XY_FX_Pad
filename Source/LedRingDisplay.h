#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <atomic>
#include <cmath>

/**
    パッド外周に取り付けるアドレサブルLEDを、実機接続前に画面上で再現する。
    位置と筆圧だけを受け取るため、将来ESP32へ渡す演出ロジックの確認にも使える。
*/
class LedRingDisplay final : public juce::Component,
                              private juce::Timer
{
public:
    static constexpr int ledCount = 112;

    LedRingDisplay()
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (60);
    }

    void setSimulationEnabled (bool shouldEnable)
    {
        simulationEnabled.store (shouldEnable);
        repaint();
    }

    void setPerformanceState (float newX, float newY, float newPressure, bool isTouching)
    {
        x.store (juce::jlimit (0.0f, 1.0f, newX));
        y.store (juce::jlimit (0.0f, 1.0f, newY));
        pressure.store (juce::jlimit (0.0f, 1.0f, newPressure));
        touching.store (isTouching);
    }

    void paint (juce::Graphics& g) override
    {
        if (! simulationEnabled.load())
            return;

        const auto bounds = getLocalBounds().toFloat().reduced (3.0f);
        const auto ledBlue = juce::Colour (0xff58aeff);

        for (int index = 0; index < ledCount; ++index)
        {
            const auto point = pointForLed (bounds, index);
            const auto intensity = displayedEnergy[static_cast<size_t> (index)];

            // 消灯時にもLEDの筐体だけをかすかに描き、配置を確認できるようにする。
            g.setColour (juce::Colour (0xff0b0d0e).interpolatedWith (ledBlue,
                0.08f + intensity * 0.92f));
            g.fillRoundedRectangle (point.x - 3.5f, point.y - 2.5f, 7.0f, 5.0f, 1.5f);

            if (intensity > 0.08f)
            {
                g.setColour (ledBlue.withAlpha (0.10f + intensity * 0.22f));
                g.fillEllipse (point.x - 8.0f, point.y - 8.0f, 16.0f, 16.0f);
            }
        }
    }

private:

    static juce::Point<float> pointForLed (juce::Rectangle<float> bounds, int index)
    {
        constexpr int perSide = ledCount / 4;
        const auto side = index / perSide;
        const auto position = static_cast<float> (index % perSide) / static_cast<float> (perSide - 1);

        switch (side)
        {
            case 0: return { juce::jmap (position, bounds.getX(), bounds.getRight()), bounds.getY() };
            case 1: return { bounds.getRight(), juce::jmap (position, bounds.getY(), bounds.getBottom()) };
            case 2: return { juce::jmap (position, bounds.getRight(), bounds.getX()), bounds.getBottom() };
            default: return { bounds.getX(), juce::jmap (position, bounds.getBottom(), bounds.getY()) };
        }
    }

    // LED位置を盤面と同じ0..1座標へ変換する。重心に近いLEDほど強く光らせる。
    static juce::Point<float> normalisedPointForLed (int index)
    {
        constexpr int perSide = ledCount / 4;
        const auto side = index / perSide;
        const auto position = static_cast<float> (index % perSide) / static_cast<float> (perSide - 1);

        switch (side)
        {
            case 0: return { position, 0.0f };
            case 1: return { 1.0f, position };
            case 2: return { 1.0f - position, 1.0f };
            default: return { 0.0f, 1.0f - position };
        }
    }

    void timerCallback() override
    {
        const auto isEnabled = simulationEnabled.load();
        const auto isTouching = touching.load();
        const auto targetPressure = isTouching ? pressure.load() : 0.0f;

        // PressureMeterと同じ性格: 押した瞬間はすぐ上がり、離すと少し余韻を残す。
        // LEDは60Hzなので、30Hzのゲージの0.25減衰と同じ時間感覚になる係数を使う。
        if (targetPressure > displayedPressure)
            displayedPressure = targetPressure;
        else
            displayedPressure += (targetPressure - displayedPressure) * 0.134f;

        for (auto& energy : displayedEnergy)
            energy *= isTouching ? 0.905f : 0.875f; // 筆を離すと、墨のにじみのように早めに消える

        if (isEnabled && displayedPressure > 0.002f)
        {
            // ControllerBridgeは右上=raw0から反時計回りにraw1, raw2, raw3を扱い、
            // パッド座標のYは「上=1」。描画座標は「上=0」なので、LED用だけ反転する。
            const juce::Point<float> centreOfPressure { x.load(), 1.0f - y.load() };
            const auto radius = 0.20f + displayedPressure * 0.10f;
            const auto strength = displayedPressure;

            for (int index = 0; index < ledCount; ++index)
            {
                const auto ledPosition = normalisedPointForLed (index);
                const auto distance = centreOfPressure.getDistanceFrom (ledPosition);
                const auto falloff = static_cast<float> (
                    std::exp (-(distance * distance) / (2.0f * radius * radius)));
                displayedEnergy[static_cast<size_t> (index)] = juce::jmax (
                    displayedEnergy[static_cast<size_t> (index)], falloff * strength);
            }
        }

        repaint();
    }

    std::array<float, ledCount> displayedEnergy {};
    std::atomic<bool> simulationEnabled { true };
    std::atomic<float> x { 0.5f };
    std::atomic<float> y { 0.5f };
    std::atomic<float> pressure { 0.0f };
    std::atomic<bool> touching { false };
    float displayedPressure = 0.0f;
};
