#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Shared dark palette styling for audio and MIDI track-header selectors.
class TrackInputPaletteLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    TrackInputPaletteLookAndFeel()
    {
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff142633));
        setColour(juce::PopupMenu::textColourId, juce::Colour(0xffe8f3fa));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff28606c));
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(0xffffdf99));
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff233746));
        setColour(juce::ComboBox::textColourId, juce::Colour(0xffffdf99));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff527b89));
        setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff54d6dc));
    }
    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override
    {
        g.fillAll(juce::Colour(0xff142633));
        g.setColour(juce::Colour(0xff527b89));
        g.drawRect(0, 0, width, height, 1);
    }
    juce::Font getPopupMenuFont() override { return juce::Font(14.0f); }
};
inline TrackInputPaletteLookAndFeel& libertyTrackInputPaletteStyle()
{
    static TrackInputPaletteLookAndFeel style;
    return style;
}
