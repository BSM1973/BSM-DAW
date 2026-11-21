#pragma once

#include <juce_graphics/juce_graphics.h>

// Liberty / Obsidian 2.0. Shared, restrained visual language for the DAW.
// Draw-only helpers: no component ownership, DSP, routing or geometry changes.
namespace LibertyUI
{
namespace Colour
{
inline const juce::Colour canvas        { 0xff0b1423 };
inline const juce::Colour canvasDeep    { 0xff080f1b };
inline const juce::Colour header        { 0xff122337 };
inline const juce::Colour panel         { 0xff122236 };
inline const juce::Colour surface       { 0xff1a3045 };
inline const juce::Colour surfaceRaised { 0xff223d54 };
inline const juce::Colour control       { 0xff1b3044 };
inline const juce::Colour divider       { 0xff35546a };
inline const juce::Colour dividerSoft   { 0xff243a50 };
inline const juce::Colour accent        { 0xff57dce8 };
inline const juce::Colour accentLight   { 0xffadf5f4 };
inline const juce::Colour accentBlue    { 0xff6e9ef2 };
inline const juce::Colour accentPurple  { 0xff9d90f6 };
inline const juce::Colour accentAmber   { 0xffe1ba75 };
inline const juce::Colour active        { 0xff226b79 };
inline const juce::Colour selection     { 0xff254c65 };
inline const juce::Colour textPrimary   { 0xffeff8fc };
inline const juce::Colour textMuted     { 0xffa1b9c9 };
inline const juce::Colour textCyan      { 0xffb8eff2 };
inline const juce::Colour danger        { 0xffb64a54 };
}
namespace Space
{
constexpr int xs = 4;
constexpr int sm = 8;
constexpr int md = 12;
constexpr int lg = 16;
constexpr int xl = 24;
constexpr float controlRadius = 7.0f;
constexpr float panelRadius = 10.0f;
}
inline void fillPanel(juce::Graphics& g,juce::Rectangle<float> area,
                      bool selected=false,float radius=Space::panelRadius)
{
    if(area.isEmpty())return;
    const auto first=selected ? Colour::surfaceRaised : Colour::panel;
    const auto last=selected ? Colour::surface : Colour::canvas;
    g.setGradientFill(juce::ColourGradient(
        first,area.getX(),area.getY(),
        last,area.getRight(),area.getBottom(),false));
    g.fillRoundedRectangle(area,radius);
    g.setColour(selected?Colour::accent.withAlpha(0.74f):
                          Colour::divider.withAlpha(0.66f));
    g.drawRoundedRectangle(area.reduced(0.5f),radius,selected?1.35f:0.85f);
    // A faint inner sheen creates depth without a glossy, dated bevel.
    g.setColour(juce::Colours::white.withAlpha(selected?0.08f:0.035f));
    g.drawLine(area.getX()+radius,area.getY()+1.2f,
               area.getRight()-radius,area.getY()+1.2f,0.7f);
}
inline void accentRule(juce::Graphics& g,juce::Rectangle<float> area,
                       juce::Colour colour=Colour::accent)
{
    if(area.isEmpty())return;
    g.setGradientFill(juce::ColourGradient(
        colour.withAlpha(0.9f),area.getX(),area.getY(),
        colour.withAlpha(0.0f),area.getRight(),area.getY(),false));
    g.fillRect(area);
}
 // Musical surfaces share a single drawing vocabulary. These are paint-only:
 // callers retain their own hit rectangles, note timing, clip colours and data.
inline void musicalBlock(juce::Graphics& g,juce::Rectangle<float> area,
                         juce::Colour tint,bool selected,bool muted=false)
{
    if(area.isEmpty())return;
    const juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(area.toNearestInt());
    if(muted)tint=tint.withMultipliedSaturation(0.12f).darker(0.35f);
    const float radius=juce::jmin(5.f,area.getHeight()*0.32f,area.getWidth()*0.25f);
    g.setGradientFill(juce::ColourGradient(
        tint.brighter(0.16f),area.getX(),area.getY(),
        tint.darker(0.23f),area.getX(),area.getBottom(),false));
    g.fillRoundedRectangle(area,radius);
    if(area.getWidth()>5.f&&area.getHeight()>5.f)
    {
        g.setColour(juce::Colours::white.withAlpha(muted?0.05f:0.19f));
        g.drawLine(area.getX()+radius,area.getY()+1.f,
                   area.getRight()-radius,area.getY()+1.f,0.8f);
    }
    g.setColour(selected?Colour::accentAmber:tint.brighter(0.3f));
    g.drawRoundedRectangle(area.reduced(0.5f),radius,
        juce::jmin(area.getHeight()*0.5f,selected?1.5f:0.8f));
}
inline void waveformEnvelope(juce::Graphics& g,const juce::Path& path,
                             juce::Rectangle<float> area,bool selected)
{
    if(area.isEmpty()||path.isEmpty())return;
    const juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(area.toNearestInt());
    g.setGradientFill(juce::ColourGradient(
        Colour::accentLight.withAlpha(selected?0.98f:0.84f),
        area.getCentreX(),area.getY(),
        Colour::accentBlue.withAlpha(selected?0.95f:0.72f),
        area.getCentreX(),area.getBottom(),false));
    g.fillPath(path);
}

}
