#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "LibertyDesignSystem.h"

// One native-JUCE menu style. Menu models, IDs, callbacks, access keys,
// icons, submenus and placement remain owned by JUCE and the caller.
namespace LibertyUI
{
class ObsidianMenuStyle : public juce::LookAndFeel_V4
{
public:
    ObsidianMenuStyle()
    {
        setColour(juce::PopupMenu::backgroundColourId, Colour::panel);
        setColour(juce::PopupMenu::textColourId, Colour::textPrimary);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, Colour::selection);
        setColour(juce::PopupMenu::highlightedTextColourId, Colour::accentLight);
        setColour(juce::ComboBox::backgroundColourId, Colour::control);
        setColour(juce::ComboBox::textColourId, Colour::textPrimary);
        setColour(juce::ComboBox::outlineColourId, Colour::divider);
        setColour(juce::ComboBox::arrowColourId, Colour::accent);
        setColour(juce::ComboBox::focusedOutlineColourId, Colour::accent);
    }
    juce::Font getPopupMenuFont() override { return juce::Font(14.f); }
    void getIdealPopupMenuItemSize(const juce::String& text,bool separator,
        int standardHeight,int& width,int& height) override
    {
        juce::LookAndFeel_V4::getIdealPopupMenuItemSize(text,separator,standardHeight,width,height);
        if(!separator){width=juce::jmax(150,width+16);height=juce::jmax(30,height);}
        else height=juce::jmax(7,height);
    }
    void drawPopupMenuBackground(juce::Graphics& g,int width,int height) override
    {
        if(width<=0||height<=0)return;
        g.setGradientFill(juce::ColourGradient(Colour::header,0.f,0.f,Colour::canvasDeep,0.f,(float)height,false));
        g.fillRect(0,0,width,height);
        g.setColour(Colour::divider);
        g.drawRect(0,0,width,height,1);
        accentRule(g,{1.f,1.f,(float)juce::jmax(0,width-2),1.f});
    }
    void drawPopupMenuItem(juce::Graphics& g,const juce::Rectangle<int>& area,
        bool separator,bool active,bool highlighted,bool ticked,bool submenu,
        const juce::String& text,const juce::String& shortcut,const juce::Drawable* icon,
        const juce::Colour* customText) override
    {
        const juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(area);
        if(separator)
        {
            g.setColour(Colour::divider);
            g.fillRect(area.reduced(12,0).withSizeKeepingCentre(juce::jmax(0,area.getWidth()-24),1));
            return;
        }
        if(area.getWidth()<12||area.getHeight()<8)return;
        const auto tile=area.reduced(5,2).toFloat();
        const bool hover=active&&highlighted;
        if(hover||ticked)
        {
            g.setGradientFill(juce::ColourGradient(
                hover?Colour::surfaceRaised:Colour::surface,tile.getX(),tile.getY(),
                hover?Colour::selection:Colour::panel,tile.getRight(),tile.getBottom(),false));
            g.fillRoundedRectangle(tile,6.f);
            g.setColour((hover?Colour::accentBlue:Colour::accent).withAlpha(active?.85f:.3f));
            g.drawRoundedRectangle(tile.reduced(.5f),6.f,1.f);
            if(hover){g.setColour(Colour::accentLight);g.fillRoundedRectangle(tile.getX()+2.f,tile.getY()+4.f,2.f,tile.getHeight()-8.f,1.f);}
        }
        const auto colour=customText!=nullptr?*customText:(hover||ticked?Colour::accentLight:Colour::textPrimary);
        // Render native labels, check marks, icons, shortcut keys and submenu
        // chevrons on the shared tile. No default rectangular hover fill.
        juce::LookAndFeel_V4::drawPopupMenuItem(g,area.reduced(7,0),false,active,false,ticked,submenu,text,shortcut,icon,&colour);
    }
    void drawComboBox(juce::Graphics& g,int width,int height,bool,
        int,int,int,int,juce::ComboBox& box) override
    {
        if(width<4||height<4)return;
        const auto tile=juce::Rectangle<float>(.5f,.5f,(float)width-1.f,(float)height-1.f);
        const bool focus=box.hasKeyboardFocus(true),hover=box.isEnabled()&&box.isMouseOverOrDragging();
        const auto base=box.findColour(juce::ComboBox::backgroundColourId);
        g.setGradientFill(juce::ColourGradient(base.interpolatedWith(Colour::surfaceRaised,hover?.65f:.1f),tile.getX(),tile.getY(),base.darker(.14f),tile.getRight(),tile.getBottom(),false));
        g.fillRoundedRectangle(tile,7.f);
        g.setColour(box.findColour(focus?juce::ComboBox::focusedOutlineColourId:juce::ComboBox::outlineColourId).withAlpha(box.isEnabled()?1.f:.4f));
        g.drawRoundedRectangle(tile,7.f,focus?1.6f:1.f);
        const float cx=(float)width-15.f,cy=(float)height*.5f;
        juce::Path arrow;arrow.startNewSubPath(cx-4.f,cy-2.f);arrow.lineTo(cx,cy+2.f);arrow.lineTo(cx+4.f,cy-2.f);
        g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha(box.isEnabled()?1.f:.35f));
        g.strokePath(arrow,juce::PathStrokeType(1.7f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded));
    }
};
}
