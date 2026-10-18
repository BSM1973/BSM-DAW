#define private public
#include "MainComponent.h"
#undef private

#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <random>
#include <vector>

bool commitLibertyAIGeneratedClip(MainComponent& owner, bool instrumentTrack);
bool isLibertyActiveMidiClipInstrument(MainComponent& owner);

namespace
{
struct Section { const char* name; int bars; float energy; int transpose; };

class AIArranger final : public juce::Component
{
public:
    explicit AIArranger(MainComponent& o) : owner(o)
    {
        title.setText("ARRANGEMENT IA", juce::dontSendNotification);
        title.setColour(juce::Label::textColourId, juce::Colour(0xff8edcff));
        title.setFont(juce::Font(12.0f, juce::Font::bold));
        addAndMakeVisible(title);
        addButton(full, juce::String::fromUTF8("CR\xC3""\x89""ER UN MORCEAU"), [this] { buildSong(false); });
        addButton(shortSong, "VERSION COURTE", [this] { buildSong(true); });
        addButton(intro, juce::String::fromUTF8("CR\xC3""\x89""ER INTRO"), [this] { makeSection("INTRO", 4, 0.55f, 0); });
        addButton(chorus, juce::String::fromUTF8("CR\xC3""\x89""ER REFRAIN"), [this] { makeSection("CHORUS", 8, 1.15f, 12); });
        addButton(bridge, juce::String::fromUTF8("CR\xC3""\x89""ER PONT"), [this] { makeSection("BRIDGE", 4, 0.82f, 5); });
        status.setColour(juce::Label::textColourId, juce::Colour(0xffb8c3cf));
        status.setFont(juce::Font(10.0f));
        status.setJustificationType(juce::Justification::topLeft);
        status.setText(juce::String::fromUTF8("S\xC3""\xA9""lectionnez une id\xC3""\xA9""e Instrument/MIDI pour cr\xC3""\xA9""er les sections du morceau."), juce::dontSendNotification);
        addAndMakeVisible(status);
        setOpaque(true);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff11161c));
        g.setColour(juce::Colour(0xff2b3540));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 6.0f, 1.0f);
    }

    void resized() override
    {
        title.setBounds(10, 5, getWidth()-20, 20);
        const int gap=6, x=10, w=(getWidth()-20-gap)/2;
        full.setBounds(x,30,w,30); shortSong.setBounds(x+w+gap,30,w,30);
        const int w3=(getWidth()-20-gap*2)/3;
        intro.setBounds(x,66,w3,28); chorus.setBounds(x+w3+gap,66,w3,28);
        bridge.setBounds(x+2*(w3+gap),66,getWidth()-10-(x+2*(w3+gap)),28);
        status.setBounds(10,100,getWidth()-20,juce::jmax(24,getHeight()-106));
    }

private:
    template<class Fn> void addButton(juce::TextButton& b,const juce::String& t,Fn&& fn)
    {
        b.setButtonText(t); b.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff202832));
        b.setColour(juce::TextButton::textColourOffId,juce::Colours::white); b.onClick=std::forward<Fn>(fn); addAndMakeVisible(b);
    }

    std::vector<MidiEngine::NoteEvent> source() const { return owner.midiEngine.getNotesCopy(); }

    std::vector<MidiEngine::NoteEvent> adapt(const std::vector<MidiEngine::NoteEvent>& src, int bars, float energy, int transpose, unsigned seed)
    {
        const auto measure=MidiEngine::ticksPerMeasure(owner.timeSignatureNumerator,owner.timeSignatureDenominator);
        const auto wanted=(std::int64_t)bars*measure;
        const auto srcLen=juce::jmax<std::int64_t>(measure,owner.midiEngine.getLengthTicks());
        std::mt19937 rng(seed);
        std::vector<MidiEngine::NoteEvent> out;
        for(std::int64_t offset=0;offset<wanted;offset+=srcLen)
            for(const auto& s:src)
            {
                if(offset+s.startTick>=wanted) continue;
                if(energy<0.7f && (rng()%100)<28) continue;
                auto n=s; n.startTick+=offset;
                if(n.channel!=10 && transpose!=0 && (rng()%100)<38) n.pitch=(std::uint8_t)juce::jlimit(0,127,(int)n.pitch+transpose);
                n.velocity=(std::uint8_t)juce::jlimit(1,127,(int)std::lround((float)n.velocity*energy));
                if(energy>1.05f && n.channel!=10 && (rng()%100)<12)
                { auto o=n; o.pitch=(std::uint8_t)juce::jlimit(0,127,(int)o.pitch+12); o.velocity=(std::uint8_t)juce::jmax(1,(int)o.velocity-22); out.push_back(o); }
                out.push_back(n);
            }
        return out;
    }

    bool commitAt(const std::vector<MidiEngine::NoteEvent>& n,double start,const juce::String& label,bool instrumentTrack)
    {
        owner.midiEngine.clear();
        for(const auto& x:n) owner.midiEngine.addNote(x.startTick,x.lengthTicks,x.pitch,x.velocity,x.channel);
        owner.midiClipStartSeconds=start; owner.midiClipLengthUserDefined=false; owner.updateMidiClipTiming();
        const bool ok=commitLibertyAIGeneratedClip(owner,instrumentTrack);
        if(ok) status.setText(label+juce::String::fromUTF8(" cr\xC3""\xA9""\xC3""\xA9"""),juce::dontSendNotification);
        return ok;
    }

    void makeSection(const juce::String& name,int bars,float energy,int transpose)
    {
        const auto src=source(); if(src.empty()){status.setText(juce::String::fromUTF8("Aucune id\xC3""\xA9""e musicale active."),juce::dontSendNotification);return;}
        const bool instrumentTrack=isLibertyActiveMidiClipInstrument(owner);
        const auto out=adapt(src,bars,energy,transpose,(unsigned)juce::Time::getMillisecondCounter());
        commitAt(out,owner.midiClipStartSeconds+owner.midiClipLengthSeconds,name,instrumentTrack);
    }

    void buildSong(bool compact)
    {
        const auto original=source(); if(original.empty()){status.setText(juce::String::fromUTF8("Aucune id\xC3""\xA9""e Instrument/MIDI active \xC3""\xA0"" arranger."),juce::dontSendNotification);return;}
        const bool instrumentTrack=isLibertyActiveMidiClipInstrument(owner);
        const double base=owner.midiClipStartSeconds;
        const double secondsPerBar=(60.0/juce::jmax(1.0,owner.tempoBpm))*4.0*(4.0/(double)juce::jmax(1,owner.timeSignatureDenominator))*owner.timeSignatureNumerator/4.0;
        const std::vector<Section> plan = compact
          ? std::vector<Section>{{"INTRO",2,0.55f,0},{"VERSE",4,0.82f,0},{"CHORUS",4,1.15f,12},{"VERSE 2",4,0.9f,0},{"CHORUS 2",4,1.2f,12},{"OUTRO",2,0.58f,-12}}
          : std::vector<Section>{{"INTRO",4,0.52f,0},{"VERSE 1",8,0.8f,0},{"PRE",4,0.96f,5},{"CHORUS 1",8,1.16f,12},{"VERSE 2",8,0.88f,0},{"CHORUS 2",8,1.2f,12},{"BRIDGE",4,0.76f,5},{"FINAL CHORUS",8,1.24f,12},{"OUTRO",4,0.56f,-12}};
        double cursor=base; int made=0; unsigned seed=(unsigned)juce::Time::getMillisecondCounter();
        for(size_t i=0;i<plan.size();++i)
        {
            const auto& s=plan[i]; auto out=adapt(original,s.bars,s.energy,s.transpose,seed+(unsigned)i*1777u);
            if(commitAt(out,cursor,s.name,instrumentTrack)) ++made;
            cursor += secondsPerBar*(double)s.bars;
        }
        status.setText("AI ARRANGER: "+juce::String(made)+juce::String::fromUTF8(" sections cr\xC3""\xA9""\xC3""\xA9""es - clips Liberty modifiables"),juce::dontSendNotification);
        owner.repaint();
    }

    MainComponent& owner; juce::Label title,status; juce::TextButton full,shortSong,intro,chorus,bridge;
};

class ArrangerWindow final : public juce::DocumentWindow
{
public:
    explicit ArrangerWindow(MainComponent& owner)
        : juce::DocumentWindow("Liberty - Arrangement IA",
                               juce::Colour(0xff11161c),
                               juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new AIArranger(owner), true);
        setResizable(true, false);
        setResizeLimits(480, 180, 1600, 800);
        centreWithSize(700, 210);
    }

    void closeButtonPressed() override { setVisible(false); }
};

class Controller final
{
public:
    explicit Controller(MainComponent& component) : owner(component) {}
    ~Controller() { shutdown(); }

    void openArranger()
    {
        if (!window)
            window = std::make_unique<ArrangerWindow>(owner);
        window->setVisible(true);
        window->toFront(true);
    }

    void shutdown() { window.reset(); }

private:
    MainComponent& owner;
    std::unique_ptr<ArrangerWindow> window;
};
std::map<MainComponent*,std::unique_ptr<Controller>> controllers;
class Bootstrap final:private juce::Timer{public:Bootstrap(){startTimerHz(5);}~Bootstrap()override{shutdown();}void shutdown(){stopTimer();for(auto&i:controllers)if(i.second)i.second->shutdown();controllers.clear();}private:void timerCallback()override{auto&d=juce::Desktop::getInstance();for(int i=0;i<d.getNumComponents();++i)if(auto*w=dynamic_cast<juce::DocumentWindow*>(d.getComponent(i)))if(auto*content=w->getContentComponent())for(int childIndex=-1;childIndex<content->getNumChildComponents();++childIndex)if(auto*m=dynamic_cast<MainComponent*>(childIndex<0?content:content->getChildComponent(childIndex)))if(controllers.find(m)==controllers.end()){controllers.emplace(m,std::make_unique<Controller>(*m));stopTimer();return;}}};
Bootstrap bootstrap;
}
void shutdownLibertyAIArrangerController(){bootstrap.shutdown();}

void showLibertyAIArranger(MainComponent& owner)
{
    auto it = controllers.find(&owner);
    if (it == controllers.end())
        it = controllers.emplace(&owner, std::make_unique<Controller>(owner)).first;
    it->second->openArranger();
}
