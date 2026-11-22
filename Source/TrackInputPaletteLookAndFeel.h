#pragma once
#include "LibertyObsidianMenuStyle.h"

// Input selectors retain their larger touch targets, but use the same menu
// painter as the main menu, context menus and the other combo boxes.
class TrackInputPaletteLookAndFeel final : public LibertyUI::ObsidianMenuStyle
{
public:
    void getIdealPopupMenuItemSize(const juce::String& text,bool separator,
        int standardHeight,int& width,int& height) override
    {
        LibertyUI::ObsidianMenuStyle::getIdealPopupMenuItemSize(text,separator,standardHeight,width,height);
        if(!separator){height=juce::jmax(38,height);width=juce::jmax(190,width);}
    }
};
inline TrackInputPaletteLookAndFeel& libertyTrackInputPaletteStyle()
{
    static TrackInputPaletteLookAndFeel style;
    return style;
}
