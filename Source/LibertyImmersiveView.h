#pragma once
#include "AudioEngine.h"
#include "LibertyDesignSystem.h"
#include "LibertyImmersiveAnalysis.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace LibertyImmersive
{
class View final : public juce::Component, private juce::Timer
{
    AudioEngine& engine;
    std::array<juce::TextButton,5> tabs;
    juce::TextButton freeze{"FIGER"},reset{"EFFACER"};
    std::array<Frame,fftSize> window{},ordered{};
    std::array<Frame,8192> incoming{};
    ComplexBuffer work{};
    Spectrum bins{};
    std::array<Metrics,300> history{};
    std::size_t cursor=0,valid=0,histCursor=0,histCount=0;
    Metrics latest{};
    double rate=0.0;
    int page=0;
    bool frozen=false,hasData=false;
    juce::Point<int> pointer{-1,-1};
    juce::Rectangle<int> plot() const { return getLocalBounds().reduced(18).withTrimmedTop(100).withTrimmedBottom(64); }
    float hz(float x) const { const float max=static_cast<float>(std::max(21.0,std::min(20000.0,rate*.5)));return 20.f*std::pow(max/20.f,std::clamp(x,0.f,1.f)); }
    float freqX(float frequency) const { return std::log(frequency/20.f)/std::log(hz(1.f)/20.f); }
    static float dbY(float value,juce::Rectangle<float> a) {return a.getBottom()-a.getHeight()*std::clamp((value+90.f)/96.f,0.f,1.f);}
    void label(juce::Graphics&g,const juce::String& text,juce::Rectangle<int> a,juce::Justification j=juce::Justification::centred) const
    {g.setColour(LibertyUI::Colour::textMuted);g.setFont(juce::Font(12.f));g.drawFittedText(text,a,j,1);}
    void trace(juce::Graphics&g,const juce::Path&p,juce::Colour c) const
    { g.setColour(c.withAlpha(.16f));g.strokePath(p,juce::PathStrokeType(3.5f));g.setColour(c);g.strokePath(p,juce::PathStrokeType(1.3f)); }
    void levelGrid(juce::Graphics&g,juce::Rectangle<float> a)
    {
        for(int v=-90;v<=0;v+=15){float y=dbY((float)v,a);g.setColour(LibertyUI::Colour::dividerSoft);g.drawLine(a.getX(),y,a.getRight(),y);label(g,juce::String(v),{(int)a.getX()-40,(int)y-8,36,16},juce::Justification::centredRight);}
    }
    void clear()
    {
        cursor=valid=histCursor=histCount=0;hasData=false;latest={};window.fill({});ordered.fill({});bins.fill(0.f);history.fill({});
    }
    void timerCallback() override
    {
        const auto sr=engine.getSampleRate();
        if(sr!=rate){rate=sr;clear();}
        auto& tap=engine.immersiveTap();
        bool received=false;
        // Limit consumer work to one ring capacity per timer tick.
        for(int block=0;block<4;++block)
        {
            const auto n=tap.read(incoming.data(),incoming.size());if(!n)break;
            if(frozen)continue;
            received=true;
            for(std::size_t i=0;i<n;++i){window[cursor]=incoming[i];cursor=(cursor+1)%fftSize;valid=std::min(fftSize,valid+1);}
        }
        if(frozen)return;
        if(received&&valid)
        {
            const auto first=(cursor+fftSize-valid)%fftSize;
            ordered.fill({});
            for(std::size_t i=0;i<valid;++i)ordered[fftSize-valid+i]=window[(first+i)%fftSize];
            latest=measure(ordered.data()+fftSize-valid,valid);
            hasData=true;
            if(page==0&&valid==fftSize)spectrum(ordered,work,bins);
        }
        else if(!received){hasData=false;latest={};valid=0;bins.fill(0.f);}
        history[histCursor]=latest;histCursor=(histCursor+1)%history.size();histCount=std::min(history.size(),histCount+1);
        repaint();
    }
public:
    explicit View(AudioEngine& e):engine(e)
    {
        const juce::String names[]={"SPECTRE","OSCILLOSCOPE","PHASE","DYNAMIQUE","HISTORIQUE"};
        for(int i=0;i<5;++i){auto& b=tabs[(size_t)i];b.setButtonText(names[i]);b.setClickingTogglesState(false);b.setToggleState(i==0,juce::dontSendNotification);b.onClick=[this,i]{page=i;for(int j=0;j<5;++j)tabs[(size_t)j].setToggleState(i==j,juce::dontSendNotification);if(page==0&&valid==fftSize)spectrum(ordered,work,bins);repaint();};addAndMakeVisible(b);}
        freeze.onClick=[this]{frozen=!frozen;freeze.setToggleState(frozen,juce::dontSendNotification);freeze.setButtonText(frozen?"REPRENDRE":"FIGER");repaint();};
        reset.onClick=[this]{clear();engine.immersiveTap().discard();repaint();};
        addAndMakeVisible(freeze);addAndMakeVisible(reset);
        setSize(800,540);
    }
    ~View() override { stopTimer();engine.immersiveTap().setEnabled(false); }
    void begin(){clear();engine.immersiveTap().discard();engine.immersiveTap().setEnabled(true);startTimerHz(25);repaint();}
    void end(){stopTimer();engine.immersiveTap().setEnabled(false);}
    void resized() override
    {
        auto bar=getLocalBounds().reduced(18).withTrimmedTop(48).removeFromTop(32);
        const int width=bar.getWidth()/7;
        for(auto& b:tabs)b.setBounds(bar.removeFromLeft(width).reduced(2,1));
        freeze.setBounds(bar.removeFromLeft(width).reduced(2,1));reset.setBounds(bar.reduced(2,1));
    }
    void mouseMove(const juce::MouseEvent&e) override{pointer=e.getPosition();if(page==0)repaint();}
    void mouseExit(const juce::MouseEvent&) override{pointer={-1,-1};repaint();}
    void paint(juce::Graphics&g) override
    {
        g.fillAll(LibertyUI::Colour::canvasDeep);
        g.setColour(LibertyUI::Colour::textPrimary);g.setFont(juce::Font(22.f,juce::Font::bold));
        g.drawText("OBSIDIAN IMMERSIVE",18,12,getWidth()-36,30,juce::Justification::centredLeft);
        auto panel=plot();LibertyUI::fillPanel(g,panel.toFloat(),false,9.f);
        auto a=panel.reduced(46,26).toFloat();
        if(a.isEmpty())return;
        if(page==0||page==4)levelGrid(g,a);
        if(page==0)
        {
            for(float f:{20.f,50.f,100.f,200.f,500.f,1000.f,2000.f,5000.f,10000.f,20000.f})
            {
                if(f>hz(1.f))continue;const float x=a.getX()+a.getWidth()*freqX(f);
                g.setColour(LibertyUI::Colour::dividerSoft);g.drawLine(x,a.getY(),x,a.getBottom());
                label(g,f>=1000?juce::String(f/1000.f,0)+"k":juce::String(f,0),{(int)x-17,(int)a.getBottom()+4,34,16});
            }
            if(hasData&&valid==fftSize&&rate>0)
            {
                juce::Path path;bool first=true;
                for(int x=0;x<(int)a.getWidth();++x)
                {
                    const float low=hz((float)x/a.getWidth()),high=hz((float)(x+1)/a.getWidth());
                    const int i0=juce::jlimit(1,(int)bins.size()-1,(int)std::floor(low*fftSize/rate));
                    const int i1=juce::jlimit(i0,(int)bins.size()-1,(int)std::ceil(high*fftSize/rate));
                    float m=0.f;for(int i=i0;i<=i1;++i)m=std::max(m,bins[(size_t)i]);
                    const float y=dbY(db(m),a);
                    if(first){path.startNewSubPath(a.getX()+x,y);first=false;}else path.lineTo(a.getX()+x,y);
                }
                juce::Graphics::ScopedSaveState saved(g);g.reduceClipRegion(a.toNearestInt());trace(g,path,LibertyUI::Colour::accent);
            }
            if(a.contains(pointer.toFloat())&&rate>0)
            {
                const float f=hz((pointer.x-a.getX())/a.getWidth());
                const auto bin=(size_t)juce::jlimit(0,(int)bins.size()-1,(int)std::round(f*fftSize/rate));
                g.setColour(LibertyUI::Colour::accentLight.withAlpha(.7f));g.drawVerticalLine(pointer.x,a.getY(),a.getBottom());
                label(g,juce::String(f,0)+" Hz  |  "+(hasData&&valid==fftSize?juce::String(db(bins[bin]),1)+" dBFS":"--"),{(int)a.getX(),panel.getY()+4,(int)a.getWidth(),18});
            }
        }
        else if(page==1)
        {
            for(int ch=0;ch<2;++ch)
            {
                auto b=a.withHeight(a.getHeight()*.5f-6.f).translated(0.f,ch*a.getHeight()*.5f);
                g.setColour(LibertyUI::Colour::divider);g.drawHorizontalLine((int)b.getCentreY(),b.getX(),b.getRight());
                label(g,ch?"R":"L",{(int)b.getX()-30,(int)b.getCentreY()-10,24,20});
                if(!hasData)continue;
                const auto n=std::min<std::size_t>(512,valid);juce::Path p;
                for(std::size_t i=0;i<n;++i){const auto& s=ordered[fftSize-n+i];const float x=b.getX()+b.getWidth()*(float)i/(float)std::max<std::size_t>(1,n-1);const float y=b.getCentreY()-std::clamp(ch?s.r:s.l,-1.f,1.f)*b.getHeight()*.48f;if(i==0)p.startNewSubPath(x,y);else p.lineTo(x,y);}
                trace(g,p,ch?LibertyUI::Colour::accentPurple:LibertyUI::Colour::accent);
            }
        }
        else if(page==2)
        {
            const float side=std::min(a.getWidth(),a.getHeight()-36.f);
            auto b=juce::Rectangle<float>(side,side).withCentre(a.getCentre()).translated(0.f,-12.f);
            g.setColour(LibertyUI::Colour::divider);g.drawEllipse(b,1.f);g.drawLine(b.getX(),b.getCentreY(),b.getRight(),b.getCentreY());g.drawLine(b.getCentreX(),b.getY(),b.getCentreX(),b.getBottom());
            if(hasData){g.setColour(LibertyUI::Colour::accent.withAlpha(.64f));const auto n=std::min<std::size_t>(1024,valid);for(std::size_t i=fftSize-n;i<fftSize;++i){const auto s=ordered[i];const float x=b.getCentreX()+std::clamp((s.l-s.r)*.5f,-1.f,1.f)*side*.48f;const float y=b.getCentreY()-std::clamp((s.l+s.r)*.5f,-1.f,1.f)*side*.48f;g.fillEllipse(x-1.f,y-1.f,2.f,2.f);}}
            auto rail=juce::Rectangle<float>(a.getX(),a.getBottom()-12.f,a.getWidth(),7.f);
            g.setColour(LibertyUI::Colour::divider);g.fillRoundedRectangle(rail,3.f);
            if(latest.correlated&&hasData){g.setColour(latest.correlation<0?LibertyUI::Colour::accentAmber:LibertyUI::Colour::accent);const float x=rail.getX()+rail.getWidth()*(latest.correlation+1.f)*.5f;g.fillEllipse(x-4.f,rail.getY()-1.f,9.f,9.f);}
            label(g,"-1           0           +1    |    "+(latest.correlated&&hasData?juce::String(latest.correlation,2):juce::String("--")),{(int)a.getX(),(int)a.getBottom()+4,(int)a.getWidth(),18});
        }
        else if(page==3)
        {
            const float vals[]={db(latest.peak),db(latest.rms),latest.crest};const char* titles[]={"PEAK dBFS","RMS dBFS","CREST dB"};
            const float w=a.getWidth()/3.f;
            for(int i=0;i<3;++i){auto b=juce::Rectangle<float>(a.getX()+i*w,a.getY()+10.f,w-10.f,a.getHeight()-20.f);LibertyUI::fillPanel(g,b,false,7.f);label(g,titles[i],b.withHeight(30.f).toNearestInt());g.setColour(i==2?LibertyUI::Colour::accentAmber:LibertyUI::Colour::accentLight);g.setFont(juce::Font(32.f,juce::Font::bold));g.drawFittedText(hasData?juce::String(vals[i],1):juce::String("--"),b.toNearestInt(),juce::Justification::centred,1);const float amount=i==2?std::clamp(vals[i]/30.f,0.f,1.f):std::clamp((vals[i]+90.f)/96.f,0.f,1.f);g.fillRoundedRectangle(b.getX()+10.f,b.getBottom()-24.f,(b.getWidth()-20.f)*amount,7.f,3.f);}
        }
        else
        {
            for(int channel=0;channel<2;++channel){juce::Path p;for(std::size_t i=0;i<histCount;++i){const auto& m=history[(histCursor+history.size()-histCount+i)%history.size()];const float x=a.getX()+a.getWidth()*(float)(history.size()-histCount+i)/(float)(history.size()-1);const float y=dbY(db(channel?m.rms:m.peak),a);if(i==0)p.startNewSubPath(x,y);else p.lineTo(x,y);}trace(g,p,channel?LibertyUI::Colour::accentPurple:LibertyUI::Colour::accent);}
            label(g,"-12 s                                      PEAK / RMS                                      0 s",{(int)a.getX(),(int)a.getBottom()+4,(int)a.getWidth(),18});
        }
        label(g,juce::String::fromUTF8("MASTER : sortie stéréo finale")+"  |  "+juce::String(rate,0)+" Hz  |  FFT 4096 / Hann  |  "+(frozen?"FIGE":hasData?"LIVE":"AUCUNE DONNEE"),{18,getHeight()-65,getWidth()-36,25});
        label(g,juce::String::fromUTF8("Mesures dBFS échantillon, RMS et corrélation. Aucun traitement du signal."),{18,getHeight()-36,getWidth()-36,20});
    }
};
class Window final : public juce::DocumentWindow
{
    View* view=nullptr;
public:
    explicit Window(AudioEngine& e):juce::DocumentWindow("Liberty - OBSIDIAN IMMERSIVE",LibertyUI::Colour::canvasDeep,juce::DocumentWindow::closeButton)
    {setUsingNativeTitleBar(true);setResizable(false,false);view=new View(e);setContentOwned(view,true);centreWithSize(800,540);}
    void open(){if(!isVisible())view->begin();setVisible(true);toFront(true);}
    void closeButtonPressed() override{view->end();setVisible(false);}
};
}
