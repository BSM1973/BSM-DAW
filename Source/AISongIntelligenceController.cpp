#define private public
#include "MainComponent.h"
#undef private
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

bool commitLibertyAIGeneratedClip(MainComponent& owner, bool instrumentTrack);

namespace
{
class SongBrain final : public juce::Component
{
public:
 explicit SongBrain(MainComponent&o):owner(o)
 {
  title.setText("AI SONG BRAIN",juce::dontSendNotification); title.setColour(juce::Label::textColourId,juce::Colour(0xff8edcff)); title.setFont(juce::Font(12.0f,juce::Font::bold)); addAndMakeVisible(title);
  add(analyse,"ANALYSE IDEA",[this]{analyseIdea();}); add(build,"SMART DEVELOP",[this]{smartDevelop();}); add(transition,"TRANSITION",[this]{makeTransition();}); add(breakdown,"BREAKDOWN",[this]{makeBreakdown();}); add(climax,"CLIMAX",[this]{makeClimax();});
  status.setColour(juce::Label::textColourId,juce::Colour(0xffb8c3cf)); status.setFont(juce::Font(10.0f)); status.setJustificationType(juce::Justification::topLeft); status.setText("Analyse the active idea, then develop it musically.",juce::dontSendNotification); addAndMakeVisible(status); setOpaque(true);
 }
 void paint(juce::Graphics&g)override{g.fillAll(juce::Colour(0xff11161c));g.setColour(juce::Colour(0xff2b3540));g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1),6,1);}
 void resized()override{title.setBounds(10,5,getWidth()-20,20);int gap=6,x=10,w=(getWidth()-20-gap)/2;analyse.setBounds(x,30,w,30);build.setBounds(x+w+gap,30,w,30);int w3=(getWidth()-20-gap*2)/3;transition.setBounds(x,66,w3,28);breakdown.setBounds(x+w3+gap,66,w3,28);climax.setBounds(x+2*(w3+gap),66,getWidth()-10-(x+2*(w3+gap)),28);status.setBounds(10,100,getWidth()-20,juce::jmax(24,getHeight()-106));}
private:
 template<class F>void add(juce::TextButton&b,const char*t,F f){b.setButtonText(t);b.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff202832));b.setColour(juce::TextButton::textColourOffId,juce::Colours::white);b.onClick=f;addAndMakeVisible(b);}
 std::vector<MidiEngine::NoteEvent> notes()const{return owner.midiEngine.getNotesCopy();}
 void analyseIdea(){auto n=notes();if(n.empty()){say("No active idea to analyse.");return;}int lo=127,hi=0,vel=0,drums=0;std::int64_t end=0;for(auto&a:n){lo=juce::jmin(lo,(int)a.pitch);hi=juce::jmax(hi,(int)a.pitch);vel+=(int)a.velocity;if(a.channel==10)++drums;end=juce::jmax(end,a.startTick+a.lengthTicks);}double q=(double)end/(double)MidiEngine::ticksPerQuarterNote;double density=q>0?(double)n.size()/q:0;juce::String mood=density>3.5?"dense / energetic":density<1.2?"open / spacious":"balanced";juce::String type=drums>(int)n.size()/2?"drum idea":"melodic/harmonic idea";say(type+" | "+mood+" | range "+juce::String(lo)+"-"+juce::String(hi)+" | avg velocity "+juce::String(vel/juce::jmax(1,(int)n.size())));}
 bool create(const std::vector<MidiEngine::NoteEvent>&n,double start,const juce::String&msg){if(n.empty())return false;owner.midiEngine.clear();for(auto&a:n)owner.midiEngine.addNote(a.startTick,a.lengthTicks,a.pitch,a.velocity,a.channel);owner.midiClipStartSeconds=start;owner.midiClipLengthUserDefined=false;owner.updateMidiClipTiming();bool ok=commitLibertyAIGeneratedClip(owner,true);say(ok?msg:"Clip creation failed");return ok;}
 void smartDevelop(){auto src=notes();if(src.empty()){say("No active idea.");return;}double base=owner.midiClipStartSeconds,len=juce::jmax(.25,owner.midiClipLengthSeconds);for(int v=0;v<3;++v){auto out=src;for(size_t i=0;i<out.size();++i){auto&n=out[i];n.velocity=(std::uint8_t)juce::jlimit(1,127,(int)n.velocity+(v+1)*5);if(n.channel!=10&&v>0&&(i%(5-v)==0))n.pitch=(std::uint8_t)juce::jlimit(0,127,(int)n.pitch+(v==1?7:12));if(v==2&&i%7==0)n.lengthTicks=juce::jmax<std::int64_t>(MidiEngine::ticksPerQuarterNote/8,n.lengthTicks/2);}create(out,base+len*(v+1),"Smart development created");}say("3 progressive sections created: lift, expansion, climax");}
 void makeTransition(){auto src=notes();if(src.empty()){say("No active idea.");return;}auto out=src;auto total=juce::jmax<std::int64_t>(MidiEngine::ticksPerQuarterNote*4,owner.midiEngine.getLengthTicks());auto start=juce::jmax<std::int64_t>(0,total-MidiEngine::ticksPerQuarterNote);int last=src.back().pitch;for(int i=0;i<8;++i){MidiEngine::NoteEvent n;n.startTick=start+i*(MidiEngine::ticksPerQuarterNote/8);n.lengthTicks=MidiEngine::ticksPerQuarterNote/12;n.pitch=(std::uint8_t)juce::jlimit(0,127,last+i);n.velocity=(std::uint8_t)juce::jlimit(1,127,72+i*6);n.channel=src.back().channel;out.push_back(n);}create(out,owner.midiClipStartSeconds+owner.midiClipLengthSeconds,"AI transition created");}
 void makeBreakdown(){auto src=notes();if(src.empty()){say("No active idea.");return;}std::vector<MidiEngine::NoteEvent>out;for(size_t i=0;i<src.size();++i)if(i%2==0){auto n=src[i];n.velocity=(std::uint8_t)juce::jlimit(1,127,(int)n.velocity-20);if(n.channel!=10)n.pitch=(std::uint8_t)juce::jlimit(0,127,(int)n.pitch-12);out.push_back(n);}create(out,owner.midiClipStartSeconds+owner.midiClipLengthSeconds,"AI breakdown created");}
 void makeClimax(){auto src=notes();if(src.empty()){say("No active idea.");return;}auto out=src;for(auto&n:out)n.velocity=(std::uint8_t)juce::jlimit(1,127,(int)n.velocity+14);size_t count=out.size();for(size_t i=0;i<count;++i)if(out[i].channel!=10&&i%4==0){auto n=out[i];n.pitch=(std::uint8_t)juce::jlimit(0,127,(int)n.pitch+12);n.velocity=(std::uint8_t)juce::jmax(1,(int)n.velocity-16);out.push_back(n);}create(out,owner.midiClipStartSeconds+owner.midiClipLengthSeconds,"AI climax created");}
 void say(const juce::String&s){status.setText(s,juce::dontSendNotification);}
 MainComponent&owner;juce::Label title,status;juce::TextButton analyse,build,transition,breakdown,climax;
};
class Controller:private juce::Timer{public:explicit Controller(MainComponent&o):owner(o){startTimerHz(4);}~Controller(){shutdown();}void shutdown(){if(stopped.exchange(true))return;stopTimer();brain.reset();}private:juce::Component*findPanel(){for(int i=0;i<owner.getNumChildComponents();++i){auto*c=owner.getChildComponent(i);if(!c)continue;for(int j=0;j<c->getNumChildComponents();++j)if(auto*l=dynamic_cast<juce::Label*>(c->getChildComponent(j)))if(l->getText()=="LIBERTY AI MUSIC")return c;}return nullptr;}void timerCallback()override{auto*p=findPanel();if(!p)return;if(parent!=p||!brain){brain.reset();parent=p;brain=std::make_unique<SongBrain>(owner);parent->addAndMakeVisible(*brain);}brain->setBounds(20,juce::jmax(910,parent->getHeight()-298),juce::jmax(280,parent->getWidth()-40),142);}MainComponent&owner;juce::Component*parent=nullptr;std::unique_ptr<SongBrain>brain;std::atomic<bool>stopped{false};};
std::map<MainComponent*,std::unique_ptr<Controller>>controllers;class Bootstrap:private juce::Timer{public:Bootstrap(){startTimerHz(4);}~Bootstrap(){shutdown();}void shutdown(){stopTimer();for(auto&i:controllers)if(i.second)i.second->shutdown();controllers.clear();}private:void timerCallback()override{auto&d=juce::Desktop::getInstance();for(int i=0;i<d.getNumComponents();++i)if(auto*w=dynamic_cast<juce::DocumentWindow*>(d.getComponent(i)))if(auto*m=dynamic_cast<MainComponent*>(w->getContentComponent()))if(!controllers.count(m))controllers.emplace(m,std::make_unique<Controller>(*m));}};Bootstrap bootstrap;
}
void shutdownLibertyAISongIntelligenceController(){bootstrap.shutdown();}
