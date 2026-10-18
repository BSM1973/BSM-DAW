#define private public
#include "MainComponent.h"
#undef private
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <random>
bool commitLibertyAIGeneratedClip(MainComponent&,bool);void prepareLibertyAIGeneration(MainComponent&);
namespace{constexpr int panelWidth=460,topBarHeight=76;juce::Component*findBrowserPanel(MainComponent&o){for(int i=0;i<o.getNumChildComponents();++i){auto*c=o.getChildComponent(i);if(!c)continue;bool f=false,p=false;for(int j=0;j<c->getNumChildComponents();++j)if(auto*b=dynamic_cast<juce::TextButton*>(c->getChildComponent(j))){f|=b->getButtonText()=="FILES";p|=b->getButtonText()=="PLUGINS";}if(f&&p)return c;}return nullptr;}
class LibertyAIPanel final:public juce::Component{public:explicit LibertyAIPanel(MainComponent&o):owner(o){setComponentID("liberty-ai-panel");title.setText("LIBERTY MUSIQUE IA",juce::dontSendNotification);title.setColour(juce::Label::textColourId,juce::Colours::white);title.setFont(juce::Font(18,juce::Font::bold));addAndMakeVisible(title);prompt.setMultiLine(true);prompt.setReturnKeyStartsNewLine(true);prompt.setTextToShowWhenEmpty("Exemple : accords metal en la mineur sur 8 mesures",juce::Colour(0xff727b86));prompt.setColour(juce::TextEditor::backgroundColourId,juce::Colour(0xff11151a));prompt.setColour(juce::TextEditor::textColourId,juce::Colours::white);addAndMakeVisible(prompt);for(auto*b:{&generate,&chords,&drums,&bass,&riff,&solo,&humanize,&variation,&clear,&close}){b->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff202832));b->setColour(juce::TextButton::textColourOffId,juce::Colours::white);addAndMakeVisible(*b);}generate.setButtonText(juce::String::fromUTF8("G\xC3""\x89""N\xC3""\x89""RER"));chords.setButtonText("ACCORDS");drums.setButtonText("BATTERIE");bass.setButtonText("BASSE");riff.setButtonText("RIFF GUITARE");solo.setButtonText("SOLO");humanize.setButtonText("HUMANISER");variation.setButtonText("VARIATION");clear.setButtonText("EFFACER MIDI");close.setButtonText("FERMER");targetBox.addItem("INSTRUMENT",1);targetBox.addItem("MIDI",2);targetBox.setSelectedId(1);addAndMakeVisible(targetBox);keyBox.addItemList({"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"},1);keyBox.setSelectedItemIndex(9);scaleBox.addItemList({"MINEUR","MAJEUR","DORIEN","PENTATONIQUE MINEURE"},1);scaleBox.setSelectedItemIndex(0);barsBox.addItemList({"2 MESURES","4 MESURES","8 MESURES","16 MESURES"},1);barsBox.setSelectedItemIndex(2);for(auto*b:{&keyBox,&scaleBox,&barsBox})addAndMakeVisible(*b);status.setColour(juce::Label::textColourId,juce::Colour(0xff8edcff));status.setFont(juce::Font(11));status.setComponentID("liberty-ai-status");status.setText("Cible : INSTRUMENT",juce::dontSendNotification);addAndMakeVisible(status);close.onClick=[this]{setVisible(false);};generate.onClick=[this]{generateChords();};chords.onClick=[this]{generateChords();};drums.onClick=[this]{generateDrums();};bass.onClick=[this]{generateBass();};riff.onClick=[this]{generateRiff();};solo.onClick=[this]{generateSolo();};humanize.onClick=[this]{humanizeMidi();};variation.onClick=[this]{createVariation();};clear.onClick=[this]{owner.midiEngine.clear();owner.updateMidiClipTiming();status.setText(juce::String::fromUTF8("Notes MIDI effac\xC3""\xA9""es"),juce::dontSendNotification);owner.repaint();};setOpaque(true);}void paint(juce::Graphics&g)override{g.fillAll(juce::Colour(0xff0d1116));g.setColour(juce::Colour(0xff29333e));g.drawVerticalLine(0,0,(float)getHeight());}void resized()override{title.setBounds(18,10,getWidth()-110,30);close.setBounds(getWidth()-82,10,66,26);prompt.setBounds(20,74,getWidth()-40,68);const int selectorWidth = juce::jmax(0, getWidth() - 40);const int selectorGap = 6;const int keyWidth = juce::jmax(70, selectorWidth * 22 / 100);const int barsWidth = juce::jmax(100, selectorWidth * 30 / 100);const int scaleWidth = juce::jmax(80, selectorWidth - keyWidth - barsWidth - 2 * selectorGap);keyBox.setBounds(20,186,keyWidth,28);scaleBox.setBounds(20+keyWidth+selectorGap,186,scaleWidth,28);barsBox.setBounds(20+keyWidth+selectorGap+scaleWidth+selectorGap,186,barsWidth,28);const int actionWidth = juce::jmax(0, getWidth() - 40);const int targetWidth = juce::jmin(150, juce::jmax(105, actionWidth * 38 / 100));targetBox.setBounds(20,224,targetWidth,30);generate.setBounds(20+targetWidth+8,224,juce::jmax(0, actionWidth-targetWidth-8),36);const int buttonGap = 6;const int buttonArea = juce::jmax(0, getWidth() - 40);const int w = juce::jmax(0, (buttonArea - 2 * buttonGap) / 3);const int lastW = juce::jmax(0, buttonArea - 2 * (w + buttonGap));chords.setBounds(20,328,w,32);drums.setBounds(20+w+buttonGap,328,w,32);bass.setBounds(20+2*(w+buttonGap),328,lastW,32);riff.setBounds(20,368,w,32);solo.setBounds(20+w+buttonGap,368,w,32);variation.setBounds(20+2*(w+buttonGap),368,lastW,32);const int halfW = juce::jmax(0, (buttonArea-buttonGap)/2);humanize.setBounds(20,408,halfW,32);clear.setBounds(20+halfW+buttonGap,408,juce::jmax(0,buttonArea-halfW-buttonGap),32);status.setBounds(20,452,getWidth()-40,48);}private:int bars()const{static int v[]={2,4,8,16};return v[juce::jlimit(0,3,barsBox.getSelectedItemIndex())];}int root()const{return juce::jlimit(0,11,keyBox.getSelectedItemIndex());}std::vector<int>scale()const{switch(scaleBox.getSelectedItemIndex()){case 1:return{0,2,4,5,7,9,11};case 2:return{0,2,3,5,7,9,10};case 3:return{0,3,5,7,10};default:return{0,2,3,5,7,8,10};}}void prep(){prepareLibertyAIGeneration(owner);owner.midiEngine.clear();owner.midiClipStartSeconds=juce::jmax(0.0,owner.midiClipStartSeconds);}void finish(const juce::String&s){owner.updateMidiClipTiming();bool ok=commitLibertyAIGeneratedClip(owner,targetBox.getSelectedId()!=2);status.setText(ok?s:juce::String::fromUTF8("\xC3""\x89""chec de cr\xC3""\xA9""ation du clip IA"),juce::dontSendNotification);owner.repaint();}void generateChords(){prep();auto sc=scale();auto m=MidiEngine::ticksPerMeasure(owner.timeSignatureNumerator,owner.timeSignatureDenominator);for(int b=0;b<bars();++b){int d=b%4==0?0:b%4==1?3:b%4==2?4:0;for(int k:{0,2,4})owner.midiEngine.addNote((std::int64_t)b*m,m,48+root()+sc[(d+k)%sc.size()],96,1);}finish(juce::String::fromUTF8("Accords IA g\xC3""\xA9""n\xC3""\xA9""r\xC3""\xA9""s"));}void generateDrums(){prep();auto q=MidiEngine::ticksPerQuarterNote;for(int b=0;b<bars()*4;++b){owner.midiEngine.addNote((std::int64_t)b*q,q/4,b%4==0?36:42,b%4==0?112:82,10);if(b%4==2)owner.midiEngine.addNote((std::int64_t)b*q,q/4,38,104,10);}finish(juce::String::fromUTF8("Batterie IA g\xC3""\xA9""n\xC3""\xA9""r\xC3""\xA9""e"));}void generateBass(){prep();auto q=MidiEngine::ticksPerQuarterNote;for(int i=0;i<bars()*4;++i)owner.midiEngine.addNote((std::int64_t)i*q,q*3/4,36+root()+(i%4==2?7:0),98,1);finish(juce::String::fromUTF8("Basse IA g\xC3""\xA9""n\xC3""\xA9""r\xC3""\xA9""e"));}void generateRiff(){prep();auto sc=scale();auto e=MidiEngine::ticksPerQuarterNote/2;for(int i=0;i<bars()*8;++i)owner.midiEngine.addNote((std::int64_t)i*e,e*3/4,48+root()+sc[(i*2+i/3)%sc.size()],100,1);finish(juce::String::fromUTF8("Riff IA g\xC3""\xA9""n\xC3""\xA9""r\xC3""\xA9"""));}void generateSolo(){prep();auto sc=scale();auto e=MidiEngine::ticksPerQuarterNote/2;std::mt19937 r((unsigned)juce::Time::getMillisecondCounter());for(int i=0;i<bars()*8;++i)if(r()%100>18)owner.midiEngine.addNote((std::int64_t)i*e,e*(1+r()%3)/2,60+root()+sc[r()%sc.size()],85+r()%30,1);finish(juce::String::fromUTF8("Solo IA g\xC3""\xA9""n\xC3""\xA9""r\xC3""\xA9"""));}void humanizeMidi(){auto n=owner.midiEngine.getNotesCopy();if(n.empty())return;owner.midiEngine.clear();std::mt19937 r((unsigned)juce::Time::getMillisecondCounter());for(auto&a:n)owner.midiEngine.addNote(juce::jmax<std::int64_t>(0,a.startTick+(int)(r()%21)-10),a.lengthTicks,a.pitch,juce::jlimit(1,127,(int)a.velocity+(int)(r()%9)-4),a.channel);owner.updateMidiClipTiming();status.setText(juce::String::fromUTF8("Humanisation MIDI termin\xC3""\xA9""e"),juce::dontSendNotification);owner.repaint();}void createVariation(){auto n=owner.midiEngine.getNotesCopy();if(n.empty())return;owner.midiEngine.clear();std::mt19937 r((unsigned)juce::Time::getMillisecondCounter());for(auto&a:n)if(r()%100>=10)owner.midiEngine.addNote(a.startTick,a.lengthTicks,juce::jlimit(0,127,(int)a.pitch+(a.channel!=10&&r()%100<24?(r()%2?2:-2):0)),a.velocity,a.channel);owner.updateMidiClipTiming();status.setText(juce::String::fromUTF8("Variation MIDI cr\xC3""\xA9""\xC3""\xA9""e"),juce::dontSendNotification);owner.repaint();}MainComponent&owner;juce::Label title,status;juce::TextEditor prompt;juce::ComboBox keyBox,scaleBox,barsBox,targetBox;juce::TextButton generate,chords,drums,bass,riff,solo,humanize,variation,clear,close;};
class Controller final : private juce::Timer
{
public:
    explicit Controller(MainComponent& component) : owner(component), panel(component)
    {
        owner.addChildComponent(panel);
        startTimerHz(2);
    }
    ~Controller() { shutdown(); }
    void openPanel()
    {
        if (stopped.load()) return;
        panel.setVisible(true);
        panel.toFront(false);
    }
    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        panel.setVisible(false);
    }
private:
    void timerCallback() override
    {
        if (stopped.load()) return;
        int right = owner.getWidth();
        if (auto* browser = findBrowserPanel(owner))
            if (browser->isVisible() && browser->getWidth() > 0)
                right = browser->getX();
        const int available = juce::jmax(1, right - 220);
        const int width = juce::jmin(panelWidth, juce::jmin(juce::jmax(300, available), juce::jmax(1, right)));
        const auto bounds = juce::Rectangle<int>(
            juce::jmax(0, right - width), topBarHeight, width,
            juce::jmax(1, owner.getHeight() - topBarHeight));
        if (panel.getBounds() != bounds) panel.setBounds(bounds);
    }
    MainComponent& owner;
    LibertyAIPanel panel;
    std::atomic<bool> stopped { false };
};
std::map<MainComponent*,std::unique_ptr<Controller>>controllers;class Bootstrap:private juce::Timer{public:Bootstrap(){startTimerHz(4);}~Bootstrap(){shutdown();}void shutdown(){stopTimer();for(auto&i:controllers)if(i.second)i.second->shutdown();controllers.clear();}private:void timerCallback()override{auto&d=juce::Desktop::getInstance();for(int i=0;i<d.getNumComponents();++i)if(auto*w=dynamic_cast<juce::DocumentWindow*>(d.getComponent(i)))if(auto*content=w->getContentComponent())for(int j=-1;j<content->getNumChildComponents();++j)if(auto*m=dynamic_cast<MainComponent*>(j<0?content:content->getChildComponent(j)))if(!controllers.count(m)){controllers.emplace(m,std::make_unique<Controller>(*m));stopTimer();return;}}};Bootstrap bootstrap;}
void showLibertyAIMusicPanel(MainComponent& owner)
{
    auto it = controllers.find(&owner);
    if (it == controllers.end())
        it = controllers.emplace(&owner, std::make_unique<Controller>(owner)).first;
    it->second->openPanel();
}
void shutdownLibertyAIController(){bootstrap.shutdown();}
