#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cmath>
#include <functional>

// Liberty Drum Studio — compact Obsidian instrument illustrations.
// Unlike fixed pad-number art, the glyph follows the actual sample name.
// The icons are resolution-independent JUCE vectors, require no external
// image files and never run in the audio callback.
namespace LibertyDrumArt
{
enum Icon : int
{
    kick, snare, closedHat, openHat, clap, rim,
    tomLow, tomMid, tomHigh, crash, ride, shaker,
    tambourine, cowbell, percussion, effect, iconCount
};
inline constexpr std::array<int,16> fallback{{
    kick,snare,clap,closedHat,openHat,tomLow,tomMid,tomHigh,
    rim,shaker,percussion,percussion,crash,ride,effect,kick
}};
inline int iconFor(const juce::String& instrument,int pad,bool hasSample) noexcept
{
    const auto n=instrument.toUpperCase();
    if(n.contains("RIM")||n.contains("SIDE STICK")||n.contains("CROSS STICK")
       ||n.contains("CLAVES"))return rim;
    if(n.contains("CLOSED HAT")||n.contains("HAT CLOSED")||n.contains("CHARLEY FER")
       ||n.contains("HI-HAT CLOSE")||n.contains("HH CLOSED")||n.contains("HAT TIGHT"))return closedHat;
    if(n.contains("OPEN HAT")||n.contains("HAT OPEN")||n.contains("CHARLEY OUV")
       ||n.contains("HI-HAT OPEN")||n.contains("SEMI-OPEN"))return openHat;
    if(n.contains("HAT")||n.contains("HIHAT")||n.contains("CHARLEY"))return closedHat;
    if(n.contains("TAMBOUR")||n.contains("RIQ"))return tambourine;
    if(n.contains("COWBELL"))return cowbell;
    if(n.contains("SHAKER"))return shaker;
    if(n.contains("CLAP")||n.contains("MAIN"))return clap;
    if(n.contains("CRASH"))return crash;
    if(n.contains("RIDE"))return ride;
    if(n.contains("TOM"))
    {
        if(n.contains("FLOOR")||n.contains("LOW")||n.contains("BAS"))return tomLow;
        if(n.contains("HIGH")||n.contains("HAUT"))return tomHigh;
        return tomMid;
    }
    if(n.contains("CONGA")||n.contains("BONGO")||n.contains("PERC")
       ||n.contains("DARBUKA")||n.contains("DJEMBE"))return percussion;
    if(n.contains("IMPACT")||n.contains("BOOM FX")||n.contains("GONG")
       ||n.contains("WHOOSH")||n.contains("FX")||n.contains("EFFECT"))return effect;
    if(n.contains("808")||n.contains("KICK")||n.contains("GROSSE CAISSE")
       ||n.contains("SUB BASS")||n.contains("BASS DRUM"))return kick;
    if(n.contains("SNARE")||n.contains("CAISSE CLAIRE"))return snare;
    // Custom audio with an unknown filename displays an honest waveform,
    // instead of an unrelated drum. Empty pads show their intended slot.
    return hasSample?effect:fallback[(std::size_t)juce::jlimit(0,15,pad)];
}
inline const char* name(int id) noexcept
{
    static constexpr const char* labels[]={
        "Grosse caisse","Caisse claire","Charley fermé","Charley ouvert",
        "Clap","Rimshot","Tom bas","Tom médium","Tom haut","Crash",
        "Ride","Shaker","Tambourin","Cowbell","Percussion","FX / One-shot"
    };
    return labels[juce::jlimit(0,iconCount-1,id)];
}

class Painter
{
    static inline juce::Colour dark() noexcept{return juce::Colour(0xff20445f);}
    static inline juce::Colour mid() noexcept{return juce::Colour(0xff6dabc9);}
    static inline juce::Colour pale() noexcept{return juce::Colour(0xffd8f2fd);}
    static inline juce::Colour cyan() noexcept{return juce::Colour(0xff5ee8f1);}
    static void line(juce::Graphics& g,float x1,float y1,float x2,float y2,float thickness=2.f)
    {g.setColour(pale());g.drawLine(x1,y1,x2,y2,thickness);}
    static void shadedOval(juce::Graphics& g,float x,float y,float w,float h)
    {
        g.setGradientFill(juce::ColourGradient(pale(),x,y,mid(),x,y+h,false));
        g.fillEllipse(x,y,w,h);
        g.setColour(dark());g.drawEllipse(x,y,w,h,2.f);
    }
    static void shell(juce::Graphics& g,float x,float y,float w,float h)
    {
        g.setGradientFill(juce::ColourGradient(mid(),x,y,dark(),x+w,y+h,false));
        g.fillRoundedRectangle(x,y+5.f,w,h-7.f,5.f);
        g.setColour(pale());g.drawRoundedRectangle(x+.75f,y+5.f,w-1.5f,h-7.f,5.f,1.5f);
        shadedOval(g,x,y,w,14.f);
        for(int i=1;i<5;++i)
        {
            const float lug=x+w*i/5.f;
            g.setColour(pale().withAlpha(.8f));g.fillRoundedRectangle(lug-1.f,y+14.f,2.f,h-19.f,1.f);
            g.setColour(cyan());g.fillEllipse(lug-2.f,y+13.f,4.f,4.f);
        }
        line(g,x+2.f,y+h-5.f,x+w-2.f,y+h-5.f,1.5f);
    }
    static void cymbal(juce::Graphics& g,float y,bool lines)
    {
        line(g,50.f,y-19.f,50.f,84.f,2.f);
        shadedOval(g,17.f,y-5.f,66.f,15.f);
        g.setColour(pale());g.fillEllipse(46.f,y-10.f,8.f,7.f);
        if(lines)
        {
            g.setColour(pale().withAlpha(.60f));
            g.drawEllipse(25.f,y-2.f,50.f,9.f,1.f);
            g.drawEllipse(34.f,y,32.f,5.f,1.f);
        }
        g.setColour(cyan());g.fillRoundedRectangle(46.f,y-15.f,8.f,3.f,1.f);
    }
    static void sparkle(juce::Graphics& g,float x,float y)
    {
        g.setColour(cyan());
        g.drawLine(x-3.f,y,x+3.f,y,1.8f);
        g.drawLine(x,y-3.f,x,y+3.f,1.8f);
    }
    static void hands(juce::Graphics& g)
    {
        juce::Path left;
        left.startNewSubPath(30.f,72.f);left.lineTo(20.f,56.f);
        left.lineTo(20.f,40.f);left.lineTo(25.f,37.f);
        left.lineTo(30.f,53.f);left.lineTo(29.f,27.f);
        left.lineTo(35.f,25.f);left.lineTo(39.f,48.f);
        left.lineTo(41.f,20.f);left.lineTo(47.f,21.f);
        left.lineTo(48.f,48.f);left.lineTo(52.f,28.f);
        left.lineTo(58.f,30.f);left.lineTo(55.f,62.f);
        left.lineTo(43.f,78.f);left.closeSubPath();
        g.setGradientFill(juce::ColourGradient(pale(),20.f,25.f,dark(),58.f,78.f,false));
        g.fillPath(left);g.setColour(pale());g.strokePath(left,juce::PathStrokeType(1.7f));
        juce::Path right;
        right.startNewSubPath(59.f,69.f);right.lineTo(61.f,44.f);
        right.lineTo(68.f,41.f);right.lineTo(70.f,69.f);
        right.lineTo(65.f,79.f);right.closeSubPath();
        g.setColour(mid());g.fillPath(right);g.setColour(pale());g.strokePath(right,juce::PathStrokeType(1.6f));
        sparkle(g,72.f,19.f);sparkle(g,80.f,30.f);
    }
public:
    static void paint(juce::Graphics& g,int type,juce::Rectangle<float> bounds)
    {
        const juce::Graphics::ScopedSaveState saved(g);
        const auto sx=bounds.getWidth()/100.f,sy=bounds.getHeight()/100.f;
        g.addTransform(juce::AffineTransform(sx,0.f,bounds.getX(),0.f,sy,bounds.getY()));
        g.setColour(juce::Colour(0xff081523).withAlpha(.72f));
        g.fillEllipse(18.f,83.f,64.f,7.f); // grounded, subtle shadow
        switch(juce::jlimit(0,iconCount-1,type))
        {
            case kick:
            {
                g.setGradientFill(juce::ColourGradient(mid(),24.f,17.f,dark(),80.f,80.f,false));
                g.fillEllipse(17.f,14.f,68.f,68.f);
                g.setColour(pale());g.drawEllipse(17.f,14.f,68.f,68.f,2.2f);
                g.setColour(mid());g.drawEllipse(24.f,20.f,55.f,55.f,2.f);
                g.setColour(pale());g.fillEllipse(60.f,52.f,12.f,12.f);
                for(int a=0;a<8;++a)
                {
                    const float t=(float)a*juce::MathConstants<float>::twoPi/8.f;
                    g.setColour(cyan());g.fillEllipse(48.f+31.f*std::cos(t)-2.f,
                        47.f+31.f*std::sin(t)-2.f,4.f,4.f);
                }
                line(g,28.f,77.f,22.f,90.f,2.4f);line(g,72.f,77.f,78.f,90.f,2.4f);
                break;
            }
            case snare:shell(g,19.f,30.f,62.f,42.f);line(g,17.f,74.f,83.f,74.f);break;
            case closedHat:
            {
                line(g,50.f,12.f,50.f,85.f);
                shadedOval(g,17.f,45.f,66.f,11.f);
                shadedOval(g,20.f,49.f,60.f,7.f);
                g.setColour(cyan());g.fillRoundedRectangle(47.f,37.f,6.f,4.f,1.f);
                break;
            }
            case openHat:
            {
                line(g,50.f,10.f,50.f,86.f);
                shadedOval(g,17.f,31.f,66.f,12.f);shadedOval(g,18.f,58.f,64.f,11.f);
                sparkle(g,82.f,23.f);sparkle(g,89.f,49.f);break;
            }
            case clap:hands(g);break;
            case rim:
            {
                shell(g,17.f,44.f,66.f,33.f);
                line(g,25.f,36.f,79.f,16.f,4.f);
                g.setColour(cyan());g.fillEllipse(27.f,38.f,7.f,7.f);
                sparkle(g,19.f,35.f);break;
            }
            case tomLow:shell(g,22.f,17.f,56.f,59.f);line(g,28.f,74.f,20.f,88.f);line(g,72.f,74.f,80.f,88.f);break;
            case tomMid:shell(g,18.f,26.f,64.f,45.f);break;
            case tomHigh:shell(g,23.f,33.f,54.f,34.f);break;
            case crash:cymbal(g,52.f,false);sparkle(g,80.f,19.f);sparkle(g,88.f,33.f);break;
            case ride:cymbal(g,52.f,true);break;
            case shaker:
            {
                const juce::Graphics::ScopedSaveState rotated(g);
                g.addTransform(juce::AffineTransform::rotation(-.61f,50.f,50.f));
                g.setGradientFill(juce::ColourGradient(pale(),29.f,28.f,dark(),72.f,68.f,false));
                g.fillRoundedRectangle(22.f,40.f,58.f,23.f,10.f);
                g.setColour(pale());g.drawRoundedRectangle(22.f,40.f,58.f,23.f,10.f,1.6f);
                g.setColour(mid());
                for(int i=0;i<7;++i)for(int row=0;row<2;++row)
                    g.fillEllipse(34.f+6.f*i,45.f+7.f*row,2.f,2.f);
                sparkle(g,85.f,47.f);
                break;
            }
            case tambourine:
            {
                g.setColour(mid());g.fillEllipse(18.f,23.f,64.f,53.f);
                g.setColour(pale());g.drawEllipse(18.f,23.f,64.f,53.f,3.f);
                g.setColour(dark());g.fillEllipse(29.f,34.f,42.f,31.f);
                for(int i=0;i<7;++i)
                {
                    const float a=(float)i*juce::MathConstants<float>::twoPi/7.f;
                    const float x=50.f+28.f*std::cos(a),y=49.f+24.f*std::sin(a);
                    g.setColour(pale());g.fillEllipse(x-5.f,y-3.f,10.f,6.f);
                    g.setColour(mid());g.drawEllipse(x-5.f,y-3.f,10.f,6.f,1.f);
                }
                break;
            }
            case cowbell:
            {
                juce::Path bell;
                bell.startNewSubPath(39.f,22.f);bell.lineTo(63.f,22.f);
                bell.lineTo(78.f,74.f);bell.lineTo(24.f,74.f);bell.closeSubPath();
                g.setGradientFill(juce::ColourGradient(pale(),35.f,25.f,dark(),80.f,75.f,false));
                g.fillPath(bell);g.setColour(pale());g.strokePath(bell,juce::PathStrokeType(2.f));
                shadedOval(g,24.f,67.f,54.f,14.f);line(g,50.f,12.f,50.f,22.f,3.f);
                line(g,45.f,12.f,55.f,12.f,3.f);break;
            }
            case percussion:
            {
                shell(g,11.f,34.f,38.f,43.f);shell(g,52.f,34.f,38.f,43.f);
                line(g,31.f,78.f,28.f,86.f);line(g,71.f,78.f,74.f,86.f);break;
            }
            case effect:
            {
                g.setColour(cyan());
                const int heights[]={7,14,23,37,54,42,63,44,31,20,12,7};
                for(int i=0;i<12;++i)
                    g.fillRoundedRectangle(12.f+i*6.8f,50.f-heights[i]/2.f,3.f,(float)heights[i],1.5f);
                break;
            }
            default:break;
        }
    }
};

class Button final:public juce::TextButton
{
public:
    std::function<void()> onContextMenu;
    void mouseDown(const juce::MouseEvent& e) override
    {
        if(e.mods.isPopupMenu()){if(onContextMenu)onContextMenu();return;}
        juce::TextButton::mouseDown(e);
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if(!e.mods.isPopupMenu())juce::TextButton::mouseUp(e);
    }
    void configure(const juce::String& label,int pad,bool hasSample)
    {
        const int newIcon=iconFor(label,pad,hasSample);
        const bool changed=newIcon!=icon;
        icon=newIcon;
        if(getButtonText()!=label)setButtonText(label);
        setTooltip("PAD "+juce::String(pad+1)+" - "+
            juce::String::fromUTF8(name(icon))+" - "+label);
        if(changed)repaint();
    }
    void setPadSelected(bool next)
    {
        if(selected==next)return;selected=next;repaint();
    }
    int getIconIndex() const noexcept{return icon;}
    bool getPadSelected() const noexcept{return selected;}
    void paintButton(juce::Graphics& g,bool mouseOver,bool mouseDown) override
    {
        auto area=getLocalBounds().toFloat().reduced(1.5f);
        if(area.isEmpty())return;
        const auto base=juce::Colour(0xff0e2336);
        const auto top=selected?juce::Colour(0xff244a65):
            mouseOver?juce::Colour(0xff1d3f57):juce::Colour(0xff17364c);
        g.setGradientFill(juce::ColourGradient(top,area.getX(),area.getY(),
            base,area.getX(),area.getBottom(),false));
        g.fillRoundedRectangle(area,9.f);
        g.setColour(selected?juce::Colour(0xff5de8f0):
            mouseOver?juce::Colour(0xff60adcd):juce::Colour(0xff356b86));
        g.drawRoundedRectangle(area.reduced(.5f),9.f,selected?2.f:1.f);
        if(mouseDown)
        {
            g.setColour(juce::Colour(0x225fe8ef));
            g.fillRoundedRectangle(area.reduced(3.f),7.f);
        }
        const float side=juce::jlimit(28.f,65.f,area.getHeight()*.60f);
        const float posX=area.getCentreX()-side*.5f;
        const float posY=area.getY()+6.f;
        Painter::paint(g,icon,{posX,posY,side,side});
        const auto roleColour=findColour(juce::TextButton::buttonColourId);
        g.setColour(selected?juce::Colour(0xff61edf3):roleColour.brighter(.45f));
        g.fillRoundedRectangle(area.getCentreX()-19.f,
            juce::jmin(area.getBottom()-28.f,posY+side+1.f),38.f,3.f,1.5f);
        g.setColour(juce::Colour(0xbb061727));
        g.fillRoundedRectangle(area.getX()+5.f,area.getBottom()-25.f,
            area.getWidth()-10.f,20.f,5.f);
        g.setColour(juce::Colour(0xffe6f7ff));
        g.setFont(juce::Font(12.f,juce::Font::bold));
        g.drawFittedText(getButtonText(),
            juce::Rectangle<int>((int)area.getX()+10,(int)area.getBottom()-24,
                (int)area.getWidth()-20,18),juce::Justification::centred,1);
    }
private:
    int icon=effect;
    bool selected=false;
};
} // namespace LibertyDrumArt
