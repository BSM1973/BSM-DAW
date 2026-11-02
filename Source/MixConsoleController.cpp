#define private public
#include "MainComponent.h"
#undef private
#include "PluginHost.h"
#include "OneKnobEffects.h"
#include "MixConsoleLayout.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <cmath>
#include <map>
#include <array>
#include <memory>
#include <vector>

int getLibertyTrackColourId(int track);
juce::String getLibertyTrackName(int track);

namespace {
constexpr int transportHeight=76, stripW=150, topY=66;
float gainToDb(float g){return g<=.00001f?-60.f:juce::jlimit(-60.f,6.f,juce::Decibels::gainToDecibels(g));}
float dbToGain(float d){return d<=-59.9f?0.f:juce::Decibels::decibelsToGain(d);}
juce::Colour trackColour(int id){static constexpr juce::uint32 palette[]{0xff31506a,0xff3b82f6,0xff22c55e,0xffeab308,0xfff97316,0xffef4444,0xffa855f7,0xffec4899,0xff14b8a6};return juce::Colour(palette[(size_t)juce::jlimit(0,8,id)]);}
class ConsoleLookAndFeel final:public juce::LookAndFeel_V4{public:void drawLinearSlider(juce::Graphics&g,int x,int y,int w,int h,float pos,float,float,const juce::Slider::SliderStyle style,juce::Slider&s)override{if(style!=juce::Slider::LinearVertical){juce::LookAndFeel_V4::drawLinearSlider(g,x,y,w,h,pos,0.f,0.f,style,s);return;}const float cx=(float)x+(float)w*.5f;const auto rail=juce::Rectangle<float>(cx-3.f,(float)y+4.f,6.f,(float)h-8.f);g.setColour(juce::Colour(0xff090b0e));g.fillRoundedRectangle(rail,3.f);g.setColour(juce::Colour(0xff344452));g.drawRoundedRectangle(rail,3.f,1.f);const auto knob=juce::Rectangle<float>((float)x+2.f,pos-7.f,(float)w-4.f,14.f);g.setColour(juce::Colour(0xffe8eff7));g.fillRoundedRectangle(knob,3.f);g.setColour(juce::Colour(0xff879aa9));g.drawRoundedRectangle(knob,3.f,1.f);g.setColour(juce::Colour(0xff24292f));g.drawHorizontalLine((int)pos,knob.getX()+4.f,knob.getRight()-4.f);}void drawRotarySlider(juce::Graphics&g,int x,int y,int w,int h,
    float position,float start,float end,juce::Slider& slider)override{
    const float d=(float)juce::jmin(w,h);
    if(d<10.f)return;
    const float cx=(float)x+(float)w*.5f;
    const float cy=(float)y+(float)h*.5f;
    const float radius=(d-2.f)*.5f;
    const float angle=start+juce::jlimit(0.f,1.f,position)*(end-start);
    const auto accent=slider.findColour(juce::Slider::rotarySliderFillColourId);
    // Engraved dial markings stay inside the slider's own bounding rectangle.
    g.setColour(juce::Colour(0xff12202c));
    g.fillEllipse(cx-radius,cy-radius,2.f*radius,2.f*radius);
    g.setColour(juce::Colour(0xff607688));
    g.drawEllipse(cx-radius,cy-radius,2.f*radius,2.f*radius,.7f);
    for(int tick=0;tick<=8;++tick){
        const float a=start+(end-start)*(float)tick/8.f;
        const float inside=radius*.84f,outside=radius*.97f;
        g.setColour(juce::Colour(0xffa0b2bf).withAlpha(tick==4?.9f:.58f));
        g.drawLine(cx+std::sin(a)*inside,cy-std::cos(a)*inside,
                   cx+std::sin(a)*outside,cy-std::cos(a)*outside,.85f);
    }
    juce::Path track;
    track.addCentredArc(cx,cy,radius*.81f,radius*.81f,0.f,start,end,true);
    g.setColour(juce::Colour(0xff304653));
    g.strokePath(track,juce::PathStrokeType(1.6f));
    juce::Path filled;
    filled.addCentredArc(cx,cy,radius*.81f,radius*.81f,0.f,start,angle,true);
    g.setColour(accent.withAlpha(.93f));
    g.strokePath(filled,juce::PathStrokeType(1.9f));
    const float bodyRadius=radius*.70f;
    juce::ColourGradient metal(juce::Colour(0xff7a8b98),
                              cx-bodyRadius*.60f,cy-bodyRadius*.75f,
                              juce::Colour(0xff1a2833),
                              cx+bodyRadius*.75f,cy+bodyRadius*.85f,false);
    g.setGradientFill(metal);
    g.fillEllipse(cx-bodyRadius,cy-bodyRadius,2.f*bodyRadius,2.f*bodyRadius);
    g.setColour(juce::Colour(0xffa3b1bb).withAlpha(.70f));
    g.drawEllipse(cx-bodyRadius,cy-bodyRadius,2.f*bodyRadius,2.f*bodyRadius,.85f);
    g.setColour(juce::Colour(0xff18232d));
    g.fillEllipse(cx-bodyRadius*.58f,cy-bodyRadius*.58f,
                  1.16f*bodyRadius,1.16f*bodyRadius);
    const float inner=bodyRadius*.15f,outer=bodyRadius*.83f;
    g.setColour(accent.brighter(.28f));
    g.drawLine(cx+std::sin(angle)*inner,cy-std::cos(angle)*inner,
               cx+std::sin(angle)*outer,cy-std::cos(angle)*outer,1.7f);
}};
struct Strip{enum Kind{audio,midi,instrument,master}kind=audio;int lane=0;float level=0.f,peak=0.f;std::unique_ptr<juce::Slider>fader,pan;std::unique_ptr<juce::TextButton>mute,solo,plugin,unload,channel;
 std::unique_ptr<juce::Slider>inputGain,highPass,compThreshold,compRatio,gateThreshold;
 std::array<std::unique_ptr<juce::Slider>,3>eqKnobs;
 std::array<std::unique_ptr<juce::TextButton>,3>modulePower;
 std::array<bool,4> moduleExpanded{};std::array<std::unique_ptr<juce::TextButton>,4>moduleTabs;
 std::unique_ptr<juce::TextButton> fxHeader;bool insertsExpanded=true;
 int firstFxSlot=0;int preferredFaderHeight=250;
 bool gateOn=false,eqOn=false;
 bool compOn=false;std::array<std::unique_ptr<juce::TextButton>,8>fx,fxUnload;juce::Rectangle<int>bounds;};
class MixConsoleView final:public juce::Component,private juce::Timer{
public:
 explicit MixConsoleView(MainComponent&o):owner(o){scrollListener.o=this;verticalListener.o=this;verticalListener.vertical=true;verticalScroll.addListener(&verticalListener);addAndMakeVisible(verticalScroll);setOpaque(true);setInterceptsMouseClicks(true,true);scroll.setRangeLimits(0,1);scroll.addListener(&scrollListener);addAndMakeVisible(scroll);owner.addAndMakeVisible(this);startTimerHz(24);setVisible(false);}
 ~MixConsoleView()override{stopTimer();scroll.removeListener(&scrollListener);verticalScroll.removeListener(&verticalListener);clear();}
 void setConsoleVisible(bool b){visible=b;setVisible(b);if(b){setBounds(0,transportHeight,owner.getWidth(),juce::jmax(1,owner.getHeight()-transportHeight));rebuild();toFront(true);layout();sync();updateMeters();repaint();}else{selected=nullptr;}}
 bool isConsoleVisible()const{return visible;} void refreshStatic(){if(!visible)return;rebuild();sync();repaint();}
 int getDropTrackForOwnerPoint(juce::Point<int> p,bool instrumentPlugin)const{if(!visible||p.y<transportHeight)return-1;auto q=p-juce::Point<int>(0,transportHeight);for(auto&s:strips)if(s->bounds.contains(q)){if(instrumentPlugin&&s->kind==Strip::instrument)return owner.getAudioTrackCount()+owner.getMidiTrackCount()+s->lane;if(!instrumentPlugin&&(s->kind==Strip::audio||s->kind==Strip::instrument)){for(int i=0;i<8;++i)if(s->fx[(size_t)i]&&s->fx[(size_t)i]->isVisible()&&s->fx[(size_t)i]->getBounds().contains(q))return 1000000+(s->kind==Strip::audio?s->lane:owner.getAudioTrackCount()+owner.getMidiTrackCount()+s->lane)*8+i;return-1;}}return-1;}
 void paint(juce::Graphics&g)override{g.fillAll(juce::Colour(0xff0b0f14));g.setColour(juce::Colour(0xff18212b));g.fillRect(0,0,getWidth(),54);g.setColour(juce::Colour(0xff344452));g.drawHorizontalLine(53,0.0f,(float)getWidth());g.setColour(juce::Colour(0xffb5c3d1));g.setFont(juce::Font(10.f));g.drawFittedText(juce::String::fromUTF8("SECTIONS LIBRES  |  MAJ + CLIC : MÊME SECTION SUR TOUTES LES PISTES  |  DOUBLE-CLIC SÉPARATEUR : HAUTEUR NORMALE"),
  18,15,juce::jmax(1,getWidth()-36),20,juce::Justification::centredLeft,1);{juce::Graphics::ScopedSaveState clip(g);g.reduceClipRegion(0,topY,getWidth(),juce::jmax(1,getHeight()-topY-38));for(auto&s:strips)draw(g,*s);}}
 void resized()override{layout();}
 void mouseWheelMove(const juce::MouseEvent& e,const juce::MouseWheelDetails& wheel)override{
   for(auto& strip:strips){
     if(!strip->bounds.contains(e.getPosition())||!strip->fxHeader||!strip->insertsExpanded)continue;
     const auto g=getStripGeometry(*strip);
     const auto y=e.getPosition().y-strip->bounds.getY();
     if(g.visibleFx>=8||y<g.fxY||y>=g.fxY+g.visibleFx*g.fxStep)continue;
     strip->firstFxSlot=juce::jlimit(0,8-g.visibleFx,strip->firstFxSlot+(wheel.deltaY<0?1:-1));
     layout();repaint();return;
   }
 }
 void mouseDown(const juce::MouseEvent&e)override{
   for(auto& strip:strips){
     if(!strip->bounds.contains(e.getPosition()))continue;
     if(strip->kind!=Strip::master&&strip->kind!=Strip::midi){
       const auto g=getStripGeometry(*strip);
       const int dy=e.getPosition().y-strip->bounds.getY();
       if(std::abs(dy-g.splitterY)<=6){
         if(e.getNumberOfClicks()>=2){
            strip->preferredFaderHeight=250;layout();repaint();return;
         }
         draggingHeight=strip.get();dragStartY=e.getPosition().y;
         dragStartPreference=strip->preferredFaderHeight;
         setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
         return;
       }
     }
     if(strip->kind==Strip::master){selected=nullptr;repaint();return;}
     selected=strip.get();
     owner.selectedTrack=strip->kind==Strip::audio?strip->lane:
       (strip->kind==Strip::midi?owner.getAudioTrackCount()+strip->lane:
       owner.getAudioTrackCount()+owner.getMidiTrackCount()+strip->lane);
     repaint();owner.repaint();return;
   }
 }
 void mouseDrag(const juce::MouseEvent&e)override{
   if(!draggingHeight)return;
   draggingHeight->preferredFaderHeight=juce::jlimit(100,360,
       dragStartPreference+dragStartY-e.getPosition().y);
   layout();repaint();
 }
 void mouseUp(const juce::MouseEvent&)override{
   draggingHeight=nullptr;setMouseCursor(juce::MouseCursor::NormalCursor);
 }
 void mouseMove(const juce::MouseEvent&e)override{
   bool onHandle=false;
   for(const auto& strip:strips){
     if(!strip->bounds.contains(e.getPosition()))continue;
     if(strip->kind==Strip::audio||strip->kind==Strip::instrument)
       onHandle=std::abs(e.getPosition().y-strip->bounds.getY()-
                         getStripGeometry(*strip).splitterY)<=6;
     break;
   }
   setMouseCursor(onHandle?juce::MouseCursor::UpDownResizeCursor:
                         juce::MouseCursor::NormalCursor);
 }
private:
 struct ScrollListener:juce::ScrollBar::Listener{MixConsoleView*o=nullptr;bool vertical=false;void scrollBarMoved(juce::ScrollBar*,double v)override{if(o){if(vertical)o->verticalOffset=(int)std::round(v);else o->offset=(int)std::round(v);o->layout();o->repaint();}}}scrollListener,verticalListener;
 void clear(){selected=nullptr;draggingHeight=nullptr;for(auto&s:strips){if(s->fader)s->fader->setLookAndFeel(nullptr);if(s->pan)s->pan->setLookAndFeel(nullptr);}strips.clear();}
 std::unique_ptr<Strip> makeStrip(Strip::Kind k,int lane){auto s=std::make_unique<Strip>();s->kind=k;s->lane=lane;s->fader=std::make_unique<juce::Slider>();s->fader->setSliderStyle(juce::Slider::LinearVertical);s->fader->setRange(-60,6,.1);s->fader->setTextBoxStyle(juce::Slider::TextBoxBelow,false,62,20);s->fader->setNumDecimalPlacesToDisplay(1);s->fader->setMouseClickGrabsKeyboardFocus(false);s->fader->setTextValueSuffix(" dB");s->fader->setLookAndFeel(&lookAndFeel);addAndMakeVisible(*s->fader);if(k==Strip::audio||k==Strip::instrument){
 s->fxHeader=std::make_unique<juce::TextButton>("INSERTS");
 s->fxHeader->setMouseClickGrabsKeyboardFocus(false);
 s->fxHeader->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff203b50));
 s->fxHeader->setColour(juce::TextButton::textColourOffId,juce::Colour(0xffe6edf3));
 addAndMakeVisible(*s->fxHeader);
 s->fxHeader->setTooltip("Ouvrir ou fermer les inserts. Maj + clic : toutes les pistes.");
 if(k==Strip::audio){for(int tab=0;tab<4;++tab){auto&button=s->moduleTabs[(size_t)tab];button=std::make_unique<juce::TextButton>(tab==0?"EQ":tab==1?"GATE":tab==2?"COMP":"INPUT");button->setMouseClickGrabsKeyboardFocus(false);button->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff263b4b));button->setColour(juce::TextButton::textColourOffId,juce::Colour(0xffe6edf3));button->setTooltip("Ouvrir ou fermer cette section. Maj + clic : toutes les pistes audio.");
 addAndMakeVisible(*button);}for(int section=0;section<3;++section){auto&button=s->modulePower[(size_t)section];button=std::make_unique<juce::TextButton>("OFF");button->setMouseClickGrabsKeyboardFocus(false);button->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff34404a));button->setColour(juce::TextButton::textColourOffId,juce::Colours::white);addAndMakeVisible(*button);}for(int band=0;band<3;++band){auto&knob=s->eqKnobs[(size_t)band];knob=std::make_unique<juce::Slider>();knob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);knob->setRange(-12.0,12.0,0.5);knob->setValue(0.0,juce::dontSendNotification);knob->setDoubleClickReturnValue(true,0.0);knob->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);knob->setMouseClickGrabsKeyboardFocus(false);knob->setLookAndFeel(&lookAndFeel);
knob->setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xff56cbdc));
knob->setTooltip(juce::String("EQ : ")+(band==0?"graves":band==1?"médiums":"aigus")+" -12 à +12 dB");addAndMakeVisible(*knob);}s->gateThreshold=std::make_unique<juce::Slider>();s->gateThreshold->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);s->gateThreshold->setRange(-80.0,-20.0,1.0);s->gateThreshold->setValue(-55.0,juce::dontSendNotification);s->gateThreshold->setDoubleClickReturnValue(true,-55.0);s->gateThreshold->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);s->gateThreshold->setMouseClickGrabsKeyboardFocus(false);s->gateThreshold->setLookAndFeel(&lookAndFeel);s->gateThreshold->setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xff8bc99e));s->gateThreshold->setTooltip("GATE : seuil en dB, déplacement pour activer");addAndMakeVisible(*s->gateThreshold);s->compThreshold=std::make_unique<juce::Slider>();s->compRatio=std::make_unique<juce::Slider>();for(auto*knob:{s->compThreshold.get(),s->compRatio.get()}){knob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);knob->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);knob->setMouseClickGrabsKeyboardFocus(false);knob->setLookAndFeel(&lookAndFeel);knob->setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xffe2ac65));addAndMakeVisible(*knob);}s->compThreshold->setRange(-48.0,0.0,1.0);s->compThreshold->setValue(-18.0,juce::dontSendNotification);s->compThreshold->setDoubleClickReturnValue(true,-18.0);s->compThreshold->setTooltip("DYNAMIQUE : seuil du compresseur (dB)");s->compRatio->setRange(1.0,12.0,0.5);s->compRatio->setValue(4.0,juce::dontSendNotification);s->compRatio->setDoubleClickReturnValue(true,4.0);s->compRatio->setTooltip("DYNAMIQUE : ratio du compresseur");s->highPass=std::make_unique<juce::Slider>();s->highPass->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);s->highPass->setRange(0.0,500.0,1.0);s->highPass->setValue(0.0,juce::dontSendNotification);s->highPass->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);s->highPass->setDoubleClickReturnValue(true,0.0);s->highPass->setMouseClickGrabsKeyboardFocus(false);s->highPass->setLookAndFeel(&lookAndFeel);s->highPass->setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xff73bbdb));s->highPass->setTooltip("INPUT : coupe-bas de 0 (désactivé) à 500 Hz");addAndMakeVisible(*s->highPass);s->inputGain=std::make_unique<juce::Slider>();s->inputGain->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);s->inputGain->setRange(-24.0,24.0,0.5);s->inputGain->setValue(0.0,juce::dontSendNotification);s->inputGain->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);s->inputGain->setDoubleClickReturnValue(true,0.0);s->inputGain->setMouseClickGrabsKeyboardFocus(false);s->inputGain->setLookAndFeel(&lookAndFeel);s->inputGain->setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xff73bbdb));s->inputGain->setTooltip("INPUT : gain d'entrée de -24 à +24 dB");addAndMakeVisible(*s->inputGain);s->channel=std::make_unique<juce::TextButton>("CANAL");s->channel->setMouseClickGrabsKeyboardFocus(false);s->channel->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff24465a));s->channel->setColour(juce::TextButton::textColourOffId,juce::Colours::white);addAndMakeVisible(*s->channel);}s->pan=std::make_unique<juce::Slider>();s->pan->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);s->pan->setRange(-1,1,.01);s->pan->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);s->pan->setDoubleClickReturnValue(true,0.0);s->pan->setMouseClickGrabsKeyboardFocus(false);s->pan->setLookAndFeel(&lookAndFeel);s->pan->setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xffe1c58a));addAndMakeVisible(*s->pan);s->mute=std::make_unique<juce::TextButton>("M");s->solo=std::make_unique<juce::TextButton>("S");s->mute->setClickingTogglesState(false);s->solo->setClickingTogglesState(false);s->mute->setMouseClickGrabsKeyboardFocus(false);s->solo->setMouseClickGrabsKeyboardFocus(false);s->mute->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff25323e));s->solo->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff25323e));s->mute->setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xffa44141));s->solo->setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xffa18432));s->mute->setColour(juce::TextButton::textColourOffId,juce::Colours::white);s->solo->setColour(juce::TextButton::textColourOffId,juce::Colours::white);s->mute->setColour(juce::TextButton::textColourOnId,juce::Colours::white);s->solo->setColour(juce::TextButton::textColourOnId,juce::Colours::white);addAndMakeVisible(*s->mute);addAndMakeVisible(*s->solo);}s->plugin=std::make_unique<juce::TextButton>();s->plugin->setMouseClickGrabsKeyboardFocus(false);s->plugin->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff18212b));s->plugin->setColour(juce::TextButton::textColourOffId,juce::Colour(0xffd4d8de));s->unload=std::make_unique<juce::TextButton>("X");s->unload->setMouseClickGrabsKeyboardFocus(false);s->unload->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff342126));s->unload->setColour(juce::TextButton::textColourOffId,juce::Colours::white);addAndMakeVisible(*s->plugin);addAndMakeVisible(*s->unload);if(k==Strip::audio||k==Strip::instrument)for(int i=0;i<8;++i){s->fx[(size_t)i]=std::make_unique<juce::TextButton>("FX "+juce::String(i+1)+" · VIDE");s->fxUnload[(size_t)i]=std::make_unique<juce::TextButton>("X");s->fx[(size_t)i]->setMouseClickGrabsKeyboardFocus(false);s->fxUnload[(size_t)i]->setMouseClickGrabsKeyboardFocus(false);s->fx[(size_t)i]->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff18212b));s->fx[(size_t)i]->setColour(juce::TextButton::textColourOffId,juce::Colour(0xffd4d8de));s->fxUnload[(size_t)i]->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff342126));s->fxUnload[(size_t)i]->setColour(juce::TextButton::textColourOffId,juce::Colours::white);addAndMakeVisible(*s->fx[(size_t)i]);addAndMakeVisible(*s->fxUnload[(size_t)i]);}wire(*s);return s;}
 void configureValuePopup(Strip& s){auto configure=[](juce::Slider* slider,const juce::String&suffix){if(!slider)return;slider->setPopupDisplayEnabled(true,true,nullptr,1500);slider->setTextValueSuffix(suffix);slider->setNumDecimalPlacesToDisplay(1);};configure(s.inputGain.get()," dB");configure(s.highPass.get()," Hz");configure(s.compThreshold.get()," dB");configure(s.compRatio.get()," :1");configure(s.gateThreshold.get()," dB");for(auto&knob:s.eqKnobs)configure(knob.get()," dB");}
 void refreshPower(Strip& s){if(s.kind!=Strip::audio)return;const bool states[3]={s.eqOn,s.gateOn,s.compOn};for(int i=0;i<3;++i){auto&button=s.modulePower[(size_t)i];if(!button)continue;button->setButtonText(states[i]?"ON":"OFF");button->setColour(juce::TextButton::buttonColourId,states[i]?juce::Colour(0xff246b63):juce::Colour(0xff34404a));}}
 void wire(Strip&s){auto*sp=&s;for(int tab=0;tab<4;++tab)if(s.moduleTabs[(size_t)tab])
 s.moduleTabs[(size_t)tab]->onClick=[this,sp,tab]{
     const bool expand=!sp->moduleExpanded[(size_t)tab];
     if(juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown()){
         for(auto& other:strips)
             if(other->kind==Strip::audio)other->moduleExpanded[(size_t)tab]=expand;
     }else sp->moduleExpanded[(size_t)tab]=expand;
     layout();repaint();
 };
 if(s.fxHeader)s.fxHeader->onClick=[this,sp]{
     const bool expand=!sp->insertsExpanded;
     if(juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown()){
         for(auto& other:strips)
             if(other->fxHeader){other->insertsExpanded=expand;other->firstFxSlot=0;}
     }else{sp->insertsExpanded=expand;sp->firstFxSlot=0;}
     layout();repaint();
 };if(sp->kind==Strip::midi){s.fader->setEnabled(false);s.plugin->setEnabled(false);s.unload->setEnabled(false);return;}s.fader->onValueChange=[this,sp]{if(syncing)return;float g=dbToGain((float)sp->fader->getValue());if(sp->kind==Strip::audio)owner.audioEngine.setTrackGain(sp->lane,g);else if(sp->kind==Strip::instrument)owner.audioEngine.setInstrumentTrackGain(sp->lane,g);else owner.audioEngine.setMasterGain(g);repaint();};if(s.pan)s.pan->onValueChange=[this,sp]{if(syncing)return;float p=(float)sp->pan->getValue();if(sp->kind==Strip::audio)owner.audioEngine.setTrackPan(sp->lane,p);else owner.audioEngine.setInstrumentTrackPan(sp->lane,p);repaint();};if(s.mute)s.mute->onClick=[this,sp]{if(sp->kind==Strip::audio)owner.audioEngine.setTrackMuted(sp->lane,!owner.audioEngine.isTrackMuted(sp->lane));else owner.audioEngine.setInstrumentTrackMuted(sp->lane,!owner.audioEngine.isInstrumentTrackMuted(sp->lane));sync();repaint();};if(s.solo)s.solo->onClick=[this,sp]{if(sp->kind==Strip::audio)owner.audioEngine.setTrackSolo(sp->lane,!owner.audioEngine.isTrackSolo(sp->lane));else owner.audioEngine.setInstrumentTrackSolo(sp->lane,!owner.audioEngine.isInstrumentTrackSolo(sp->lane));sync();repaint();};for(int section=0;section<3;++section)if(s.modulePower[(size_t)section])s.modulePower[(size_t)section]->onClick=[this,sp,section]{bool on=false;if(section==0){sp->eqOn=!sp->eqOn;on=sp->eqOn;float gains[3];for(int j=0;j<3;++j)gains[j]=owner.audioEngine.getChannelEqBandDb(sp->lane,j);owner.audioEngine.setChannelEq(sp->lane,on,gains[0],gains[1],gains[2]);}else if(section==1){sp->gateOn=!sp->gateOn;on=sp->gateOn;owner.audioEngine.setChannelGate(sp->lane,on,(float)sp->gateThreshold->getValue());}else{sp->compOn=!sp->compOn;on=sp->compOn;owner.audioEngine.setChannelCompressor(sp->lane,on,(float)sp->compThreshold->getValue(),(float)sp->compRatio->getValue());}auto&button=*sp->modulePower[(size_t)section];button.setButtonText(on?"ON":"OFF");button.setColour(juce::TextButton::buttonColourId,on?juce::Colour(0xff246b63):juce::Colour(0xff34404a));repaint();};for(int band=0;band<3;++band)if(s.eqKnobs[(size_t)band])s.eqKnobs[(size_t)band]->onValueChange=[this,sp,band]{if(syncing)return;float gains[3];for(int j=0;j<3;++j)gains[j]=owner.audioEngine.getChannelEqBandDb(sp->lane,j);gains[band]=(float)sp->eqKnobs[(size_t)band]->getValue();sp->eqOn=true;refreshPower(*sp);owner.audioEngine.setChannelEq(sp->lane,true,gains[0],gains[1],gains[2]);repaint();};if(s.gateThreshold)s.gateThreshold->onValueChange=[this,sp]{if(syncing)return;sp->gateOn=true;refreshPower(*sp);owner.audioEngine.setChannelGate(sp->lane,true,(float)sp->gateThreshold->getValue());repaint();};if(s.compThreshold)s.compThreshold->onValueChange=[this,sp]{if(syncing)return;sp->compOn=true;refreshPower(*sp);owner.audioEngine.setChannelCompressor(sp->lane,true,(float)sp->compThreshold->getValue(),(float)sp->compRatio->getValue());repaint();};if(s.compRatio)s.compRatio->onValueChange=[this,sp]{if(syncing)return;sp->compOn=true;owner.audioEngine.setChannelCompressor(sp->lane,true,(float)sp->compThreshold->getValue(),(float)sp->compRatio->getValue());repaint();};if(s.highPass)s.highPass->onValueChange=[this,sp]{if(syncing)return;owner.audioEngine.setChannelHighPassHz(sp->lane,(float)sp->highPass->getValue());repaint();};if(s.inputGain)s.inputGain->onValueChange=[this,sp]{if(syncing)return;owner.audioEngine.setChannelInputGainDb(sp->lane,(float)sp->inputGain->getValue());repaint();};if(s.channel)s.channel->onClick=[this,sp]{
 sp->moduleExpanded[3]=true;layout();repaint();
 juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
 "LIBERTY  |  CANAL AUDIO",
 "Les sections INPUT, EQ, GATE et COMP peuvent rester ouvertes simultanément. "
 "Glissez la séparation au-dessus du panoramique pour ajuster la hauteur des faders.",
 "FERMER");
 };s.plugin->onClick=[sp]{auto&h=LibertyPluginHost::instance();if(sp->kind==Strip::instrument&&h.hasInstrumentForTrack(sp->lane))h.showInstrumentEditorForTrack(sp->lane);};s.unload->onClick=[this,sp]{auto&h=LibertyPluginHost::instance();if(sp->kind==Strip::instrument){h.unloadInstrumentForTrack(sp->lane);owner.notifyInstrumentChanged();}sync();repaint();};if(sp->kind==Strip::audio||sp->kind==Strip::instrument)for(int i=0;i<8;++i){sp->fx[(size_t)i]->onClick=[this,sp,i]{auto&h=LibertyPluginHost::instance();auto&k=LibertyOneKnobManager::instance();const int oneKey=sp->kind==Strip::audio?sp->lane*8+i:100000+sp->lane*8+i;if(k.hasEffect(oneKey))k.showEditor(oneKey);else if(sp->kind==Strip::audio)h.showEditorForTrackSlot(sp->lane,i);else h.showEffectEditorForInstrumentTrackSlot(sp->lane,i);};sp->fxUnload[(size_t)i]->onClick=[this,sp,i]{auto&h=LibertyPluginHost::instance();auto&k=LibertyOneKnobManager::instance();if(sp->kind==Strip::audio){h.unloadEffectForTrackSlot(sp->lane,i);k.clearEffect(sp->lane*8+i);}else{h.unloadEffectForInstrumentTrackSlot(sp->lane,i);k.clearEffect(100000+sp->lane*8+i);}sync();repaint();};}}
 
 void rebuild(){
   const int audio=owner.getAudioTrackCount();
   const int midi=owner.getMidiTrackCount();
   const int instrument=owner.getInstrumentTrackCount();
   const int wanted=audio+midi+instrument+1;
   bool matches=wanted==(int)strips.size();
   if(matches){
     int p=0;
     for(int i=0;i<audio&&matches;++i,++p)
       matches=strips[(size_t)p]->kind==Strip::audio&&strips[(size_t)p]->lane==i;
     for(int i=0;i<midi&&matches;++i,++p)
       matches=strips[(size_t)p]->kind==Strip::midi&&strips[(size_t)p]->lane==i;
     for(int i=0;i<instrument&&matches;++i,++p)
       matches=strips[(size_t)p]->kind==Strip::instrument&&strips[(size_t)p]->lane==i;
     if(matches)matches=strips[(size_t)p]->kind==Strip::master;
   }
   if(matches)return;

   // Keep the mixer view as the track list changes, without copying audio DSP.
   // Values and enable states always come from AudioEngine in sync().
   struct ViewSnapshot{
     std::array<bool,4> modules{};
     bool inserts=true;
     int firstFx=0;
     int faderHeight=250;
   };
   std::map<std::pair<int,int>,ViewSnapshot> viewSnapshots;
   for(const auto& item:strips){
     viewSnapshots[{(int)item->kind,item->lane}]=
       {item->moduleExpanded,item->insertsExpanded,
        item->firstFxSlot,item->preferredFaderHeight};
   }
   const auto selectedKind=selected?selected->kind:Strip::master;
   const int selectedLane=selected?selected->lane:-1;
   clear();
   auto makeRestored=[&](Strip::Kind kind,int lane){
      auto result=makeStrip(kind,lane);
      auto old=viewSnapshots.find({(int)kind,lane});
      if(old!=viewSnapshots.end()){
        result->moduleExpanded=old->second.modules;
        result->insertsExpanded=old->second.inserts;
        result->firstFxSlot=old->second.firstFx;
        result->preferredFaderHeight=old->second.faderHeight;
      }
      if(kind==Strip::audio)configureValuePopup(*result);
      return result;
   };
   for(int i=0;i<audio;++i)strips.push_back(makeRestored(Strip::audio,i));
   for(int i=0;i<midi;++i)strips.push_back(makeRestored(Strip::midi,i));
   for(int i=0;i<instrument;++i)strips.push_back(makeRestored(Strip::instrument,i));
   strips.push_back(makeRestored(Strip::master,0));
   const int arrangeTracks=audio+midi+instrument;
   if(arrangeTracks<=0)owner.selectedTrack=-1;
   if(selectedLane>=0){
     for(auto& item:strips){
       if(item->kind!=selectedKind||item->lane!=selectedLane)continue;
       selected=item.get();
       owner.selectedTrack=selectedKind==Strip::audio?selectedLane:
           (selectedKind==Strip::midi?audio+selectedLane:audio+midi+selectedLane);
       break;
     }
     if(selected==nullptr)
       owner.selectedTrack=arrangeTracks>0?
          juce::jlimit(0,arrangeTracks-1,owner.selectedTrack):-1;
   }
   layout();sync();
 }
 using StripGeometry=LibertyMixerLayout::Geometry;
 StripGeometry getStripGeometry(const Strip&s)const{
    const auto kind=s.kind==Strip::audio?LibertyMixerLayout::Kind::Audio:
        s.kind==Strip::instrument?LibertyMixerLayout::Kind::Instrument:
        s.kind==Strip::master?LibertyMixerLayout::Kind::Master:
        LibertyMixerLayout::Kind::Midi;
    return LibertyMixerLayout::calculate(s.bounds.getHeight(),kind,
        s.moduleExpanded,s.insertsExpanded,s.preferredFaderHeight);
 }
 void layout(){
    const int viewportHeight=juce::jmax(1,getHeight()-topY-38);
    const juce::Rectangle<int> viewport(0,topY,juce::jmax(1,getWidth()),viewportHeight);
    const int total=(int)strips.size()*stripW;
    const int max=juce::jmax(0,total-getWidth()+24);
    offset=juce::jlimit(0,max,offset);
    int x=12-offset;
    for(auto& s:strips){
      s->bounds={x,topY,stripW-4,viewportHeight};
      const bool vis=s->bounds.getRight()>0&&s->bounds.getX()<getWidth();
      const auto g=getStripGeometry(*s);
      const int bx=s->bounds.getX(),by=s->bounds.getY(),cx=s->bounds.getCentreX();
      auto put=[&](juce::Component* control,int rx,int ry,int w,int h,bool enabled=true){
          if(!control)return;
          const juce::Rectangle<int> r(bx+rx,by+ry,w,h);
          control->setBounds(r);
          control->setVisible(vis&&enabled&&h>0&&s->bounds.contains(r)&&viewport.contains(r));
      };
      if(s->kind==Strip::audio){
        for(int index=0;index<4;++index){
          const auto& p=g.modules[(size_t)index];
          auto& b=s->moduleTabs[(size_t)index];
          if(b){
            put(b.get(),7,p.headerY,s->bounds.getWidth()-45,20);
            b->setToggleState(s->moduleExpanded[(size_t)index],juce::dontSendNotification);
            b->setColour(juce::TextButton::buttonColourId,
                s->moduleExpanded[(size_t)index]?juce::Colour(0xff315b6d):juce::Colour(0xff253544));
          }
          if(index<3)put(s->modulePower[(size_t)index].get(),s->bounds.getWidth()-34,
              p.headerY+2,26,16,true);
        }
        const auto& e=g.modules[0],&gate=g.modules[1],&comp=g.modules[2],&input=g.modules[3];
        auto knob=[&](juce::Slider* control,int rx,const LibertyMixerLayout::Panel& p,int w){
          const int size=juce::jmin(26,p.bodyHeight-8);
          put(control,rx,p.bodyY+4,w,size,p.bodyHeight>=25);
        };
        for(int band=0;band<3;++band)
          knob(s->eqKnobs[(size_t)band].get(),16+band*44,e,29);
        knob(s->gateThreshold.get(),57,gate,31);
        knob(s->compThreshold.get(),28,comp,31);
        knob(s->compRatio.get(),86,comp,31);
        const int inputKnob=juce::jmin(26,input.bodyHeight-8);
        put(s->channel.get(),8,input.bodyY+8,44,20,input.bodyHeight>=30);
        put(s->inputGain.get(),65,input.bodyY+4,28,inputKnob,input.bodyHeight>=25);
        put(s->highPass.get(),105,input.bodyY+4,28,inputKnob,input.bodyHeight>=25);
      }else{
        for(auto&b:s->moduleTabs)if(b)b->setVisible(false);
        for(auto&b:s->modulePower)if(b)b->setVisible(false);
        for(auto&b:s->eqKnobs)if(b)b->setVisible(false);
        for(auto* k:{s->gateThreshold.get(),s->compThreshold.get(),s->compRatio.get(),
                     s->inputGain.get(),s->highPass.get()})if(k)k->setVisible(false);
        if(s->channel)s->channel->setVisible(false);
      }
      if(s->kind==Strip::audio||s->kind==Strip::instrument){
        if(s->fxHeader){
          put(s->fxHeader.get(),7,g.insertsHeaderY,s->bounds.getWidth()-14,21);
          juce::String text="INSERTS";
          if(s->insertsExpanded){
            const int first=juce::jlimit(0,juce::jmax(0,8-g.visibleFx),s->firstFxSlot);
            s->firstFxSlot=first;
            text="INSERTS "+juce::String(first+1)+"-"+juce::String(first+g.visibleFx)+"/8";
          }
          s->fxHeader->setButtonText(text);
          s->fxHeader->setToggleState(s->insertsExpanded,juce::dontSendNotification);
        }
        if(s->kind==Strip::instrument){
          put(s->plugin.get(),8,82,s->bounds.getWidth()-43,24);
          put(s->unload.get(),s->bounds.getWidth()-29,82,18,24);
        }else{
          if(s->plugin)s->plugin->setVisible(false);
          if(s->unload)s->unload->setVisible(false);
        }
        for(int i=0;i<8;++i){
          const int index=i-s->firstFxSlot;
          const bool show=s->insertsExpanded&&index>=0&&index<g.visibleFx;
          const int row=g.fxY+index*g.fxStep;
          put(s->fx[(size_t)i].get(),8,row,s->bounds.getWidth()-43,
             g.fxStep>0?g.fxStep-3:0,show);
          put(s->fxUnload[(size_t)i].get(),s->bounds.getWidth()-29,row,18,
             g.fxStep>0?g.fxStep-3:0,show);
        }
        put(s->pan.get(),cx-bx-21,g.panY,42,34);
        put(s->mute.get(),cx-bx-42,g.muteY,36,26);
        put(s->solo.get(),cx-bx+6,g.muteY,36,26);
      }else{
        if(s->fxHeader)s->fxHeader->setVisible(false);
        if(s->plugin)s->plugin->setVisible(false);
        if(s->unload)s->unload->setVisible(false);
        if(s->pan)s->pan->setVisible(false);
        if(s->mute)s->mute->setVisible(false);
        if(s->solo)s->solo->setVisible(false);
        for(auto&b:s->fx)if(b)b->setVisible(false);
        for(auto&b:s->fxUnload)if(b)b->setVisible(false);
      }
      const bool showFader=s->kind!=Strip::midi&&g.faderHeight>=60;
      put(s->fader.get(),cx-bx-29,g.faderY,58,g.faderHeight,showFader);
      x+=stripW;
    }
    scroll.setBounds(12,getHeight()-24,juce::jmax(1,getWidth()-24),16);
    scroll.setRangeLimits(0,juce::jmax(1,max+getWidth()));
    scroll.setCurrentRange(offset,getWidth(),juce::dontSendNotification);
    scroll.setVisible(max>0);
    verticalScroll.setVisible(false);
 }
 juce::String name(const Strip&s)const{if(s.kind==Strip::master)return "MASTER";const int logical=s.kind==Strip::audio?s.lane:(s.kind==Strip::midi?owner.getAudioTrackCount()+s.lane:owner.getAudioTrackCount()+owner.getMidiTrackCount()+s.lane);const auto n=getLibertyTrackName(logical);if(n.isNotEmpty())return n;if(s.kind==Strip::audio)return "Audio "+juce::String(s.lane+1);if(s.kind==Strip::midi)return "MIDI "+juce::String(s.lane+1);return "Instrument "+juce::String(s.lane+1);}
 bool isSelected(const Strip&s)const{if(selected==&s)return true;if(s.kind==Strip::master)return false;const int logical=s.kind==Strip::audio?s.lane:(s.kind==Strip::midi?owner.getAudioTrackCount()+s.lane:owner.getAudioTrackCount()+owner.getMidiTrackCount()+s.lane);return owner.selectedTrack==logical;}void draw(juce::Graphics&g,Strip&s){
    if(s.bounds.isEmpty()||s.bounds.getRight()<0||s.bounds.getX()>getWidth())return;
    juce::Graphics::ScopedSaveState savedState(g);
    g.reduceClipRegion(s.bounds);
    const bool chosen=isSelected(s);
    const int x=s.bounds.getX(),y=s.bounds.getY(),w=s.bounds.getWidth(),h=s.bounds.getHeight();
    g.setColour(chosen?juce::Colour(0xff223944):juce::Colour(0xff18212b));
    g.fillRoundedRectangle(s.bounds.toFloat(),6.f);
    if(s.kind!=Strip::master){
      const int logical=s.kind==Strip::audio?s.lane:
          (s.kind==Strip::midi?owner.getAudioTrackCount()+s.lane:
           owner.getAudioTrackCount()+owner.getMidiTrackCount()+s.lane);
      const auto tint=trackColour(getLibertyTrackColourId(logical));
      g.setColour(tint.withAlpha(.16f));g.fillRoundedRectangle(s.bounds.toFloat(),6.f);
    }
    g.setColour(chosen?juce::Colour(0xff54d6dc):juce::Colour(0xff344452));
    g.drawRoundedRectangle(s.bounds.toFloat(),6.f,chosen?2.f:1.f);
    g.setColour(s.kind==Strip::master?juce::Colour(0xffe4a85e):
        trackColour(getLibertyTrackColourId(s.kind==Strip::audio?s.lane:
           s.kind==Strip::midi?owner.getAudioTrackCount()+s.lane:
           owner.getAudioTrackCount()+owner.getMidiTrackCount()+s.lane)));
    g.fillRoundedRectangle((float)x,(float)y,(float)w,6.f,3.f);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(12.f,juce::Font::bold));
    g.drawFittedText(name(s),x+6,y+14,w-12,20,juce::Justification::centred,1);
    g.setColour(juce::Colour(0xffbdcfdb));g.setFont(juce::Font(8.5f,juce::Font::bold));
    g.drawText(s.kind==Strip::audio?"PISTE AUDIO":s.kind==Strip::midi?"PISTE MIDI":
       s.kind==Strip::instrument?"INSTRUMENT VIRTUEL":"MASTER STÉRÉO",
       x+4,y+36,w-8,15,juce::Justification::centred);
    g.setColour(juce::Colour(0xff111922));
    g.fillRoundedRectangle((float)x+8,(float)y+56,(float)w-16,24.f,3.f);
    g.setColour(juce::Colour(0xffd1dce7));g.setFont(juce::Font(8.5f));
    g.drawText(s.kind==Strip::master?"OUT 1-2":"ROUTE > MASTER",
       x+10,y+58,w-20,18,juce::Justification::centred,true);
    const auto geom=getStripGeometry(s);
    auto panel=[&](int top,int height,juce::Colour accent){
      if(height<=3)return;
      const auto r=juce::Rectangle<float>((float)x+5,(float)(y+top),
                        (float)w-10,(float)height);
      g.setColour(juce::Colour(0xff101c28));g.fillRoundedRectangle(r,3.f);
      g.setColour(accent.withAlpha(.72f));g.drawRoundedRectangle(r,3.f,1.f);
    };
    if(s.kind==Strip::audio){
      for(int index:{3,0,1,2}){
        const auto& p=geom.modules[(size_t)index];
        panel(p.headerY-1,22,juce::Colour(0xff547f9a));
        if(p.bodyHeight>0){
          panel(p.bodyY,p.bodyHeight-1,juce::Colour(0xff3a647b));
          if(p.bodyHeight>=40){
            g.setColour(juce::Colour(0xffa8bccb));g.setFont(juce::Font(8.f));
            const int labelY=y+p.bodyY+p.bodyHeight-12;
            if(index==0){
              for(int band=0;band<3;++band)g.drawText(
                band==0?"GRAVES":band==1?"MÉDIUMS":"AIGUS",
                x+8+band*44,labelY,42,10,juce::Justification::centred);
            }else if(index==1)g.drawText("SEUIL",x+49,labelY,50,10,juce::Justification::centred);
            else if(index==2){
              g.drawText("SEUIL",x+22,labelY,42,10,juce::Justification::centred);
              g.drawText("RATIO",x+80,labelY,42,10,juce::Justification::centred);
            }else{
              g.drawText("GAIN",x+62,labelY,35,10,juce::Justification::centred);
              g.drawText("COUPE-BAS",x+98,labelY,45,10,juce::Justification::centred);
            }
          }
        }
      }
    }
    if(s.kind==Strip::audio||s.kind==Strip::instrument){
      panel(geom.insertsHeaderY-1,23,juce::Colour(0xff5186a5));
      if(s.insertsExpanded&&geom.visibleFx>0)
        panel(geom.fxY-2,geom.fxStep*geom.visibleFx+3,juce::Colour(0xff5186a5));
      panel(geom.panY-2,38,juce::Colour(0xff688aa7));
      panel(geom.muteY-2,30,juce::Colour(0xff8c9eb1));
      g.setColour(juce::Colour(0xff6a9eb9));
      g.fillRoundedRectangle((float)x+12,(float)(y+geom.splitterY),
                             (float)w-24,3.f,1.5f);
      g.setColour(juce::Colour(0xffbed4e0));
      g.fillRoundedRectangle((float)x+(float)w*.5f-16.f,
                             (float)(y+geom.splitterY-1),32.f,5.f,2.f);
    }else if(s.kind==Strip::master){
      g.setColour(juce::Colour(0xffb5c3d1));g.setFont(juce::Font(8.f,juce::Font::bold));
      g.drawText("MASTER BUS",x,y+92,w,18,juce::Justification::centred);
    }
    if(s.kind!=Strip::midi){
      panel(geom.faderY-3,h-geom.faderY+1,juce::Colour(0xffa88d62));
      drawMeter(g,s);
      const int meterTop=y+geom.faderY,meterBottom=s.bounds.getBottom()-36;
      if(meterBottom-meterTop>=40){
        g.setColour(juce::Colour(0xffb5c3d1));g.setFont(juce::Font(8.f));
        for(int i=0;i<4;++i){
          const int yy=i==0?meterTop:i==1?meterTop+(meterBottom-meterTop)/4:
                        i==2?meterTop+(meterBottom-meterTop)/2:meterBottom-12;
          g.drawText(i==0?"+6":i==1?"0":i==2?"-18":"-60",
             s.bounds.getRight()-30,yy,22,12,juce::Justification::right);
        }
      }
    }
 }
 void drawMeter(juce::Graphics&g,const Strip&s){if(s.kind==Strip::midi)return;const int top=s.bounds.getY()+getStripGeometry(s).faderY,bottom=s.bounds.getBottom()-36;if(bottom-top<40)return;auto meter=juce::Rectangle<float>((float)s.bounds.getX()+10.f,(float)top,12.f,(float)(bottom-top));g.setColour(juce::Colour(0xff0b0f14));g.fillRoundedRectangle(meter,2.f);g.setColour(juce::Colour(0xff344452));g.drawRoundedRectangle(meter,2.f,1.f);const float db=s.level<=.00001f?-60.f:juce::jlimit(-60.f,6.f,juce::Decibels::gainToDecibels(s.level));const float norm=juce::jlimit(0.f,1.f,(db+60.f)/66.f);auto fill=meter;fill.setY(meter.getBottom()-meter.getHeight()*norm);fill.setHeight(meter.getHeight()*norm);juce::ColourGradient grad(juce::Colour(0xff54d6dc),fill.getX(),fill.getBottom(),juce::Colour(0xffe4a85e),fill.getX(),fill.getY(),false);g.setGradientFill(grad);g.fillRoundedRectangle(fill,2.f);if(s.peak>.98f){g.setColour(juce::Colour(0xffff4e4e));g.fillRect((int)meter.getX(),(int)meter.getY(),(int)meter.getWidth(),3);}}
 float audioLevel(int track)const{if(!owner.audioEngine.isPlaying()||owner.audioEngine.isTrackMuted(track))return 0.f;const auto bufferSnapshot=owner.audioEngine.getAudioBufferSnapshot(track);const auto*b=bufferSnapshot.get();const double rate=owner.audioEngine.getSampleRate();if(!b||rate<=0.||b->getNumSamples()<=0)return 0.f;const double sec=owner.audioEngine.getCurrentTimeSeconds()-owner.audioEngine.getTrackStartSeconds(track);if(sec<0.||sec>=owner.audioEngine.getAudioFileLengthSeconds(track))return 0.f;const int centre=juce::jlimit(0,b->getNumSamples()-1,(int)std::llround(sec*rate)),start=juce::jmax(0,centre-128),count=juce::jmin(256,b->getNumSamples()-start);float peak=0.f;for(int c=0;c<juce::jmin(2,b->getNumChannels());++c){const float*d=b->getReadPointer(c,start);for(int i=0;i<count;++i)peak=juce::jmax(peak,std::abs(d[i]));}return peak*owner.audioEngine.getTrackGain(track);}
 void updateMeters(){float master=0.f;for(auto&s:strips){float raw=0.f;if(s->kind==Strip::audio){raw=audioLevel(s->lane);master+=raw;}else if(s->kind==Strip::midi)continue;s->peak=juce::jmax(raw,s->peak*.90f);s->level=juce::jmax(raw,s->level*.82f);}for(auto&s:strips)if(s->kind==Strip::master){const float raw=juce::jmin(1.2f,master*owner.audioEngine.getMasterGain());s->peak=juce::jmax(raw,s->peak*.90f);s->level=juce::jmax(raw,s->level*.82f);}}
 void sync(){syncing=true;auto&h=LibertyPluginHost::instance();auto&k=LibertyOneKnobManager::instance();for(auto&s:strips){if(s->kind==Strip::audio){s->fader->setEnabled(true);s->plugin->setEnabled(true);s->fader->setTextValueSuffix(" dB");s->fader->setValue(gainToDb(owner.audioEngine.getTrackGain(s->lane)),juce::dontSendNotification);s->pan->setValue(owner.audioEngine.getTrackPan(s->lane),juce::dontSendNotification);
 // Always show the actual audio-engine values, even after changing projects,
 // loading a snapshot or adding/removing tracks.
 const auto channelSettings=owner.audioEngine.getChannelProcessingSettings(s->lane);
 s->eqOn=channelSettings.eqEnabled;
 s->gateOn=channelSettings.gateEnabled;
 s->compOn=channelSettings.compressorEnabled;
 s->inputGain->setValue(channelSettings.inputGainDb,juce::dontSendNotification);
 s->highPass->setValue(channelSettings.highPassHz,juce::dontSendNotification);
 s->gateThreshold->setValue(channelSettings.gateThresholdDb,juce::dontSendNotification);
 s->compThreshold->setValue(channelSettings.compressorThresholdDb,juce::dontSendNotification);
 s->compRatio->setValue(channelSettings.compressorRatio,juce::dontSendNotification);
 s->eqKnobs[0]->setValue(channelSettings.eqLowDb,juce::dontSendNotification);
 s->eqKnobs[1]->setValue(channelSettings.eqMidDb,juce::dontSendNotification);
 s->eqKnobs[2]->setValue(channelSettings.eqHighDb,juce::dontSendNotification);
 refreshPower(*s);
 s->plugin->setButtonText("AUDIO");s->plugin->setEnabled(false);for(int i=0;i<8;++i){const bool has=h.hasEffectForTrackSlot(s->lane,i);const int oneKey=s->lane*8+i;const bool hasOne=k.hasEffect(oneKey);const auto label=has?h.getEffectNameForTrackSlot(s->lane,i):(hasOne?k.getName(oneKey):"FX "+juce::String(i+1)+" - EMPTY");if(s->fx[(size_t)i]->getButtonText()!=label)s->fx[(size_t)i]->setButtonText(label);if(s->fxUnload[(size_t)i]->isEnabled()!=(has||hasOne))s->fxUnload[(size_t)i]->setEnabled(has||hasOne);}{const bool m=owner.audioEngine.isTrackMuted(s->lane),so=owner.audioEngine.isTrackSolo(s->lane);s->mute->setToggleState(m,juce::dontSendNotification);s->solo->setToggleState(so,juce::dontSendNotification);s->mute->setColour(juce::TextButton::buttonColourId,m?juce::Colour(0xffa44141):juce::Colour(0xff25323e));s->solo->setColour(juce::TextButton::buttonColourId,so?juce::Colour(0xffa18432):juce::Colour(0xff25323e));}}else if(s->kind==Strip::midi){s->fader->setValue(-60.0,juce::dontSendNotification);s->fader->setTextValueSuffix("");s->plugin->setButtonText("MIDI");s->plugin->setEnabled(false);s->unload->setEnabled(false);}else if(s->kind==Strip::instrument){s->fader->setEnabled(true);s->plugin->setEnabled(true);s->fader->setTextValueSuffix(" dB");s->fader->setValue(gainToDb(owner.audioEngine.getInstrumentTrackGain(s->lane)),juce::dontSendNotification);s->pan->setValue(owner.audioEngine.getInstrumentTrackPan(s->lane),juce::dontSendNotification);bool ho=h.hasInstrumentForTrack(s->lane);juce::String n=ho?h.getInstrumentNameForTrack(s->lane):"INSTRUMENT - EMPTY";s->plugin->setButtonText(n);s->unload->setEnabled(ho);for(int i=0;i<8;++i){const bool has=h.hasEffectForInstrumentTrackSlot(s->lane,i);const int oneKey=100000+s->lane*8+i;const bool hasOne=k.hasEffect(oneKey);const auto label=has?h.getEffectNameForInstrumentTrackSlot(s->lane,i):(hasOne?k.getName(oneKey):"FX "+juce::String(i+1)+" - EMPTY");if(s->fx[(size_t)i]->getButtonText()!=label)s->fx[(size_t)i]->setButtonText(label);if(s->fxUnload[(size_t)i]->isEnabled()!=(has||hasOne))s->fxUnload[(size_t)i]->setEnabled(has||hasOne);}{const bool m=owner.audioEngine.isInstrumentTrackMuted(s->lane),so=owner.audioEngine.isInstrumentTrackSolo(s->lane);s->mute->setToggleState(m,juce::dontSendNotification);s->solo->setToggleState(so,juce::dontSendNotification);s->mute->setColour(juce::TextButton::buttonColourId,m?juce::Colour(0xffa44141):juce::Colour(0xff25323e));s->solo->setColour(juce::TextButton::buttonColourId,so?juce::Colour(0xffa18432):juce::Colour(0xff25323e));}}else{s->fader->setEnabled(true);s->fader->setTextValueSuffix(" dB");s->fader->setValue(gainToDb(owner.audioEngine.getMasterGain()),juce::dontSendNotification);s->plugin->setButtonText("MASTER OUTPUT");s->plugin->setEnabled(false);s->unload->setEnabled(false);}}syncing=false;}
 void timerCallback()override{if(!visible)return;updateMeters();for(auto&strip:strips)if(!strip->bounds.isEmpty()&&strip->kind!=Strip::midi){const int meterTop=strip->bounds.getY()+getStripGeometry(*strip).faderY;const int meterBottom=strip->bounds.getBottom()-36;if(meterBottom-meterTop>=40){auto meter=juce::Rectangle<int>(strip->bounds.getX()+8,meterTop,18,meterBottom-meterTop);repaint(meter);}}}
 Strip*draggingHeight=nullptr;int dragStartY=0,dragStartPreference=0;
 MainComponent&owner;ConsoleLookAndFeel lookAndFeel;std::vector<std::unique_ptr<Strip>>strips;juce::ScrollBar scroll{false};juce::ScrollBar verticalScroll{true};int verticalOffset=0;int offset=0;bool syncing=false,visible=false;Strip*selected=nullptr;
};
// The page tab in PageModeCoordinator is the only mixer navigation button.
// Do not create duplicate controls over the transport.
class MixConsoleController final{
public:
 explicit MixConsoleController(MainComponent&o):owner(o),view(o){}
 ~MixConsoleController(){shutdown();}
 void shutdown(){if(stopped.exchange(true))return;shown=false;view.setConsoleVisible(false);}
 void show(bool b){if(stopped.load())return;shown=b;view.setConsoleVisible(b);if(b)view.toFront(true);owner.repaint();}
 bool isVisible()const{return shown;}
 void refresh(){if(stopped.load())return;view.refreshStatic();}
 void resizeToOwner(){if(stopped.load()||!shown)return;view.setBounds(0,transportHeight,owner.getWidth(),juce::jmax(1,owner.getHeight()-transportHeight));}
 int getDropTrack(juce::Point<int>p,bool i)const{return view.getDropTrackForOwnerPoint(p,i);}
private:
 MainComponent&owner;MixConsoleView view;std::atomic<bool>stopped{false};bool shown=false;
};
std::map<MainComponent*,std::unique_ptr<MixConsoleController>>controllers;
class Bootstrap final:private juce::Timer{public:Bootstrap(){startTimerHz(10);}~Bootstrap()override{shutdown();}void shutdown(){stopTimer();for(auto&p:controllers)if(p.second)p.second->shutdown();controllers.clear();}private:void timerCallback()override{auto&d=juce::Desktop::getInstance();for(int i=0;i<d.getNumComponents();++i)if(auto*w=dynamic_cast<juce::DocumentWindow*>(d.getComponent(i)))if(auto*m=dynamic_cast<MainComponent*>(w->getContentComponent()))if(!controllers.count(m)){controllers.emplace(m,std::make_unique<MixConsoleController>(*m));stopTimer();break;}}};Bootstrap bootstrap;
}
void ensureLibertyMixConsoleController(MainComponent* o)
{
    if (o != nullptr && !controllers.count(o))
        controllers.emplace(o, std::make_unique<MixConsoleController>(*o));
}
void showLibertyMixConsolePage(MainComponent* o, bool visible)
{
    if (o == nullptr) return;
    ensureLibertyMixConsoleController(o);
    auto it = controllers.find(o);
    if (it != controllers.end() && it->second)
        it->second->show(visible);
}
bool isLibertyMixConsoleVisible(MainComponent*o){auto i=controllers.find(o);return o&&i!=controllers.end()&&i->second&&i->second->isVisible();}
int getLibertyMixConsolePluginDropTrack(MainComponent*o,juce::Point<int>p,bool i){auto it=controllers.find(o);return o&&it!=controllers.end()&&it->second?it->second->getDropTrack(p,i):-1;} void refreshLibertyMixConsole(MainComponent*o){auto it=controllers.find(o);if(o&&it!=controllers.end()&&it->second)it->second->refresh();} void resizeLibertyMixConsole(MainComponent*o){auto it=controllers.find(o);if(o&&it!=controllers.end()&&it->second)it->second->resizeToOwner();}
void shutdownLibertyMixConsoleController(){bootstrap.shutdown();}
