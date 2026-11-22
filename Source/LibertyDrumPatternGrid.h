#pragma once
// Included only after LibertyDrumSampler is declared.
class LibertyDrumPatternGrid final : public juce::Component, private juce::Timer
{
public:
    explicit LibertyDrumPatternGrid(LibertyDrumSampler& engine):sampler(engine)
    {
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        // Fully self-describing controls, no extra floating tooltip window.
        startTimerHz(16);
    }
    std::function<void(int)> onPadSelected;
    void setSelectedPad(int pad){selectedPad=juce::jlimit(0,LibertyDrumSampler::padCount-1,pad);repaint();}
    void resized() override{repaint();}
    juce::Rectangle<int> cell(int pad,int step) const noexcept
    {
        const int top=27,head=148;
        const int rowH=juce::jmax(1,(getHeight()-top-4)/LibertyDrumSampler::padCount);
        const int cellW=juce::jmax(1,(getWidth()-head-6)/LibertyDrumSampler::patternSteps);
        return {head+step*cellW,top+pad*rowH,cellW,rowH};
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(LibertyUI::Colour::canvasDeep);
        auto bar=getLocalBounds().withHeight(24);
        LibertyUI::fillPanel(g,bar.toFloat(),false,5.f);
        g.setColour(LibertyUI::Colour::textMuted);
        g.setFont(juce::Font(11.f,juce::Font::bold));
        g.drawText("PAD / MIDI",10,2,125,19,juce::Justification::centredLeft);
        const int playhead=sampler.getPatternPreviewStep();
        for(int step=0;step<LibertyDrumSampler::patternSteps;++step)
        {
            const auto r=cell(0,step);
            g.setColour(playhead==step?LibertyUI::Colour::accentLight:
                step%4==0?LibertyUI::Colour::accentBlue:LibertyUI::Colour::textMuted);
            g.drawFittedText(juce::String(step+1),{r.getX(),2,r.getWidth(),19},
                juce::Justification::centred,1);
        }
        for(int pad=0;pad<LibertyDrumSampler::padCount;++pad)
        {
            const auto lead=cell(pad,0);
            auto row=juce::Rectangle<int>(0,lead.getY(),getWidth(),lead.getHeight());
            g.setColour(pad==selectedPad?LibertyUI::Colour::selection:
                pad%2?LibertyUI::Colour::canvas:LibertyUI::Colour::panel);
            g.fillRect(row);
            if(pad==selectedPad)
            {g.setColour(LibertyUI::Colour::accent);g.fillRect(0,row.getY(),3,row.getHeight());}
            const auto padColour=juce::Colour((juce::uint32)sampler.getPad(pad).colourArgb.load());
            g.setColour(padColour.brighter(.25f));g.fillRoundedRectangle(9.f,(float)row.getY()+5.f,4.f,juce::jmax(2.f,(float)row.getHeight()-10.f),2.f);
            g.setColour(LibertyUI::Colour::textPrimary);
            g.setFont(juce::Font(11.f,juce::Font::bold));
            const juce::String name=sampler.hasSample(pad)?sampler.getPad(pad).name:("PAD "+juce::String(pad+1));
            g.drawFittedText(name,{18,row.getY()+2,104,juce::jmax(0,row.getHeight()-4)},juce::Justification::centredLeft,1);
            g.setColour(LibertyUI::Colour::textMuted);g.setFont(10.f);
            g.drawText(juce::String(36+pad),{121,row.getY(),23,row.getHeight()},juce::Justification::centred);
            for(int step=0;step<LibertyDrumSampler::patternSteps;++step)
            {
                auto r=cell(pad,step).reduced(2,3);
                if(r.isEmpty())continue;
                const int vel=sampler.getPatternVelocity(pad,step);
                const bool lit=step==playhead && sampler.isPatternPreviewPlaying();
                g.setColour(lit?LibertyUI::Colour::selection:
                    step%4==0?LibertyUI::Colour::surface:LibertyUI::Colour::control);
                g.fillRoundedRectangle(r.toFloat(),4.f);
                if(vel>0)
                {
                    const float alpha=.40f+.60f*(float)vel/127.f;
                    g.setColour(padColour.interpolatedWith(LibertyUI::Colour::accent,.25f).withAlpha(alpha));
                    g.fillRoundedRectangle(r.reduced(1).toFloat(),4.f);
                    if(vel>=110){g.setColour(LibertyUI::Colour::textPrimary.withAlpha(.78f));g.fillEllipse((float)r.getCentreX()-2.f,(float)r.getCentreY()-2.f,4.f,4.f);}
                }
                if(lit){g.setColour(LibertyUI::Colour::accentLight);g.drawRoundedRectangle(r.toFloat(),4.f,1.3f);}
            }
        }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto found=hit(e.getPosition());
        if(found.first<0)return;
        selectedPad=found.first;
        if(onPadSelected)onPadSelected(selectedPad);
        const int old=sampler.getPatternVelocity(found.first,found.second);
        if(e.mods.isPopupMenu())dragValue=0;
        else if(e.mods.isShiftDown())dragValue=old==0?48:old<80?80:old<112?112:old<127?127:0;
        else dragValue=old>0?0:100;
        sampler.setPatternVelocity(found.first,found.second,dragValue);
        lastCell=found;repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto found=hit(e.getPosition());
        if(found.first<0||found==lastCell)return;
        sampler.setPatternVelocity(found.first,found.second,dragValue);
        lastCell=found;repaint();
    }
private:
    std::pair<int,int> hit(juce::Point<int> p) const noexcept
    {
        if(p.x<148||p.x>=getWidth()||p.y<27||p.y>=getHeight())return {-1,-1};
        const auto r=cell(0,0),w=r.getWidth(),h=r.getHeight();
        const int step=(p.x-148)/w,pad=(p.y-27)/h;
        if(pad<0||pad>=LibertyDrumSampler::padCount||step<0||step>=LibertyDrumSampler::patternSteps)return {-1,-1};
        return {pad,step};
    }
    void timerCallback() override{if(isShowing()&&sampler.isPatternPreviewPlaying())repaint();}
    LibertyDrumSampler& sampler;
    int selectedPad=0,dragValue=100;
    std::pair<int,int> lastCell{-1,-1};
};
