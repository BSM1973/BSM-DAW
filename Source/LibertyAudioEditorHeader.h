#pragma once
#include <JuceHeader.h>

// Identical title and zoom help for the dock and floating audio editor.
// Coordinates are local to the supplied content rectangle.
struct LibertyAudioEditorHeader
{
    static void paint(juce::Graphics& g, juce::Rectangle<int> area,
                      const juce::String& trackName, double zoom)
    {
        const auto inner = area.reduced(14, 12);
        g.setColour(juce::Colour(0xffdcebf4));
        g.setFont(juce::Font(15.0f, juce::Font::bold));
        g.drawText("ÉDITEUR AUDIO  |  " + trackName,
                   inner.getX(), inner.getY(), inner.getWidth(), 24,
                   juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff92b7c8));
        g.setFont(juce::Font(11.0f));
        const bool compact = inner.getWidth() < 600;
        const int toolbarWidth = juce::jmin(280, juce::jmax(0, inner.getWidth()));
        const int helpWidth = compact ? inner.getWidth()
            : juce::jmax(0, inner.getWidth() - toolbarWidth - 12);
        g.drawText("Molette : zoom  |  Maj + molette : déplacement  |  Zoom "
                       + juce::String(zoom, 1) + "x",
                   inner.getX(), inner.getY() + 23, helpWidth, 18,
                   juce::Justification::centredLeft, true);
    }
};
