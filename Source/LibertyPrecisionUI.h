#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "LibertyDesignSystem.h"
#include <array>
#include <cmath>
namespace LibertyPrecision
{
// All drawing is vector based in logical coordinates; JUCE supplies Retina scaling.
inline void drawDial(juce::Graphics& g,int x,int y,int w,int h,float position,float start,float end,juce::Slider& slider)
{
    const float d=(float)juce::jmin(w,h);
    if(d<10.f)return;
    const float cx=(float)x+(float)w*.5f;
    const float cy=(float)y+(float)h*.5f;
    const float radius=(d-2.f)*.5f;
    const float angle=start+juce::jlimit(0.f,1.f,position)*(end-start);
    const auto accent=slider.findColour(juce::Slider::rotarySliderFillColourId)
        .withMultipliedAlpha(slider.isEnabled()?1.f:.45f);
    juce::Graphics::ScopedSaveState state(g);
    g.reduceClipRegion(juce::Rectangle<int>(x,y,w,h));
    // Precision dark dial with a clear coloured value arc.
    g.setColour(LibertyUI::Colour::canvasDeep);
    g.fillEllipse(cx-radius,cy-radius,2.f*radius,2.f*radius);
    g.setColour(LibertyUI::Colour::divider);
    g.drawEllipse(cx-radius,cy-radius,2.f*radius,2.f*radius,.7f);
    const int tickCount=d>=36.f?16:8;
    for(int tick=0;tick<=tickCount;++tick){
        const float a=start+(end-start)*(float)tick/(float)tickCount;
        const bool major=(tick%(tickCount/4))==0;
        const float inside=radius*(major?.83f:.90f),outside=radius*.97f;
        g.setColour((a<=angle?accent:LibertyUI::Colour::textMuted).withAlpha(major?.92f:.48f));
        g.drawLine(cx+std::sin(a)*inside,cy-std::cos(a)*inside,
                   cx+std::sin(a)*outside,cy-std::cos(a)*outside,major?1.f:.65f);
    }
    juce::Path track;
    track.addCentredArc(cx,cy,radius*.81f,radius*.81f,0.f,start,end,true);
    g.setColour(juce::Colour(0xff304653));
    g.strokePath(track,juce::PathStrokeType(1.6f));
    juce::Path filled;
    filled.addCentredArc(cx,cy,radius*.81f,radius*.81f,0.f,start,angle,true);
    g.setColour(accent.withAlpha(.16f));
    g.strokePath(filled,juce::PathStrokeType(3.8f));
    g.setColour(accent.withAlpha(.93f));
    g.strokePath(filled,juce::PathStrokeType(1.9f));
    const float bodyRadius=radius*.70f;
    g.setGradientFill(juce::ColourGradient(
        LibertyUI::Colour::surfaceRaised,cx,cy-bodyRadius,
        LibertyUI::Colour::canvasDeep,cx,cy+bodyRadius,false));
    g.fillEllipse(cx-bodyRadius,cy-bodyRadius,2.f*bodyRadius,2.f*bodyRadius);
    g.setColour(LibertyUI::Colour::textMuted.withAlpha(.68f));
    g.drawEllipse(cx-bodyRadius,cy-bodyRadius,2.f*bodyRadius,2.f*bodyRadius,.85f);
    g.setColour(LibertyUI::Colour::panel);
    g.fillEllipse(cx-bodyRadius*.58f,cy-bodyRadius*.58f,
                  1.16f*bodyRadius,1.16f*bodyRadius);
    if(slider.hasKeyboardFocus(true)) {
        g.setColour(LibertyUI::Colour::accentLight);
        g.drawEllipse(cx-radius,cy-radius,2.f*radius,2.f*radius,1.2f);
    }
    const float inner=bodyRadius*.15f,outer=bodyRadius*.83f;
    g.setColour(accent.brighter(.28f));
    g.drawLine(cx+std::sin(angle)*inner,cy-std::cos(angle)*inner,
               cx+std::sin(angle)*outer,cy-std::cos(angle)*outer,1.7f);
}
inline void enableValuePopup(juce::Slider& slider)
{
    // JUCE formats the real parameter value, using the slider's suffix and precision.
    slider.setPopupDisplayEnabled(true,true,nullptr,950);
}
inline juce::MouseCursor makeToolCursor(int kind)
{
    // A two-times raster of vector paths, no font glyphs or emoji dependencies.
    juce::Image image(juce::Image::ARGB,48,48,true);
    juce::Graphics g(image);
    g.addTransform(juce::AffineTransform::scale(2.f));
    juce::Path p;
    if(kind==1) { p.startNewSubPath(4,20);p.lineTo(7,12);p.lineTo(18,1);p.lineTo(23,6);p.lineTo(12,17);p.closeSubPath();p.startNewSubPath(7,12);p.lineTo(12,17); }
    else if(kind==2) { p.addEllipse(2,12,7,7);p.addEllipse(12,12,7,7);p.startNewSubPath(7,13);p.lineTo(20,1);p.startNewSubPath(14,13);p.lineTo(3,1); }
    else if(kind==3) { p.startNewSubPath(2,13);p.lineTo(13,2);p.lineTo(22,11);p.lineTo(11,22);p.closeSubPath();p.startNewSubPath(7,8);p.lineTo(16,17); }
    else if(kind==5) { p.addRectangle(5,6,15,15);p.addRectangle(9,2,7,6);p.startNewSubPath(9,13);p.lineTo(16,13);p.startNewSubPath(12.5f,9.5f);p.lineTo(12.5f,16.5f); }
    else if(kind==7) { for(int i=2;i<20;i+=6){p.addRectangle((float)i,2,4,1);p.addRectangle((float)i,20,4,1);p.addRectangle(2,(float)i,1,4);p.addRectangle(20,(float)i,1,4);} }
    else { p.addEllipse(3,3,18,18);p.startNewSubPath(6,18);p.lineTo(18,6); }
    g.setColour(juce::Colours::black.withAlpha(.9f));
    g.strokePath(p,juce::PathStrokeType(3.2f));
    g.setColour(juce::Colours::white);
    g.strokePath(p,juce::PathStrokeType(1.3f));
    return juce::MouseCursor(image,24,24,2.f);
}
inline juce::MouseCursor midiCursor(int tool)
{
    switch(tool){
        case 0:return juce::MouseCursor::NormalCursor;
        case 4:return juce::MouseCursor::LeftRightResizeCursor;
        case 6:return juce::MouseCursor::UpDownResizeCursor;
        default:break;
    }
    static const std::array<juce::MouseCursor,9> cursors{
        juce::MouseCursor(juce::MouseCursor::NormalCursor),makeToolCursor(1),makeToolCursor(2),
        makeToolCursor(3),juce::MouseCursor(juce::MouseCursor::LeftRightResizeCursor),makeToolCursor(5),
        juce::MouseCursor(juce::MouseCursor::UpDownResizeCursor),makeToolCursor(7),makeToolCursor(8)};
    return cursors[(size_t)juce::jlimit(0,8,tool)];
}
inline juce::MouseCursor arrangementCursor(int tool)
{
    // Arrangement and Piano Roll use different tool indices.
    static constexpr int map[]{0,7,1,3,2,5,8,4};
    return midiCursor(map[juce::jlimit(0,7,tool)]);
}
}
