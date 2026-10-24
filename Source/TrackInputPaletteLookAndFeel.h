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
    void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator,
                                   int standardMenuItemHeight, int& idealWidth,
                                   int& idealHeight) override
    {
        juce::LookAndFeel_V4::getIdealPopupMenuItemSize(text, isSeparator,
            standardMenuItemHeight, idealWidth, idealHeight);
        if (!isSeparator)
        {
            idealHeight = juce::jmax(38, idealHeight);
            idealWidth = juce::jmax(190, idealWidth + 24);
        }
    }

    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override
    {
        if (isSeparator)
        {
            g.setColour(juce::Colour(0xff405665));
            g.fillRect(area.reduced(14, 3).withHeight(1));
            return;
        }
        const auto tile = area.reduced(7, 3).toFloat();
        g.setColour(isHighlighted ? juce::Colour(0xff28606c)
                                  : isTicked ? juce::Colour(0xff27444d)
                                             : juce::Colour(0xff203746));
        g.fillRoundedRectangle(tile, 7.0f);
        if (isTicked)
        {
            g.setColour(juce::Colour(0xff54d6dc));
            g.drawRoundedRectangle(tile, 7.0f, 1.5f);
        }
        g.setColour(isActive ? (isTicked ? juce::Colour(0xffffdf99)
                                         : juce::Colour(0xffe8f3fa))
                             : juce::Colour(0xff8296a3));
        g.setFont(juce::Font(14.0f, isTicked ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText(text, area.reduced(18, 3).withTrimmedRight(18),
                         juce::Justification::centredLeft, 1);
        if (isTicked)
        {
            g.setColour(juce::Colour(0xff54d6dc));
            g.drawText(juce::String::fromUTF8("✓"), area.withX(area.getRight() - 25).withWidth(25),
                       juce::Justification::centred, false);
        }
        if (hasSubMenu)
        {
            g.setColour(juce::Colour(0xff54d6dc));
            g.drawText(">", area.withX(area.getRight() - 16).withWidth(16),
                       juce::Justification::centred, false);
        }
        juce::ignoreUnused(shortcutKeyText, icon, textColour);
    }
};
inline TrackInputPaletteLookAndFeel& libertyTrackInputPaletteStyle()
{
    static TrackInputPaletteLookAndFeel style;
    return style;
}
