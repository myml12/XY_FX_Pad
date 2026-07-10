#include "XYPad.h"

XYPad::XYPad()
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void XYPad::setAxisLabels (juce::String xText, juce::String yText)
{
    xLabel = std::move (xText);
    yLabel = std::move (yText);
    repaint();
}

void XYPad::setExternalPosition (float x, float y, bool down, bool notifyListeners)
{
    xValue = juce::jlimit (0.0f, 1.0f, x);
    yValue = juce::jlimit (0.0f, 1.0f, y);
    pointerDown = down;

    if (notifyListeners)
        notify();

    repaint();
}

void XYPad::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const auto centreX = bounds.getX() + bounds.getWidth() * xValue;
    const auto centreY = bounds.getBottom() - bounds.getHeight() * yValue;

    g.fillAll (juce::Colour (0xff101217));

    juce::ColourGradient gradient (juce::Colour (0xff2f7adf), bounds.getX(), bounds.getCentreY(),
                                   juce::Colour (0xffd45c46), bounds.getRight(), bounds.getCentreY(), false);
    gradient.addColour (0.5, juce::Colour (0xff323742));
    g.setGradientFill (gradient);
    g.fillRoundedRectangle (bounds, 8.0f);

    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.fillRoundedRectangle (bounds.reduced (8.0f), 6.0f);

    g.setColour (juce::Colours::white.withAlpha (0.16f));
    for (int i = 1; i < 4; ++i)
    {
        const auto x = bounds.getX() + bounds.getWidth() * static_cast<float> (i) / 4.0f;
        const auto y = bounds.getY() + bounds.getHeight() * static_cast<float> (i) / 4.0f;
        g.drawVerticalLine (juce::roundToInt (x), bounds.getY() + 8.0f, bounds.getBottom() - 8.0f);
        g.drawHorizontalLine (juce::roundToInt (y), bounds.getX() + 8.0f, bounds.getRight() - 8.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (0.78f));
    g.drawLine (bounds.getCentreX(), bounds.getY() + 10.0f, bounds.getCentreX(), bounds.getBottom() - 10.0f, 2.0f);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (xLabel, bounds.withHeight (30.0f).reduced (10.0f, 4.0f), juce::Justification::centredLeft);
    g.drawText (yLabel, bounds.withY (8.0f).reduced (0.0f, 4.0f), juce::Justification::centredTop);

    g.setColour (juce::Colours::white.withAlpha (pointerDown ? 0.95f : 0.55f));
    g.fillEllipse (centreX - 11.0f, centreY - 11.0f, 22.0f, 22.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawEllipse (centreX - 11.0f, centreY - 11.0f, 22.0f, 22.0f, 2.0f);
}

void XYPad::mouseDown (const juce::MouseEvent& event)
{
    pointerDown = true;
    updateFromMouse (event.position);
}

void XYPad::mouseDrag (const juce::MouseEvent& event)
{
    updateFromMouse (event.position);
}

void XYPad::mouseUp (const juce::MouseEvent& event)
{
    updateFromMouse (event.position);
    pointerDown = false;
    notify();
    repaint();
}

void XYPad::updateFromMouse (juce::Point<float> position)
{
    const auto b = getLocalBounds().toFloat();
    xValue = juce::jlimit (0.0f, 1.0f, (position.x - b.getX()) / b.getWidth());
    yValue = juce::jlimit (0.0f, 1.0f, 1.0f - ((position.y - b.getY()) / b.getHeight()));
    notify();
    repaint();
}

void XYPad::notify()
{
    if (onPadChanged != nullptr)
        onPadChanged (xValue, yValue, pointerDown);
}
