#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

class XYPad final : public juce::Component
{
public:
    std::function<void (float x, float y, bool down)> onPadChanged;

    XYPad();
    void setAxisLabels (juce::String xText, juce::String yText);
    void setExternalPosition (float x, float y, bool down, bool notifyListeners);
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;

private:
    void updateFromMouse (juce::Point<float> position);
    void notify();

    float xValue = 0.5f;
    float yValue = 0.0f;
    bool pointerDown = false;
    juce::String xLabel = "Delay Time";
    juce::String yLabel = "Feedback";
};
