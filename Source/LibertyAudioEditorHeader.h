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
    static void paintSelectionButtons(juce::Graphics& g, juce::Rectangle<int> inner)
    {
        const bool compact = inner.getWidth() < 600;
        const int toolbarWidth = juce::jmin(280, juce::jmax(0, inner.getWidth()));
        const auto bar = juce::Rectangle<int>(inner.getRight() - toolbarWidth,
            inner.getY() + (compact ? 44 : 22), toolbarWidth, 21);
        const char* labels[] = { "TOUT", "EFFACER", "DÉBUT", "FIN" };
        const char* shortLabels[] = { "T", "X", "D", "F" };
        for (int index = 0; index < 4; ++index)
        {
            const int width = bar.getWidth() / 4;
            const auto button = bar.withX(bar.getX() + index * width)
                                   .withWidth(juce::jmax(0, width - 3));
            g.setColour(juce::Colour(0xff203b4d));
            g.fillRoundedRectangle(button.toFloat(), 3.0f);
            g.setColour(juce::Colour(0xff83bed4));
            g.drawRoundedRectangle(button.toFloat(), 3.0f, 1.0f);
            g.setFont(juce::Font(10.0f, juce::Font::bold));
            g.drawText(width >= 48 ? labels[index] : shortLabels[index],
                       button, juce::Justification::centred);
        }
    }
    static void paintTimeFormat(juce::Graphics& g, juce::Rectangle<int> bounds,
                                int format)
    {
        const char* names[] = { "SECONDES", "MIN:SEC", "ÉCHANTILLONS", "MESURES" };
        g.setColour(juce::Colour(0xff203b4d));
        g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
        g.setColour(juce::Colour(0xff83bed4));
        g.drawRoundedRectangle(bounds.toFloat(), 3.0f, 1.0f);
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText(juce::String(names[juce::jlimit(0, 3, format)]),
                   bounds, juce::Justification::centred);
    }
};
