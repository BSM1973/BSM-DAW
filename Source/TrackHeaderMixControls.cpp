#define private public
#include "MainComponent.h"
#undef private
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

int getLibertyTrackRowHeight() noexcept;
bool isLibertyMixConsoleVisible(MainComponent* owner);
bool isLibertyPerformVisible(MainComponent* owner);

namespace {
constexpr int rulerH=32;
class PanLF final:public juce::LookAndFeel_V4{public:void drawRotarySlider(juce::Graphics&g,int x,int y,int w,int h,float p,float a,float b,juce::Slider&)override{auto s=(float)juce::jmin(w,h)-2,cx=x+w*.5f,cy=y+h*.5f,r=s*.5f;g.setColour(juce::Colour(0xffff9a24));g.fillEllipse(cx-r,cy-r,s,s);g.setColour(juce::Colour(0xff5b2b00));g.fillEllipse(cx-r+3,cy-r+3,s-6,s-6);juce::Path q;q.addRoundedRectangle(-1.1f,-r*.72f,2.2f,r*.72f,1);g.setColour(juce::Colours::white);g.fillPath(q,juce::AffineTransform::rotation(a+p*(b-a)).translated(cx,cy));}};
class VolumeLF final : public juce::LookAndFeel_V4
{
public:
 void drawLinearSlider(juce::Graphics& g,int x,int y,int width,int height,
                       float sliderPos,float,float,juce::Slider::SliderStyle,juce::Slider&) override
 {
     const float cy=(float)y+(float)height*0.5f;
     const float left=(float)x+9.0f;
     const float right=(float)(x+width)-9.0f;
     if(right<=left)return;
     const float thumb=juce::jlimit(left,right,sliderPos);
     g.setColour(juce::Colour(0xff334858));
     g.fillRoundedRectangle(left,cy-4.0f,right-left,8.0f,4.0f);
     g.setColour(juce::Colour(0xff4cc5d1));
     g.fillRoundedRectangle(left,cy-4.0f,juce::jmax(0.0f,thumb-left),8.0f,4.0f);
     g.setColour(juce::Colour(0xff4a8795));
     g.fillEllipse(thumb-10.0f,cy-10.0f,20.0f,20.0f);
     g.setColour(juce::Colour(0xffe0eaf0));
     g.fillEllipse(thumb-7.0f,cy-7.0f,14.0f,14.0f);
 }
};
struct Controls{std::unique_ptr<juce::Slider>vol,pan;std::unique_ptr<juce::TextButton>mute,solo;std::unique_ptr<juce::Component>mask;std::unique_ptr<juce::ComboBox>midiInput;};
class TrackHeaderMixControls final:public juce::Component,private juce::Timer{
public:
 explicit TrackHeaderMixControls(MainComponent&o):owner(o){setInterceptsMouseClicks(false,true);owner.addAndMakeVisible(this);owner.addMouseListener(this,true);setAlwaysOnTop(true);startTimerHz(12);}
 ~TrackHeaderMixControls()override{shutdown();clear();}
 void shutdown(){if(stopped.exchange(true))return;stopTimer();owner.removeMouseListener(this);setVisible(false);}
 void mouseDown(const juce::MouseEvent& e) override
 {
     if (stopped || !e.mods.isLeftButtonDown() || e.mods.isPopupMenu()) return;
     if (isLibertyMixConsoleVisible(&owner) || isLibertyPerformVisible(&owner)) return;
     const auto p=e.getEventRelativeTo(&owner).getPosition();
     if (p.x<0 || p.x>=owner.trackHeaderWidth || p.y<owner.getArrangeTop() || p.y>=owner.getMixerTop()) return;
     const int logical=owner.getArrangeTrackAtY(p.y);
     const int firstInstrument=owner.getAudioTrackCount()+owner.getMidiTrackCount();
     if (logical<firstInstrument || logical>=firstInstrument+owner.getInstrumentTrackCount()) return;
     // The right-hand blank area must select the instrument even when an
     // unrelated overlay swallows the ordinary MainComponent mouseDown.
     // Avoid every active control (REC/MON, M/S, PAN, volume).
     const int y=owner.getArrangeTrackTop(logical);
     const int dy=p.y-y;
     if (dy<0 || dy>=owner.getArrangeTrackHeight(logical)) return;
     if (dy>=5 && dy<34 && p.x>=90 && p.x<215) return;
     if (dy>=55 && dy<92 && p.x>=15 && p.x<185) return;
     if (dy>=100 && dy<130 && p.x>=65 && p.x<235) return;
     if (owner.selectedTrack!=logical)
     {
         owner.dockMixerMode=true;
         owner.dockStepSequencerMode=false;
         owner.drumSamplerDockSelected=false;
     }
     owner.selectedTrack=logical;
     owner.repaint();
 }
 void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
 {
  if(stopped || isLibertyMixConsoleVisible(&owner) || isLibertyPerformVisible(&owner))return;
  const auto p=e.getEventRelativeTo(&owner).getPosition();
  if(p.x<0 || p.x>=owner.trackHeaderWidth || p.y<owner.getArrangeTop() || p.y>=owner.getMixerTop())return;
  if(wheel.deltaY==0.0f || e.mods.isCtrlDown() || e.mods.isCommandDown())return;
  const int current=owner.getTrackScrollRows();
  const int step=wheel.deltaY<0.0f ? 1 : -1;
  const int next=juce::jlimit(0,juce::jmax(0,owner.getTotalArrangeTrackCount()-1),current+step);
  if(next!=current){owner.setTrackScrollRows(next);refreshTrackLayout();}
 }
 void paint(juce::Graphics&g)override
 {
     g.reduceClipRegion(getLocalBounds());
     for (int i=0; i<(int)controls.size(); ++i)
     {
         const int logical=logicalTrack(i);
         const int y=owner.getArrangeTrackTop(logical);
         const int rh=owner.getArrangeTrackHeight(logical);
         if (rh<94 || y+rh<=owner.getArrangeTop() || y>=owner.getMixerTop()) continue;
         // Three anchored rows: recording, pan/mute/solo, volume.
         g.setColour(juce::Colour(0xff526575).withAlpha(0.50f));
         g.drawHorizontalLine(y+36, 12.0f, (float)owner.trackHeaderWidth-12.0f);
         if (rh>=132) g.drawHorizontalLine(y+94, 12.0f, (float)owner.trackHeaderWidth-12.0f);
         g.setFont(juce::Font(11.0f,juce::Font::bold));
         g.setColour(juce::Colour(0xffffad53));
         if(isAudio(i))g.drawText("PAN",20,y+47,42,12,juce::Justification::centredLeft);
         if (rh>=132)
         {
             g.setColour(juce::Colour(0xffe1e9f0));
             g.drawText("VOL",25,y+105,40,18,juce::Justification::centredLeft);
         }
     }
 }

private:
 bool isAudio(int i)const{return i<owner.getAudioTrackCount();}
 int logicalTrack(int i)const{return isAudio(i)?i:owner.getAudioTrackCount()+owner.getMidiTrackCount()+(i-owner.getAudioTrackCount());}
 void ensure(){int wanted=owner.getAudioTrackCount()+owner.getInstrumentTrackCount();while((int)controls.size()>wanted)controls.pop_back();while((int)controls.size()<wanted){int i=(int)controls.size();Controls c;c.vol=std::make_unique<juce::Slider>();c.pan=std::make_unique<juce::Slider>();c.mute=std::make_unique<juce::TextButton>("M");c.solo=std::make_unique<juce::TextButton>("S");c.mask=std::make_unique<juce::Component>();
if(!isAudio(i))
{
 c.midiInput=std::make_unique<juce::ComboBox>();
 auto* box=c.midiInput.get();
 box->setTooltip(juce::String::fromUTF8("Entrée MIDI de la piste instrument (Toutes = tous les périphériques MIDI)"));
 box->onChange=[this,i,box]
 {
  const int t=i-owner.getAudioTrackCount();
  const int id=box->getSelectedId();
  if(id==1){owner.audioEngine.setInstrumentMidiInput(t,{});return;}
  const auto devices=juce::MidiInput::getAvailableDevices();
  const int index=id-2;
  if(index>=0 && index<devices.size())owner.audioEngine.setInstrumentMidiInput(t,devices[index].identifier);
 };
 addAndMakeVisible(*box);
}
auto* v=c.vol.get();v->setSliderStyle(juce::Slider::LinearHorizontal);v->setScrollWheelEnabled(false);v->setLookAndFeel(&volumeLf);v->setVelocityBasedMode(false);v->setRange(0,2,.001);v->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);v->setTooltip("Volume de la piste (double-clic = 0 dB)");v->setDoubleClickReturnValue(true,1.0);v->textFromValueFunction=[](double value){return value <= 0.00001 ? juce::String("-inf dB") : juce::String(20.0*std::log10(value),1)+" dB";};v->valueFromTextFunction=[](const juce::String& text){return juce::jlimit(0.0,2.0,std::pow(10.0,text.getDoubleValue()/20.0));};v->onValueChange=[this,i,v]{if(syncing)return;if(isAudio(i))owner.audioEngine.setTrackGain(i,(float)v->getValue());else owner.audioEngine.setInstrumentTrackGain(i-owner.getAudioTrackCount(),(float)v->getValue());};auto*p=c.pan.get();p->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);p->setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,juce::MathConstants<float>::pi * 2.75f,true);p->setDoubleClickReturnValue(true,0.0);p->setTooltip("PAN : gauche / centre / droite (double-clic = centre)");p->setRange(-1,1,.001);p->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);p->setLookAndFeel(&lf);p->onValueChange=[this,i,p]{if(syncing)return;if(isAudio(i))owner.audioEngine.setTrackPan(i,(float)p->getValue());else owner.audioEngine.setInstrumentTrackPan(i-owner.getAudioTrackCount(),(float)p->getValue());};c.mute->onClick=[this,i]{if(isAudio(i))owner.audioEngine.setTrackMuted(i,!owner.audioEngine.isTrackMuted(i));else{int t=i-owner.getAudioTrackCount();owner.audioEngine.setInstrumentTrackMuted(t,!owner.audioEngine.isInstrumentTrackMuted(t));}};c.solo->onClick=[this,i]{if(isAudio(i))owner.audioEngine.setTrackSolo(i,!owner.audioEngine.isTrackSolo(i));else{int t=i-owner.getAudioTrackCount();owner.audioEngine.setInstrumentTrackSolo(t,!owner.audioEngine.isInstrumentTrackSolo(t));}};for(auto*b:{c.mute.get(),c.solo.get()}){b->setMouseClickGrabsKeyboardFocus(false);b->setColour(juce::TextButton::textColourOffId,juce::Colours::white);addAndMakeVisible(*b);}addAndMakeVisible(*v);addAndMakeVisible(*p);addAndMakeVisible(*c.mask);controls.push_back(std::move(c));}}
 void layout()
 {
     constexpr int rightMargin=18;
     for (int i=0;i<(int)controls.size();++i)
     {
         auto&c=controls[(size_t)i];
         const int logical=logicalTrack(i);
         const int y=owner.getArrangeTrackTop(logical);
         const int rh=owner.getArrangeTrackHeight(logical);
         const bool middleVisible=logical>=owner.getTrackScrollRows() && y>=owner.getArrangeTop() && y+94<=owner.getMixerTop() && rh>=94;
         const bool volumeVisible=middleVisible && y+132<=owner.getMixerTop() && rh>=132;
         c.vol->setVisible(volumeVisible);c.pan->setVisible(middleVisible);
         c.mute->setVisible(middleVisible);c.solo->setVisible(middleVisible);
         c.mask->setVisible(false);
         if(c.midiInput)
         {
             // Match AudioRecordingController::resized(): the ComboBox is
             // an owner child, positioned in owner coordinates every refresh.
             const int selectorY=y+37;
             c.midiInput->setBounds(211,selectorY,106,19);
             // Both position and parent must match the header overlay.
             // A selector can only appear when its actual hit-tested row is
             // the instrument row it belongs to (never an audio row).
             // Determine the visible instrument range from the same rows
             // that MainComponent paints.  Audio-only viewports must never
             // contain a MIDI selector, even when a stale child has bounds.
             const int firstInstrument=owner.getAudioTrackCount()+owner.getMidiTrackCount();
             const int firstVisible=owner.getTrackScrollRows();
             const int lastVisible=owner.getArrangeTrackAtY(owner.getMixerTop()-1);
             const bool instrumentInViewport=lastVisible>=firstInstrument
                 && firstVisible<owner.getTotalArrangeTrackCount();
             const bool visible=instrumentInViewport
                 && logical>=firstInstrument
                 && logical>=firstVisible && logical<=lastVisible
                 && rh>=56 && y>=owner.getArrangeTop()
                 && selectorY+19<=owner.getMixerTop()
                 && owner.getArrangeTrackAtY(selectorY)==logical
                 && owner.getArrangeTrackAtY(selectorY+18)==logical;
             c.midiInput->setVisible(visible);
         }
         if(!middleVisible)continue;
         c.pan->setBounds(22,y+62,27,27);
         c.mute->setBounds(83,y+57,44,28);
         c.solo->setBounds(136,y+57,44,28);
         const int sliderX=75;
         const int sliderWidth=juce::jmax(0,juce::jmin(145,owner.trackHeaderWidth-rightMargin-sliderX));
         c.vol->setBounds(sliderX,y+102,sliderWidth,24);
         // Enforce the header boundary even if a future width changes.
         jassert(c.vol->getRight()<=owner.trackHeaderWidth);
     }
 }
 public: void refreshTrackLayout(){layout();repaint();}
 private: void sync(){syncing=true;
const auto devices=juce::MidiInput::getAvailableDevices();
for(int i=0;i<(int)controls.size();++i)
{
 auto* box=controls[(size_t)i].midiInput.get();
 if(!box)continue;
 const auto chosen=owner.audioEngine.getInstrumentMidiInput(i-owner.getAudioTrackCount());
 if(box->getNumItems()!=devices.size()+1)
 {
  box->clear(juce::dontSendNotification);
  box->addItem(juce::String::fromUTF8("Entrées"),1);
  for(int j=0;j<devices.size();++j)box->addItem(devices[j].name,j+2);
 }
 int selected=1;
 for(int j=0;j<devices.size();++j)if(devices[j].identifier==chosen){selected=j+2;break;}
 box->setSelectedId(selected,juce::dontSendNotification);
}
for(int i=0;i<(int)controls.size();++i){auto&c=controls[(size_t)i];int t=i-owner.getAudioTrackCount();float gain=isAudio(i)?owner.audioEngine.getTrackGain(i):owner.audioEngine.getInstrumentTrackGain(t);float pan=isAudio(i)?owner.audioEngine.getTrackPan(i):owner.audioEngine.getInstrumentTrackPan(t);c.vol->setValue(gain,juce::dontSendNotification);c.pan->setValue(pan,juce::dontSendNotification);bool m=isAudio(i)?owner.audioEngine.isTrackMuted(i):owner.audioEngine.isInstrumentTrackMuted(t),s=isAudio(i)?owner.audioEngine.isTrackSolo(i):owner.audioEngine.isInstrumentTrackSolo(t);c.mute->setColour(juce::TextButton::buttonColourId,m?juce::Colour(0xff9b4545):juce::Colour(0xff31363e));c.solo->setColour(juce::TextButton::buttonColourId,s?juce::Colour(0xff8b7a32):juce::Colour(0xff31363e));}syncing=false;}
 void clear(){for(auto&c:controls){if(c.pan)c.pan->setLookAndFeel(nullptr);if(c.vol)c.vol->setLookAndFeel(nullptr);}controls.clear();}
 void timerCallback()override{if(stopped)return;if(isLibertyMixConsoleVisible(&owner)||isLibertyPerformVisible(&owner)){setVisible(false);return;}setVisible(true);const auto headerBounds = juce::Rectangle<int>(0, 0, owner.trackHeaderWidth, juce::jmax(0, owner.getMixerTop()));if(getBounds()!=headerBounds)setBounds(headerBounds);ensure();layout();sync();toFront(false);repaint();}
 MainComponent&owner;PanLF lf;VolumeLF volumeLf;std::vector<Controls>controls;std::atomic<bool>stopped{false};bool syncing=false;
};
std::map<MainComponent*,std::unique_ptr<TrackHeaderMixControls>>controllers;
void ensureTrackControls(MainComponent* owner){if(owner!=nullptr&&!controllers.count(owner))controllers.emplace(owner,std::make_unique<TrackHeaderMixControls>(*owner));}
void refreshTrackControls(MainComponent* owner){ensureTrackControls(owner);auto it=controllers.find(owner);if(it!=controllers.end()&&it->second)it->second->refreshTrackLayout();}
class Bootstrap final:private juce::Timer{public:Bootstrap(){startTimerHz(10);}~Bootstrap()override{shutdown();}void shutdown(){stopTimer();for(auto&p:controllers)if(p.second)p.second->shutdown();controllers.clear();}private:void timerCallback()override{auto&d=juce::Desktop::getInstance();for(int i=0;i<d.getNumComponents();++i)if(auto*w=dynamic_cast<juce::DocumentWindow*>(d.getComponent(i)))if(auto*m=dynamic_cast<MainComponent*>(w->getContentComponent()))if(!controllers.count(m)){controllers.emplace(m,std::make_unique<TrackHeaderMixControls>(*m));stopTimer();return;}}};Bootstrap bootstrap;
}
void shutdownLibertyTrackHeaderMixControls(){bootstrap.shutdown();}
void toggleLibertyMidiInstrumentMute(MainComponent&owner){int logical=owner.selectedTrack-owner.getAudioTrackCount()-owner.getMidiTrackCount();if(logical>=0&&logical<owner.getInstrumentTrackCount())owner.audioEngine.setInstrumentTrackMuted(logical,!owner.audioEngine.isInstrumentTrackMuted(logical));owner.repaint();}

void refreshLibertyTrackHeaderMixLayout(MainComponent* owner)
{
    if (owner != nullptr) refreshTrackControls(owner);
}

void ensureLibertyTrackHeaderMixControls(MainComponent* owner)
{
    ensureTrackControls(owner);
}
