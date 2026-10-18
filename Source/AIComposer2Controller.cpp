#define private public
#include "MainComponent.h"
#undef private
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <map>
#include <memory>
#include <random>
#include <vector>
bool commitLibertyAIGeneratedNotes(MainComponent&,const std::vector<MidiEngine::NoteEvent>&,bool,double,double);
namespace{juce::Component*findAIPanel(MainComponent&o){for(int i=0;i<o.getNumChildComponents();++i){auto*c=o.getChildComponent(i);if(!c)continue;for(int j=0;j<c->getNumChildComponents();++j)if(auto*l=dynamic_cast<juce::Label*>(c->getChildComponent(j)))if(l->getText()=="LIBERTY AI MUSIC")return c;}return nullptr;}
class Composer final:public juce::Component{public:explicit Composer(MainComponent&o):owner(o){title.setText("COMPOSITION IA",juce::dontSendNotification);title.setColour(juce::Label::textColourId,juce::Colour(0xffffc857));title.setFont(juce::Font(14,juce::Font::bold));addAndMakeVisible(title);close.setButtonText("FERMER");close.onClick=[this]{if(auto* w = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent())) w->setVisible(false); else setVisible(false);};addAndMakeVisible(close);style.addItemList({"POP","ROCK","METAL","FUNK","BLUES","AMBIENT",juce::String::fromUTF8("CIN\xC3""\x89""MATIQUE"),juce::String::fromUTF8("\xC3""\x89""LECTRONIQUE")},1);mood.addItemList({"SOMBRE","LUMINEUX","TENDU",juce::String::fromUTF8("\xC3""\x89""PIQUE"),juce::String::fromUTF8("M\xC3""\x89""LANCOLIQUE"),"AGRESSIF",juce::String::fromUTF8("R\xC3""\x8A""VEUR")},1);key.addItemList({"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"},1);scale.addItemList({"MINEUR","MAJEUR","DORIAN","PENTATONIQUE MINEURE"},1);bars.addItemList({"2 MESURES","4 MESURES","8 MESURES","16 MESURES"},1);part.addItemList({"COMPLET","ACCORDS","BASSE",juce::String::fromUTF8("M\xC3""\x89""LODIE"),"RIFF"},1);progression.addItemList({"i-VI-III-VII","I-V-vi-IV","I-IV-V-I","ii-V-I","i-iv-V-i"},1);style.setSelectedId(3);mood.setSelectedId(1);key.setSelectedId(10);scale.setSelectedId(1);bars.setSelectedId(3);part.setSelectedId(1);progression.setSelectedId(1);for(auto*c:{&style,&mood,&key,&scale,&bars,&part,&progression})addAndMakeVisible(*c);setup(complexity,55);setup(energy,70);setup(human,18);generate.setButtonText(juce::String::fromUTF8("CR\xC3""\x89""ER LA SECTION"));generate.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff31465b));generate.onClick=[this]{make();};addAndMakeVisible(generate);status.setColour(juce::Label::textColourId,juce::Colour(0xff8edcff));addAndMakeVisible(status);setOpaque(true);}void paint(juce::Graphics&g)override{g.fillAll(juce::Colour(0xff121820));g.setColour(juce::Colour(0xffffc857));g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1),6,1.5f);g.setColour(juce::Colour(0xffaab5c0));g.setFont(10);g.drawText(juce::String::fromUTF8("COMPLEXIT\xC3""\x89"""),10,150,90,20,juce::Justification::centredLeft);g.drawText(juce::String::fromUTF8("\xC3""\x89""NERGIE"),10,180,90,20,juce::Justification::centredLeft);g.drawText("JEU HUMAIN",10,210,90,20,juce::Justification::centredLeft);}void resized()override{int w=getWidth(),half=(w-30)/2;title.setBounds(10,5,w-145,22);close.setBounds(w-132,5,122,24);style.setBounds(10,34,half,25);mood.setBounds(20+half,34,half,25);key.setBounds(10,63,70,25);scale.setBounds(86,63,145,25);bars.setBounds(237,63,w-247,25);part.setBounds(10,92,half,25);progression.setBounds(20+half,92,half,25);complexity.setBounds(102,148,w-112,24);energy.setBounds(102,178,w-112,24);human.setBounds(102,208,w-112,24);generate.setBounds(10,240,w-20,32);status.setBounds(10,274,w-20,22);}private:void setup(juce::Slider&s,double v){s.setRange(0,100,1);s.setValue(v);s.setTextBoxStyle(juce::Slider::TextBoxRight,false,44,18);addAndMakeVisible(s);}std::vector<int>scaleNotes()const{switch(scale.getSelectedId()){case 2:return{0,2,4,5,7,9,11};case 3:return{0,2,3,5,7,9,10};case 4:return{0,3,5,7,10};default:return{0,2,3,5,7,8,10};}}int barCount()const{static int b[]={2,4,8,16};return b[juce::jlimit(0,3,bars.getSelectedItemIndex())];}double secBar()const{return(60.0/juce::jmax(1.0,owner.tempoBpm))*owner.timeSignatureNumerator*(4.0/juce::jmax(1,owner.timeSignatureDenominator));}int moodOffset()const{switch(mood.getSelectedId()){case 2:return 12;case 4:return 12;case 5:return-5;case 7:return 7;default:return 0;}}int styleDensity()const{switch(style.getSelectedId()){case 3:return 8;case 4:return 8;case 6:return 2;case 7:return 3;case 8:return 8;default:return 4;}}void add(std::vector<MidiEngine::NoteEvent>&o,std::int64_t st,std::int64_t len,int pitch,int vel){MidiEngine::NoteEvent n;n.startTick=juce::jmax<std::int64_t>(0,st);n.lengthTicks=juce::jmax<std::int64_t>(1,len);n.pitch=(std::uint8_t)juce::jlimit(0,127,pitch);n.velocity=(std::uint8_t)juce::jlimit(1,127,vel);n.channel=1;o.push_back(n);}void make(){int bc=barCount(),root=key.getSelectedItemIndex(),mode=part.getSelectedId();std::vector<int> sc=scaleNotes();const std::int64_t barTick=MidiEngine::ticksPerMeasure(owner.timeSignatureNumerator,owner.timeSignatureDenominator);const std::int64_t q=MidiEngine::ticksPerQuarterNote;std::vector<MidiEngine::NoteEvent>out;std::mt19937 rng((unsigned)juce::Time::getMillisecondCounter());int degrees[4]={0,5,2,6};if(progression.getSelectedId()==2){degrees[0]=0;degrees[1]=4;degrees[2]=5;degrees[3]=3;}else if(progression.getSelectedId()==3){degrees[0]=0;degrees[1]=3;degrees[2]=4;degrees[3]=0;}else if(progression.getSelectedId()==4){degrees[0]=1;degrees[1]=4;degrees[2]=0;degrees[3]=0;}else if(progression.getSelectedId()==5){degrees[0]=0;degrees[1]=3;degrees[2]=4;degrees[3]=0;}int vel=juce::jlimit(45,125,48+(int)energy.getValue()*3/4),density=juce::jlimit(2,8,(styleDensity()+(int)complexity.getValue()/20)/2),feel=(int)human.getValue();for(int b=0;b<bc;++b){int deg=degrees[b%4]%(int)sc.size(),chordRoot=48+root+sc[(size_t)deg]+moodOffset();if(mode==1||mode==2){int third=sc[(size_t)((deg+2)%(int)sc.size())]+((deg+2)>=(int)sc.size()?12:0),fifth=sc[(size_t)((deg+4)%(int)sc.size())]+((deg+4)>=(int)sc.size()?12:0);add(out,(std::int64_t)b*barTick,barTick,chordRoot,vel);add(out,(std::int64_t)b*barTick,barTick,48+root+third+moodOffset(),vel-5);add(out,(std::int64_t)b*barTick,barTick,48+root+fifth+moodOffset(),vel-8);}if(mode==1||mode==3){int steps=(style.getSelectedId()==4||style.getSelectedId()==3)?8:4;for(int s=0;s<steps;++s){if((int)(rng()%100)>juce::jlimit(30,95,(int)energy.getValue()+15))continue;std::int64_t st=(std::int64_t)b*barTick+(std::int64_t)s*barTick/steps;int p=36+root+sc[(size_t)deg]+((s%4==3&&style.getSelectedId()!=3)?7:0);add(out,st,barTick/steps*3/4,p,juce::jlimit(50,120,vel+5-(s%2)*10));}}if(mode==1||mode==4||mode==5){int steps=mode==5?juce::jmax(4,density):density;for(int s=0;s<steps;++s){int chance=juce::jlimit(35,98,(int)energy.getValue()+(mode==5?20:0));if((int)(rng()%100)>chance)continue;std::int64_t st=(std::int64_t)b*barTick+(std::int64_t)s*barTick/steps;if(feel>0)st+=(int)(rng()%(feel+1))-feel/2;int idx=(deg+s+(int)(rng()%3))%(int)sc.size(),oct=(style.getSelectedId()==6?24:12);int p=48+root+oct+sc[(size_t)idx]+moodOffset();if(style.getSelectedId()==3&&mode==5)p-=12;if(mood.getSelectedId()==1||mood.getSelectedId()==6)p-=5;int lv=vel-12+(int)(rng()%22);add(out,st,juce::jmax<std::int64_t>(q/8,barTick/steps*(mode==5?1:3)/4),p,lv);}}}bool ok=commitLibertyAIGeneratedNotes(owner,out,true,juce::jmax(0.0,owner.midiClipStartSeconds),bc*secBar());juce::String names[]={"","COMPLET","ACCORDS","BASS",juce::String::fromUTF8("M\xC3""\x89""LODIE"),"RIFF"};status.setText(ok?names[mode]+" | "+key.getText()+" "+scale.getText()+" | "+bars.getText():"Creation impossible",juce::dontSendNotification);owner.repaint();}MainComponent&owner;juce::Label title,status;juce::ComboBox style,mood,key,scale,bars,part,progression;juce::Slider complexity,energy,human;juce::TextButton generate,close;};
class ComposerWindow final : public juce::DocumentWindow
{
public:
    explicit ComposerWindow(MainComponent& owner)
        : juce::DocumentWindow("Liberty - Composition IA",
                               juce::Colour(0xff11161c),
                               juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new Composer(owner), true);
        setResizable(true, false);
        setResizeLimits(650, 380, 1600, 1000);
        centreWithSize(850, 460);
    }
    void closeButtonPressed() override { setVisible(false); }
};

class Controller final
{
public:
    explicit Controller(MainComponent& component) : owner(component) {}
    void openComposer()
    {
        if (!window) window = std::make_unique<ComposerWindow>(owner);
        window->setVisible(true);
        window->toFront(true);
    }
private:
    MainComponent& owner;
    std::unique_ptr<ComposerWindow> window;
};
std::map<MainComponent*,std::unique_ptr<Controller>>cs;class Boot:private juce::Timer{public:Boot(){startTimerHz(3);}~Boot(){shutdown();}void shutdown(){stopTimer();cs.clear();}private:void timerCallback()override{auto&d=juce::Desktop::getInstance();for(int i=0;i<d.getNumComponents();++i)if(auto*w=dynamic_cast<juce::DocumentWindow*>(d.getComponent(i)))if(auto*content=w->getContentComponent())for(int childIndex=-1;childIndex<content->getNumChildComponents();++childIndex)if(auto*m=dynamic_cast<MainComponent*>(childIndex<0?content:content->getChildComponent(childIndex)))if(!cs.count(m)){cs.emplace(m,std::make_unique<Controller>(*m));stopTimer();return;}}}boot;}
void shutdownLibertyAIComposer2Controller(){boot.shutdown();}

void showLibertyAIComposer2(MainComponent& owner)
{
    auto it = cs.find(&owner);
    if (it == cs.end()) it = cs.emplace(&owner, std::make_unique<Controller>(owner)).first;
    it->second->openComposer();
}
