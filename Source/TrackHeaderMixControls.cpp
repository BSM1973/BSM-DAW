#define private public
#include "MainComponent.h"
#undef private
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

int getLibertyTrackRowHeight() noexcept;
bool isLibertyMixConsoleVisible(MainComponent* owner);

namespace {
constexpr int rulerH=32;
class PanLF final:public juce::LookAndFeel_V4{public:void drawRotarySlider(juce::Graphics&g,int x,int y,int w,int h,float p,float a,float b,juce::Slider&)override{auto s=(float)juce::jmin(w,h)-2,cx=x+w*.5f,cy=y+h*.5f,r=s*.5f;g.setColour(juce::Colour(0xffff9a24));g.fillEllipse(cx-r,cy-r,s,s);g.setColour(juce::Colour(0xff5b2b00));g.fillEllipse(cx-r+3,cy-r+3,s-6,s-6);juce::Path q;q.addRoundedRectangle(-1.1f,-r*.72f,2.2f,r*.72f,1);g.setColour(juce::Colours::white);g.fillPath(q,juce::AffineTransform::rotation(a+p*(b-a)).translated(cx,cy));}};
struct Controls{std::unique_ptr<juce::Slider>vol,pan;std::unique_ptr<juce::TextButton>mute,solo;std::unique_ptr<juce::Component>mask;};
class TrackHeaderMixControls final:public juce::Component,private juce::Timer{
public:
 explicit TrackHeaderMixControls(MainComponent&o):owner(o){setInterceptsMouseClicks(false,true);owner.addAndMakeVisible(this);startTimerHz(12);}
 ~TrackHeaderMixControls()override{shutdown();clear();}
 void shutdown(){if(stopped.exchange(true))return;stopTimer();setVisible(false);}
 void paint(juce::Graphics&g)override{g.setFont(juce::Font(7.5f,juce::Font::bold));int rh=getLibertyTrackRowHeight();for(int i=0;i<(int)controls.size();++i){int logical=logicalTrack(i),y=76+rulerH+(logical-owner.getTrackScrollRows())*rh;if(y+rh<108||y>=owner.getHeight()-210)continue;int my=y+rh-30;g.setColour(juce::Colour(0xff1e232a));g.fillRect(6,my,184,30);g.setColour(juce::Colour(0xffc5cbd3));g.drawText("VOL",8,my+9,20,11,juce::Justification::centredLeft);g.setColour(juce::Colour(0xffffb04d));g.drawText("PAN",99,my+9,24,11,juce::Justification::centredLeft);}}
private:
 bool isAudio(int i)const{return i<owner.getAudioTrackCount();}
 int logicalTrack(int i)const{return isAudio(i)?i:owner.getAudioTrackCount()+owner.getMidiTrackCount()+(i-owner.getAudioTrackCount());}
 void ensure(){int wanted=owner.getAudioTrackCount()+owner.getInstrumentTrackCount();while((int)controls.size()>wanted)controls.pop_back();while((int)controls.size()<wanted){int i=(int)controls.size();Controls c;c.vol=std::make_unique<juce::Slider>();c.pan=std::make_unique<juce::Slider>();c.mute=std::make_unique<juce::TextButton>("M");c.solo=std::make_unique<juce::TextButton>("S");c.mask=std::make_unique<juce::Component>();auto* v=c.vol.get();v->setSliderStyle(juce::Slider::LinearHorizontal);v->setRange(0,2,.001);v->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);v->onValueChange=[this,i,v]{if(syncing)return;if(isAudio(i))owner.audioEngine.setTrackGain(i,(float)v->getValue());else owner.audioEngine.setInstrumentTrackGain(i-owner.getAudioTrackCount(),(float)v->getValue());};auto*p=c.pan.get();p->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);p->setRange(-1,1,.001);p->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);p->setLookAndFeel(&lf);p->onValueChange=[this,i,p]{if(syncing)return;if(isAudio(i))owner.audioEngine.setTrackPan(i,(float)p->getValue());else owner.audioEngine.setInstrumentTrackPan(i-owner.getAudioTrackCount(),(float)p->getValue());};c.mute->onClick=[this,i]{if(isAudio(i))owner.audioEngine.setTrackMuted(i,!owner.audioEngine.isTrackMuted(i));else{int t=i-owner.getAudioTrackCount();owner.audioEngine.setInstrumentTrackMuted(t,!owner.audioEngine.isInstrumentTrackMuted(t));}};c.solo->onClick=[this,i]{if(isAudio(i))owner.audioEngine.setTrackSolo(i,!owner.audioEngine.isTrackSolo(i));else{int t=i-owner.getAudioTrackCount();owner.audioEngine.setInstrumentTrackSolo(t,!owner.audioEngine.isInstrumentTrackSolo(t));}};for(auto*b:{c.mute.get(),c.solo.get()}){b->setMouseClickGrabsKeyboardFocus(false);b->setColour(juce::TextButton::textColourOffId,juce::Colours::white);addAndMakeVisible(*b);}addAndMakeVisible(*v);addAndMakeVisible(*p);addAndMakeVisible(*c.mask);controls.push_back(std::move(c));}}
 void layout(){int rh=getLibertyTrackRowHeight();for(int i=0;i<(int)controls.size();++i){auto&c=controls[(size_t)i];int y=76+rulerH+(logicalTrack(i)-owner.getTrackScrollRows())*rh,my=y+rh-30,ms=y+39;bool vis=y+rh>=108&&y<owner.getHeight()-210;c.vol->setVisible(vis);c.pan->setVisible(vis);c.mute->setVisible(vis);c.solo->setVisible(vis);c.mask->setVisible(vis);if(!vis)continue;c.mute->setBounds(10,ms,30,20);c.solo->setBounds(44,ms,30,20);c.vol->setBounds(28,my+6,66,18);c.pan->setBounds(126,my+4,22,22);c.mask->setBounds(154,my,36,30);}}
 void sync(){syncing=true;for(int i=0;i<(int)controls.size();++i){auto&c=controls[(size_t)i];int t=i-owner.getAudioTrackCount();float gain=isAudio(i)?owner.audioEngine.getTrackGain(i):owner.audioEngine.getInstrumentTrackGain(t);float pan=isAudio(i)?owner.audioEngine.getTrackPan(i):owner.audioEngine.getInstrumentTrackPan(t);c.vol->setValue(gain,juce::dontSendNotification);c.pan->setValue(pan,juce::dontSendNotification);bool m=isAudio(i)?owner.audioEngine.isTrackMuted(i):owner.audioEngine.isInstrumentTrackMuted(t),s=isAudio(i)?owner.audioEngine.isTrackSolo(i):owner.audioEngine.isInstrumentTrackSolo(t);c.mute->setColour(juce::TextButton::buttonColourId,m?juce::Colour(0xff9b4545):juce::Colour(0xff31363e));c.solo->setColour(juce::TextButton::buttonColourId,s?juce::Colour(0xff8b7a32):juce::Colour(0xff31363e));}syncing=false;}
 void clear(){for(auto&c:controls)if(c.pan)c.pan->setLookAndFeel(nullptr);controls.clear();}
 void timerCallback()override{if(stopped)return;if(isLibertyMixConsoleVisible(&owner)){setVisible(false);return;}setVisible(true);if(getBounds()!=owner.getLocalBounds())setBounds(owner.getLocalBounds());ensure();layout();sync();toFront(false);repaint();}
 MainComponent&owner;PanLF lf;std::vector<Controls>controls;std::atomic<bool>stopped{false};bool syncing=false;
};
std::map<MainComponent*,std::unique_ptr<TrackHeaderMixControls>>controllers;
class Bootstrap final:private juce::Timer{public:Bootstrap(){startTimerHz(10);}~Bootstrap()override{shutdown();}void shutdown(){stopTimer();for(auto&p:controllers)if(p.second)p.second->shutdown();controllers.clear();}private:void timerCallback()override{auto&d=juce::Desktop::getInstance();for(int i=0;i<d.getNumComponents();++i)if(auto*w=dynamic_cast<juce::DocumentWindow*>(d.getComponent(i)))if(auto*m=dynamic_cast<MainComponent*>(w->getContentComponent()))if(!controllers.count(m))controllers.emplace(m,std::make_unique<TrackHeaderMixControls>(*m));}};Bootstrap bootstrap;
}
void shutdownLibertyTrackHeaderMixControls(){bootstrap.shutdown();}
void toggleLibertyMidiInstrumentMute(MainComponent&owner){int logical=owner.selectedTrack-owner.getAudioTrackCount()-owner.getMidiTrackCount();if(logical>=0&&logical<owner.getInstrumentTrackCount())owner.audioEngine.setInstrumentTrackMuted(logical,!owner.audioEngine.isInstrumentTrackMuted(logical));owner.repaint();}
