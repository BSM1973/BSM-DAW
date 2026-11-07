#define private public
#include "MainComponent.h"
#undef private
#include "PluginHost.h"
#include "OneKnobEffects.h"
#include "MixConsoleLayout.h"
#include "MixerProgressiveFx.h"
#include "NoOverlapLayout.h"
#include "MixerSearchFilter.h"
#include "LibertyFrenchLabels.h"
#include "MixConsoleSoloLogic.h"
#include "MixConsoleMeter.h"
#include "ChannelDialogLayout.h"
#include "ChannelInspectorSession.h"
#include "ChannelInspectorPresets.h"
#include "ChannelUserPresets.h"
#include "ChannelPresetStatus.h"
#include "ChannelSectionComparison.h"
#include "ChannelABHistory.h"
#include "ChannelABGestureHistory.h"
#include "ChannelABShortcut.h"
#include "ChannelABOverview.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <cmath>
#include <map>
#include <array>
#include <memory>
#include <vector>

int getLibertyTrackColourId(int track);
void setLibertyTrackColourId(int track,int colourId);
juce::String getLibertyTrackName(int track);
void setLibertyTrackName(int track,const juce::String&);
juce::Colour libertySharedTrackColour(int colourId);
void showLibertyMixerColourPalette(juce::Point<int> screenPoint,int selectedId,
                                  std::function<void(int)> onSelect);
void showLibertyEffectBrowserForSlot(MainComponent*,bool instrument,int lane,int slot);
int getLibertyBrowserReservedWidth(MainComponent*);

namespace {
constexpr int transportHeight=76, stripW=150, topY=66;
float gainToDb(float g){return g<=.00001f?-60.f:juce::jlimit(-60.f,6.f,juce::Decibels::gainToDecibels(g));}
float dbToGain(float d){return d<=-59.9f?0.f:juce::Decibels::decibelsToGain(d);}
juce::Colour trackColour(int id){return libertySharedTrackColour(id);}
// Compact console inspired by the uploaded reference video: understated
// graphite surfaces, sage accents and a warm Master, preserving DSP layout.
namespace ModernMixer {
const juce::Colour canvas{0xff101a1c};
const juce::Colour header{0xff172627};
const juce::Colour strip{0xff1d2d2e};
const juce::Colour selected{0xff2b4141};
const juce::Colour border{0xff385052};
const juce::Colour panel{0xff223638};
const juce::Colour text{0xffeef5f2};
const juce::Colour secondary{0xffa5bdb6};
const juce::Colour sage{0xff75d3b9};
const juce::Colour amber{0xffdfa167};
}

class ConsoleLookAndFeel final:public juce::LookAndFeel_V4{
public:
 void drawLinearSlider(juce::Graphics& g,int x,int y,int w,int h,float pos,float,
                       float,const juce::Slider::SliderStyle style,juce::Slider& slider)override
 {
     if(style!=juce::Slider::LinearVertical)
     {
         juce::LookAndFeel_V4::drawLinearSlider(g,x,y,w,h,pos,0.f,0.f,style,slider);
         return;
     }
     const float cx=(float)x+(float)w*.5f;
     const auto rail=juce::Rectangle<float>(cx-2.2f,(float)y+6.f,4.4f,
                                           (float)juce::jmax(0,h-12));
     g.setColour(juce::Colour(0xff091415));
     g.fillRoundedRectangle(rail,2.2f);
     g.setColour(juce::Colour(0xff455d59));
     g.drawRoundedRectangle(rail,2.2f,.8f);
     g.setColour(ModernMixer::sage.withAlpha(.5f));
     g.fillRoundedRectangle(cx-1.f,pos+5.f,2.f,
         juce::jmax(0.f,rail.getBottom()-pos-5.f),1.f);
     const auto thumb=juce::Rectangle<float>((float)x+3.f,pos-7.f,
                                            (float)juce::jmax(1,w-6),14.f);
     g.setColour(juce::Colour(0xffd4ded9));
     g.fillRoundedRectangle(thumb,3.f);
     g.setColour(juce::Colour(0xff6c8984));
     g.drawRoundedRectangle(thumb,3.f,.8f);
     g.setColour(juce::Colour(0xff536d67));
     g.drawHorizontalLine((int)pos,thumb.getX()+5.f,thumb.getRight()-5.f);
 }
 void drawRotarySlider(juce::Graphics&g,int x,int y,int w,int h,
    float position,float start,float end,juce::Slider& slider)override{
    const float d=(float)juce::jmin(w,h);
    if(d<10.f)return;
    const float cx=(float)x+(float)w*.5f;
    const float cy=(float)y+(float)h*.5f;
    const float radius=(d-2.f)*.5f;
    const float angle=start+juce::jlimit(0.f,1.f,position)*(end-start);
    const auto accent=slider.findColour(juce::Slider::rotarySliderFillColourId);
    // Flat rotary dial rather than brushed-metal bevels.
    g.setColour(juce::Colour(0xff182a2c));
    g.fillEllipse(cx-radius,cy-radius,2.f*radius,2.f*radius);
    g.setColour(juce::Colour(0xff66827a));
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
    g.setColour(juce::Colour(0xff364d4d));
    g.fillEllipse(cx-bodyRadius,cy-bodyRadius,2.f*bodyRadius,2.f*bodyRadius);
    g.setColour(juce::Colour(0xffa0bcb3).withAlpha(.68f));
    g.drawEllipse(cx-bodyRadius,cy-bodyRadius,2.f*bodyRadius,2.f*bodyRadius,.85f);
    g.setColour(juce::Colour(0xff223334));
    g.fillEllipse(cx-bodyRadius*.58f,cy-bodyRadius*.58f,
                  1.16f*bodyRadius,1.16f*bodyRadius);
    const float inner=bodyRadius*.15f,outer=bodyRadius*.83f;
    g.setColour(accent.brighter(.28f));
    g.drawLine(cx+std::sin(angle)*inner,cy-std::cos(angle)*inner,
               cx+std::sin(angle)*outer,cy-std::cos(angle)*outer,1.7f);
}};

class LibertyChannelInspector final:public juce::Component, private juce::Timer {
public:
 LibertyChannelInspector(AudioEngine& source,int track,juce::String trackLabel,
                         std::function<void()> refresh)
   :engine(source),trackIndex(track),name(std::move(trackLabel)),
    afterApply(std::move(refresh)),session(source,track){
    const auto v=session.pending();
    configure(knobs[0],-24,24,.5,v.inputGainDb," dB",juce::Colour(0xff72c6eb));
    configure(knobs[1],0,500,1,v.highPassHz," Hz",juce::Colour(0xff72c6eb));
    configure(knobs[2],-80,-20,1,v.gateThresholdDb," dB",juce::Colour(0xff91d1a8));
    configure(knobs[3],-48,0,1,v.compressorThresholdDb," dB",juce::Colour(0xffe0b276));
    configure(knobs[4],1,12,.5,v.compressorRatio," :1",juce::Colour(0xffe0b276));
    configure(knobs[5],-18,18,.5,v.eqLowDb," dB",juce::Colour(0xff60d3d6));
    configure(knobs[6],-18,18,.5,v.eqMidDb," dB",juce::Colour(0xff60d3d6));
    configure(knobs[7],-18,18,.5,v.eqHighDb," dB",juce::Colour(0xff60d3d6));
    for(size_t i=0;i<knobs.size();++i)
    {
        auto& knob=knobs[i];
        const int index=static_cast<int>(i);
        knob.onDragStart=[this,index]{gestureHistory.begin(index);};
        knob.onDragEnd=[this,index]{gestureHistory.end(index);};
        knob.onValueChange=[this,index]{trackControlChange(index);};
    }
    restoreMatchingPresetLabels();
    openingPresetIndicator=presetIndicator;
    for(auto& button:power){
        button.setMouseClickGrabsKeyboardFocus(false);
        button.setClickingTogglesState(true);
        addAndMakeVisible(button);
    }
    power[0].setToggleState(v.gateEnabled,juce::dontSendNotification);
    power[1].setToggleState(v.compressorEnabled,juce::dontSendNotification);
    power[2].setToggleState(v.eqEnabled,juce::dontSendNotification);
    for(int i=0;i<3;++i){
        power[(size_t)i].onClick=[this,i]{
            abHistory.record(captureHistoryFrame());
            session.setPending(readControls());
            refreshHistoryButtons();
            refreshPower(i);
            repaint();
        };
        refreshPower(i);
    }
    for(auto* button:{&resetButton,&copyButton,&pasteButton,&variantButton,&presetsButton,&undoButton,&redoButton,&cancelButton,&applyButton}){
        button->setMouseClickGrabsKeyboardFocus(false);
        button->setColour(juce::TextButton::textColourOffId,juce::Colours::white);
        addAndMakeVisible(*button);
    }
    resetButton.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff34475a));
    copyButton.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff28546a));
    pasteButton.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff28546a));
    refreshVariantButton();
    presetsButton.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff28546a));
    undoButton.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff305065));
    redoButton.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff305065));
    cancelButton.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff34475a));
    applyButton.setColour(juce::TextButton::buttonColourId,juce::Colour(0xff207b86));
    resetButton.setTooltip("Valeurs neutres, sans modifier le son avant APPLIQUER");
    copyButton.setTooltip(juce::String::fromUTF8("Copier les réglages préparés pour les coller sur une autre piste audio."));
    pasteButton.setTooltip(juce::String::fromUTF8("Coller les réglages sans modifier le son avant APPLIQUER."));
    variantButton.setTooltip(juce::String::fromUTF8(
        "Clic : changer de variante. Maj + clic : vue globale des différences A/B. "
        "Cmd+1 : MODE A ; Cmd+2 : MODE B. Aucun son modifié avant APPLIQUER."));
    presetsButton.setTooltip(juce::String::fromUTF8(
        "Préréglages CANAL et historique A/B. Cmd+Z : annuler ; Cmd+Maj+Z : rétablir. "
        "La saisie numérique conserve son propre Annuler/Rétablir."));
    undoButton.setTooltip(juce::String::fromUTF8("ANNULER la dernière modification A/B (Cmd+Z)."));
    redoButton.setTooltip(juce::String::fromUTF8("RÉTABLIR la modification A/B annulée (Cmd+Maj+Z)."));
    undoButton.onClick=[this]{restoreABEdit(false);};
    redoButton.onClick=[this]{restoreABEdit(true);};
    refreshHistoryButtons();
    pasteButton.setEnabled(LibertyChannelInspectorState::Session::canPaste());
    resetButton.onClick=[this]{
        rememberABEdit();
        session.reset();
        writeControls(session.pending());
        presetIndicator.select(session.variantIndex(),
            juce::String::fromUTF8("Neutre"),readControls());
        repaint();
    };
    copyButton.onClick=[this]{
        session.setPending(readControls());
        session.copy();
        pasteButton.setEnabled(true);
    };
    pasteButton.onClick=[this]{
        if(!LibertyChannelInspectorState::Session::canPaste())return;
        rememberABEdit();
        if(session.paste())
        {
            writeControls(session.pending());
            presetIndicator.clear(session.variantIndex());
            repaint();
        }
    };
    variantButton.onClick=[this]{
        // Shift+click shows a read-only comparison instead of switching.
        if(juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown())
        {
            showGlobalABComparison();
            return;
        }
        selectVariantForEditing(1-session.variantIndex());
    };
    presetsButton.onClick=[this]{
        juce::PopupMenu menu;
        menu.addItem(1700,juce::String::fromUTF8("ANNULER MODIFICATION A/B"),abHistory.canUndo());
        menu.addItem(1701,juce::String::fromUTF8("RÉTABLIR MODIFICATION A/B"),abHistory.canRedo());
        menu.addSeparator();
        menu.addSectionHeader(juce::String::fromUTF8("PRÉRÉGLAGES D'USINE"));
        for(int id=1;id<=LibertyChannelInspectorPresets::count;++id)
            menu.addItem(id,juce::String::fromUTF8(LibertyChannelInspectorPresets::name(id)));
        LibertyChannelUserPresets::Library library;
        const bool loaded=library.load(LibertyChannelUserPresets::Library::defaultFile());
        const auto saved=library.items();
        menu.addSeparator();
        menu.addSectionHeader("FAVORIS");
        if(loaded)
        {
            for(size_t i=0;i<saved.size();++i)
                if(saved[i].favorite)
                    menu.addItem(700+(int)i,saved[i].name);
        }
        menu.addSeparator();
        menu.addSectionHeader(juce::String::fromUTF8("MES PRÉRÉGLAGES"));
        if(loaded)
        {
            for(size_t i=0;i<saved.size();++i)
                menu.addItem(100+(int)i,saved[i].name);
        }
        menu.addSeparator();
        menu.addItem(1000,juce::String::fromUTF8("ENREGISTRER MES RÉGLAGES..."),loaded);
        menu.addItem(1001,juce::String::fromUTF8("IMPORTER UNE BIBLIOTHÈQUE..."),loaded);
        menu.addItem(1002,juce::String::fromUTF8("EXPORTER MES PRÉRÉGLAGES..."),
                     loaded && !saved.empty());
        juce::PopupMenu remove;
        for(size_t i=0;i<saved.size();++i)
            remove.addItem(200+(int)i,saved[i].name);
        menu.addSubMenu(juce::String::fromUTF8("SUPPRIMER UN PRÉRÉGLAGE"),remove,
                        loaded && !saved.empty());
        juce::PopupMenu renameMenu,duplicateMenu;
        for(size_t i=0;i<saved.size();++i)
        {
            renameMenu.addItem(300+(int)i,saved[i].name);
            duplicateMenu.addItem(400+(int)i,saved[i].name);
        }
        menu.addSubMenu(juce::String::fromUTF8("RENOMMER UN PRÉRÉGLAGE"),
                        renameMenu,loaded && !saved.empty());
        menu.addSubMenu(juce::String::fromUTF8("DUPLIQUER UN PRÉRÉGLAGE"),
                        duplicateMenu,loaded && !saved.empty()
                            && (int)saved.size()<LibertyChannelUserPresets::Library::maxPresets);
        juce::PopupMenu favoriteMenu;
        for(size_t i=0;i<saved.size();++i)
            favoriteMenu.addItem(800+(int)i,
                (saved[i].favorite
                    ? juce::String::fromUTF8("RETIRER DES FAVORIS : ")
                    : juce::String::fromUTF8("AJOUTER AUX FAVORIS : "))
                +saved[i].name);
        menu.addSubMenu("GÉRER LES FAVORIS",favoriteMenu,loaded && !saved.empty());
        menu.addSeparator();
        menu.addSectionHeader("VARIANTES A/B");
        menu.addItem(5300,juce::String::fromUTF8("COMPARER A ET B : VUE GLOBALE"));
        menu.addItem(1100,"COPIER A VERS B");
        menu.addItem(1101,"COPIER B VERS A");
        menu.addItem(1102,juce::String::fromUTF8("ÉCHANGER A ET B"));
        menu.addSeparator();
        menu.addItem(1103,"RESTAURER A");
        menu.addItem(1104,"RESTAURER B");
        juce::PopupMenu sectionsAToB,sectionsBToA,resetSections,restoreSections,swapSections;
        juce::PopupMenu copyToClipboardSections,pasteFromClipboardSections;
        const juce::String sectionNames[4]={
            "INPUT","GATE","COMPRESSEUR",juce::String::fromUTF8("ÉGALISEUR")
        };
        for(int section=0;section<4;++section)
        {
            sectionsAToB.addItem(1200+section,sectionNames[section]);
            sectionsBToA.addItem(1210+section,sectionNames[section]);
            resetSections.addItem(1300+section,sectionNames[section]);
            restoreSections.addItem(1400+section,sectionNames[section]);
            swapSections.addItem(1500+section,sectionNames[section]);
            copyToClipboardSections.addItem(1600+section,sectionNames[section]);
            pasteFromClipboardSections.addItem(
                1610+section,sectionNames[section],
                LibertyChannelInspectorState::Session::canPasteSection(
                    static_cast<LibertyChannelInspectorState::Session::Section>(section)));
        }
        menu.addSeparator();
        menu.addSubMenu("COPIER UNE SECTION A VERS B",sectionsAToB);
        menu.addSubMenu("COPIER UNE SECTION B VERS A",sectionsBToA);
        menu.addSubMenu(juce::String::fromUTF8("RÉINITIALISER UNE SECTION"),
                        resetSections);
        menu.addSubMenu(juce::String::fromUTF8("RESTAURER UNE SECTION"),
                        restoreSections);
        menu.addSubMenu(juce::String::fromUTF8("ÉCHANGER UNE SECTION A/B"),
                        swapSections);
        menu.addSeparator();
        menu.addSubMenu("COPIER UNE SECTION DE LA PISTE",copyToClipboardSections);
        menu.addSubMenu("COLLER UNE SECTION SUR LA PISTE",pasteFromClipboardSections);
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(&presetsButton),
            [safe=juce::Component::SafePointer<LibertyChannelInspector>(this),
             saved](int result){
                if(safe==nullptr || result==0)return;
                if(result==1700){safe->restoreABEdit(false);return;}
                if(result==1701){safe->restoreABEdit(true);return;}
                if(result==5300){safe->showGlobalABComparison();return;}
                if(result>=1 && result<=LibertyChannelInspectorPresets::count)
                {
                    safe->loadPresetForEditing(
                        juce::String::fromUTF8(LibertyChannelInspectorPresets::name(result)),
                        LibertyChannelInspectorPresets::make(result));
                }
                else if(result>=100 && result<100+(int)saved.size())
                {
                    const auto& preset=saved[(size_t)(result-100)];
                    safe->loadPresetForEditing(preset.name,preset.settings);
                }
                else if(result==1000)
                    safe->askToSavePreset();
                else if(result==1001)
                    safe->choosePresetImport();
                else if(result==1002)
                    safe->choosePresetExport();
                else if(result==1100)
                    safe->copyVariantForEditing(0,1);
                else if(result==1101)
                    safe->copyVariantForEditing(1,0);
                else if(result==1102)
                    safe->swapVariantsForEditing();
                else if(result==1103)
                    safe->restoreVariantAtOpening(0);
                else if(result==1104)
                    safe->restoreVariantAtOpening(1);
                else if(result>=1200 && result<1204)
                    safe->copySectionForEditing(0,1,result-1200);
                else if(result>=1210 && result<1214)
                    safe->copySectionForEditing(1,0,result-1210);
                else if(result>=1300 && result<1304)
                    safe->resetSectionForEditing(result-1300);
                else if(result>=1400 && result<1404)
                    safe->restoreSectionForEditing(result-1400);
                else if(result>=1500 && result<1504)
                    safe->swapSectionForEditing(result-1500);
                else if(result>=1600 && result<1604)
                    safe->copySectionToClipboardForEditing(result-1600);
                else if(result>=1610 && result<1614)
                    safe->pasteSectionFromClipboardForEditing(result-1610);
                else if(result>=200 && result<200+(int)saved.size())
                    safe->askToRemovePreset(saved[(size_t)(result-200)].name);
                else if(result>=300 && result<300+(int)saved.size())
                    safe->askToModifyPresetName(saved[(size_t)(result-300)].name,false);
                else if(result>=400 && result<400+(int)saved.size())
                    safe->askToModifyPresetName(saved[(size_t)(result-400)].name,true);
                else if(result>=700 && result<700+(int)saved.size())
                {
                    const auto& preset=saved[(size_t)(result-700)];
                    if(preset.favorite)
                    {
                        safe->loadPresetForEditing(preset.name,preset.settings);
                    }
                }
                else if(result>=800 && result<800+(int)saved.size())
                {
                    const auto& preset=saved[(size_t)(result-800)];
                    safe->changePresetFavorite(preset.name,!preset.favorite);
                }
            });
    };
    cancelButton.onClick=[this]{session.cancel();closeDialog(0);};
    applyButton.onClick=[this]{
        session.setPending(readControls());
        session.apply();
        if(afterApply)afterApply();
        closeDialog(1);
    };
    setWantsKeyboardFocus(true);
    setSize(760,520);
    // Keep the paste state up to date when another CANAL dialog copies settings.
    startTimer(200);
 }
 ~LibertyChannelInspector()override{
     stopTimer();
     for(auto& slider:knobs)slider.setLookAndFeel(nullptr);
 }
 bool keyPressed(const juce::KeyPress& key)override
 {
     // Keep numeric text editing native; all other shortcuts stay in CANAL.
     const auto modifiers=key.getModifiers();
     const auto action=LibertyChannelABShortcut::resolve(
         key.getKeyCode(),modifiers.isCommandDown(),
         modifiers.isShiftDown(),modifiers.isAltDown(),
         isTextEditorFocused());
     using Action=LibertyChannelABShortcut::Action;
     if(action==Action::none)return false;
     if(action==Action::undo && abHistory.canUndo())restoreABEdit(false);
     if(action==Action::redo && abHistory.canRedo())restoreABEdit(true);
     if(action==Action::selectA)selectVariantForEditing(0);
     if(action==Action::selectB)selectVariantForEditing(1);
     // A bubbled Cmd+Z from a numeric editor must never mutate global or
     // A/B history. Direct bank selection never runs during text entry.
     return true;
 }
 void mouseDown(const juce::MouseEvent& event)override
 {
     const auto layout=LibertyChannelDialogLayout::make(getWidth(),getHeight());
     const auto position=event.getPosition();
     if(event.mods.isPopupMenu())
     {
         const int section=LibertyChannelDialogLayout::sectionTitleAt(
             layout,position.x,position.y);
         if(section>=0)showSectionContextMenu(section,position);
     }
     else if(event.mods.isLeftButtonDown())
     {
         const int section=LibertyChannelDialogLayout::abIndicatorAt(
             layout,position.x,position.y);
         if(section>=0)showSectionComparisonMenu(section,position);
     }
 }
 void mouseMove(const juce::MouseEvent& event)override
 {
     const auto layout=LibertyChannelDialogLayout::make(getWidth(),getHeight());
     const auto position=event.getPosition();
     setMouseCursor(LibertyChannelDialogLayout::abIndicatorAt(
                        layout,position.x,position.y)>=0
         ?juce::MouseCursor::PointingHandCursor
         :juce::MouseCursor::NormalCursor);
 }
 void mouseExit(const juce::MouseEvent&)override
 {
     setMouseCursor(juce::MouseCursor::NormalCursor);
 }
 void paint(juce::Graphics& g)override{
     g.fillAll(juce::Colour(0xff101b2a));
     g.setColour(juce::Colour(0xff263e56));
     g.fillRoundedRectangle(12.f,10.f,(float)getWidth()-24.f,60.f,8.f);
     g.setColour(juce::Colour(0xff63cfd8));
     g.fillRoundedRectangle(12.f,10.f,4.f,60.f,2.f);
     g.setColour(juce::Colours::white);
     g.setFont(juce::Font(20.f,juce::Font::bold));
     g.drawFittedText("LIBERTY  |  CANAL AUDIO",28,17,getWidth()-260,26,
                       juce::Justification::centredLeft,1);
     g.setFont(juce::Font(11.f));
     g.setColour(juce::Colour(0xffb5cbd9));
     g.drawFittedText(name+"  |  Traitements audio",29,52,getWidth()-58,16,
                      juce::Justification::centredLeft,1);
     const auto layout=LibertyChannelDialogLayout::make(getWidth(),getHeight());
     const juce::String headings[4]={"INPUT","GATE","COMPRESSEUR",juce::String::fromUTF8("ÉGALISEUR")};
     const juce::Colour accents[4]={juce::Colour(0xff72c6eb),juce::Colour(0xff91d1a8),
                                   juce::Colour(0xffe0b276),juce::Colour(0xff60d3d6)};
     // Read the displayed controls (including pending knob edits) without
     // changing the Session, variant selection or live audio processing.
     const auto visibleSettings=readControls();
     const auto* otherSettings=session.variantSettings(1-session.variantIndex());
     using Section=LibertyChannelInspectorState::Session::Section;
     // A/B highlights follow uncommitted knob edits; no session or DSP writes.
     std::array<bool,8> differingKnobs{};
     std::array<bool,3> differingPower{};
     for(int i=0;i<4;++i){
        const auto b=layout.sections[(size_t)i];
        g.setColour(juce::Colour(0xff16283a));
        g.fillRoundedRectangle((float)b.x,(float)b.y,(float)b.w,(float)b.h,7.f);
        g.setColour(juce::Colour(0xff49657b));
        g.drawRoundedRectangle((float)b.x,(float)b.y,(float)b.w,(float)b.h,7.f,1.f);
        g.setColour(accents[i]);
        g.fillRoundedRectangle((float)b.x+11,(float)b.y+13,3.f,22.f,1.5f);
        g.setFont(juce::Font(14.f,juce::Font::bold));
        g.drawText(headings[i],b.x+23,b.y+13,b.w-220,22,juce::Justification::centredLeft);
        const auto comparison=otherSettings!=nullptr
            ?LibertyChannelSectionComparison::rows(
                visibleSettings,*otherSettings,static_cast<Section>(i))
            :std::vector<LibertyChannelSectionComparison::Row>{};
        const int count=LibertyChannelSectionComparison::differenceCount(comparison);
        if(otherSettings!=nullptr)
        {
            // Row order matches ChannelSectionComparison::rows().
            if(i==0){differingKnobs[0]=comparison[0].different;
                     differingKnobs[1]=comparison[1].different;}
            else if(i==1){differingPower[0]=comparison[0].different;
                          differingKnobs[2]=comparison[1].different;}
            else if(i==2){differingPower[1]=comparison[0].different;
                          differingKnobs[3]=comparison[1].different;
                          differingKnobs[4]=comparison[2].different;}
            else {differingPower[2]=comparison[0].different;
                  differingKnobs[5]=comparison[1].different;
                  differingKnobs[6]=comparison[2].different;
                  differingKnobs[7]=comparison[3].different;}
        }
        const auto indicator=layout.abIndicators[(size_t)i];
        g.setColour(juce::Colour(count>0?0xff51432f:0xff24394a));
        g.fillRoundedRectangle((float)indicator.x,(float)indicator.y,
                               (float)indicator.w,(float)indicator.h,5.f);
        g.setColour(juce::Colour(count>0?0xffe0b276:0xff617f96));
        g.drawRoundedRectangle((float)indicator.x,(float)indicator.y,
                               (float)indicator.w,(float)indicator.h,5.f,1.f);
        g.setFont(juce::Font(10.f,juce::Font::bold));
        g.setColour(juce::Colour(count>0?0xffffdfa5:0xffbacbd8));
        g.drawText("A/B "+juce::String(count),
                   indicator.x,indicator.y,indicator.w,indicator.h,
                   juce::Justification::centred);
     }
     const juce::String labels[8]={"GAIN","COUPE-BAS","SEUIL","SEUIL","RATIO",
                            "GRAVES",juce::String::fromUTF8("MÉDIUMS"),"AIGUS"};
     // Amber outlines identify changed enable switches without covering buttons.
     for(int i=0;i<3;++i)
     {
         if(!differingPower[(size_t)i])continue;
         const auto p=layout.powers[(size_t)i];
         g.setColour(juce::Colour(0xfff3c77a));
         g.drawRoundedRectangle((float)p.x-2.f,(float)p.y-2.f,
                                (float)p.w+4.f,(float)p.h+4.f,5.f,2.f);
     }
     g.setFont(juce::Font(10.f,juce::Font::bold));
     for(int i=0;i<8;++i){
         const auto b=layout.controls[(size_t)i];
         if(differingKnobs[(size_t)i])
         {
             g.setColour(juce::Colour(0xff493b2e));
             g.fillRoundedRectangle((float)b.x+9.f,(float)b.y-20.f,
                                    (float)b.w-18.f,18.f,4.f);
         }
         g.setColour(juce::Colour(differingKnobs[(size_t)i]
                                    ?0xffffdfa5:0xffcedde7));
         g.drawText(labels[i],b.x,b.y-18,b.w,14,juce::Justification::centred);
     }
     // Single-line footer remains above the action buttons (no overlap).
     g.setColour(juce::Colour(0xff6ed0d7));
     g.setFont(juce::Font(10.f,juce::Font::bold));
     g.drawFittedText(
         presetIndicator.description(session.variantIndex(),readControls()),
         20,getHeight()-87,462,19,juce::Justification::centredLeft,1);
     g.setColour(juce::Colour(0xff91aabd));
     g.setFont(juce::Font(10.f));
     g.drawFittedText(juce::String::fromUTF8("ANNULER ignore. APPLIQUER valide."),
         499,getHeight()-87,getWidth()-519,19,
         juce::Justification::centredRight,1);
 }
 void resized()override{
    const auto layout=LibertyChannelDialogLayout::make(getWidth(),getHeight());
    for(int i=0;i<8;++i){
        const auto b=layout.controls[(size_t)i];
        knobs[(size_t)i].setBounds(b.x,b.y,b.w,b.h);
    }
    for(int i=0;i<3;++i){
        const auto b=layout.powers[(size_t)i];
        power[(size_t)i].setBounds(b.x,b.y,b.w,b.h);
    }
    juce::TextButton* buttons[]={&resetButton,&cancelButton,&applyButton};
    for(int i=0;i<3;++i){
        const auto b=layout.actions[(size_t)i];
        buttons[i]->setBounds(b.x,b.y,b.w,b.h);
    }
    juce::TextButton* clipButtons[]={&copyButton,&pasteButton};
    for(int i=0;i<2;++i){
        const auto b=layout.clipboard[(size_t)i];
        clipButtons[i]->setBounds(b.x,b.y,b.w,b.h);
    }
    const auto variantBounds=layout.variant;
    variantButton.setBounds(variantBounds.x,variantBounds.y,variantBounds.w,variantBounds.h);
    const auto presetBounds=layout.presets;
    presetsButton.setBounds(presetBounds.x,presetBounds.y,presetBounds.w,presetBounds.h);
    const auto undoBounds=layout.historyActions[0];
    const auto redoBounds=layout.historyActions[1];
    undoButton.setBounds(undoBounds.x,undoBounds.y,undoBounds.w,undoBounds.h);
    redoButton.setBounds(redoBounds.x,redoBounds.y,redoBounds.w,redoBounds.h);
 }
private:
 bool isTextEditorFocused()const
 {
     // Numeric editors are descendants of the eight CANAL sliders.
     // Use Component identity/hierarchy, without RTTI: compatible with
     // JUCE builds where C++ run-time type information is disabled.
     const auto* focused=juce::Component::getCurrentlyFocusedComponent();
     if(focused==nullptr)return false;
     for(const auto& knob:knobs)
         if(&knob!=focused && knob.isParentOf(focused))
             return true;
     return false;
 }
 struct HistoryFrame
 {
     LibertyChannelInspectorState::Session::Snapshot state;
     LibertyChannelPresetStatus::Indicator labels;
 };
 HistoryFrame captureHistoryFrame() const
 {
     return {session.snapshot(),presetIndicator};
 }
 // Select one draft without touching live DSP. Re-selecting the active
 // bank is a true no-op: it must not add an Undo step or clear Redo.
 void selectVariantForEditing(int target)
 {
     if(!LibertyChannelABShortcut::selectBankWouldChange(
            session.variantIndex(),target))return;
     rememberABEdit();
     session.switchVariant();
     writeControls(session.pending());
     refreshVariantButton();
     repaint();
 }
 // Capture current screen edits before an A/B command mutates either bank.
 void rememberABEdit()
 {
     session.setPending(readControls());
     abHistory.record(captureHistoryFrame());
     refreshHistoryButtons();
 }
 // One undo step per knob drag; text and keyboard changes are separate steps.
 void trackControlChange(int index)
 {
     if(gestureHistory.shouldRecord(index))
         abHistory.record(captureHistoryFrame());
     session.setPending(readControls());
     refreshHistoryButtons();
     repaint();
 }
 void restoreABEdit(bool redo)
 {
     // This history is local to CANAL; never call Session::apply() here.
     auto frame=captureHistoryFrame();
     if(!(redo?abHistory.redo(frame):abHistory.undo(frame)))return;
     if(!session.restoreSnapshot(frame.state))return;
     presetIndicator=frame.labels;
     writeControls(session.pending());
     refreshVariantButton();
     refreshHistoryButtons();
     repaint();
 }
 void loadPresetSectionForEditing(
     const AudioEngine::ChannelProcessingSettings& preset,int section)
 {
    if(section<0 || section>=4)return;
    rememberABEdit();
    using Section=LibertyChannelInspectorState::Session::Section;
    if(!session.loadPresetSection(preset,static_cast<Section>(section)))
        return;
    writeControls(session.pending());
    // Keep the target bank's label: MODIFIÉ reflects selective changes.
    repaint();
 }
 LibertyChannelABOverview::Counts globalABDifferences()const
 {
     LibertyChannelABOverview::Counts summary;
     const auto* storedA=session.variantSettings(0);
     const auto* storedB=session.variantSettings(1);
     if(storedA==nullptr || storedB==nullptr)return summary;
     // Include every uncommitted value visible in the current bank.
     const auto displayed=readControls();
     const auto& a=session.variantIndex()==0?displayed:*storedA;
     const auto& b=session.variantIndex()==1?displayed:*storedB;
     for(int section=0;section<LibertyChannelABOverview::sectionCount;++section)
     {
         summary.bySection[(size_t)section]=
             LibertyChannelSectionComparison::differenceCount(
                 LibertyChannelSectionComparison::rows(a,b,
                     static_cast<LibertyChannelInspectorState::Session::Section>(section)));
     }
     return summary;
 }
 void confirmWholeVariantCopy(int from,int to)
 {
     if(from<0 || from>=2 || to<0 || to>=2 || from==to)return;
     const auto summary=globalABDifferences();
     if(summary.identical())return;
     const juce::String names[4]={
         "INPUT","GATE","COMPRESSEUR",juce::String::fromUTF8("ÉGALISEUR")
     };
     juce::String message=juce::String::fromUTF8("La variante ")
         +(to==0?"A":"B")
         +juce::String::fromUTF8(" sera remplacée par les réglages et le préréglage de ")
         +(from==0?"A":"B")+juce::String::fromUTF8(".\n\n");
     message+=juce::String(summary.total())
         +juce::String::fromUTF8(" paramètres différents dans ")
         +juce::String(summary.affectedSections())
         +juce::String::fromUTF8(" section(s) :\n");
     for(int section=0;section<LibertyChannelABOverview::sectionCount;++section)
         if(summary.canCopySection(section))
             message+=names[section]+": "
                 +juce::String(summary.bySection[(size_t)section])
                 +juce::String::fromUTF8(" différence(s)\n");
     message+=juce::String::fromUTF8(
         "\nCette opération sera annulable en une étape. "
         "Le son ne changera pas avant APPLIQUER.");
     juce::AlertWindow::showOkCancelBox(
         juce::AlertWindow::WarningIcon,
         "LIBERTY  |  CANAL",
         message,"CONFIRMER","ANNULER",nullptr,
         juce::ModalCallbackFunction::create(
             [safe=juce::Component::SafePointer<LibertyChannelInspector>(this),
              from,to](int accepted)
             {
                 if(safe==nullptr)return;
                 // A draft may have changed while confirmation was open.
                 // Neither cancellation nor identical drafts add history.
                 if(!LibertyChannelABOverview::commitConfirmedWholeCopy(
                        accepted,safe->globalABDifferences()))return;
                 safe->copyVariantForEditing(from,to);
             }));
 }
 void showGlobalABComparison()
 {
     const auto summary=globalABDifferences();
     juce::PopupMenu menu;
     menu.addSectionHeader(
         juce::String::fromUTF8("A/B : ")+juce::String(summary.total())
             +juce::String::fromUTF8(" PARAMÈTRES DIFFÉRENTS"));
     menu.addItem(LibertyChannelABOverview::chooseA,"MODE A  |  Cmd+1",
                  true,session.variantIndex()==0);
     menu.addItem(LibertyChannelABOverview::chooseB,"MODE B  |  Cmd+2",
                  true,session.variantIndex()==1);
     menu.addSeparator();
     const juce::String names[4]={
         "INPUT","GATE","COMPRESSEUR",juce::String::fromUTF8("ÉGALISEUR")
     };
     for(int section=0;section<LibertyChannelABOverview::sectionCount;++section)
     {
         const int count=summary.bySection[(size_t)section];
         juce::PopupMenu operations;
         operations.addItem(LibertyChannelABOverview::openSectionBase+section,
             juce::String::fromUTF8("VOIR LES PARAMÈTRES A/B"));
         operations.addSeparator();
         operations.addItem(LibertyChannelABOverview::copySectionAToBBase+section,
             "COPIER LA SECTION A VERS B",summary.canCopySection(section));
         operations.addItem(LibertyChannelABOverview::copySectionBToABase+section,
             "COPIER LA SECTION B VERS A",summary.canCopySection(section));
         menu.addSubMenu(names[section]+"  |  "+juce::String(count)
                +(count==1?juce::String::fromUTF8(" DIFFÉRENCE")
                          :juce::String::fromUTF8(" DIFFÉRENCES")),
             operations);
     }
     menu.addSeparator();
     menu.addSectionHeader(juce::String::fromUTF8("ALIGNER TOUTES LES SECTIONS"));
     menu.addItem(LibertyChannelABOverview::copyAllAToB,
         juce::String::fromUTF8("COPIER A VERS B — RÉGLAGES ET PRÉRÉGLAGE"),
         !summary.identical());
     menu.addItem(LibertyChannelABOverview::copyAllBToA,
         juce::String::fromUTF8("COPIER B VERS A — RÉGLAGES ET PRÉRÉGLAGE"),
         !summary.identical());
     // No operation touches the real-time DSP before APPLIQUER.
     // Each copy reuses the current reversible CANAL A/B session.
     menu.showMenuAsync(
         juce::PopupMenu::Options().withTargetComponent(&variantButton),
         [safe=juce::Component::SafePointer<LibertyChannelInspector>(this)](int chosen)
         {
             if(safe==nullptr || chosen==0)return;
             if(chosen==LibertyChannelABOverview::chooseA)
             {
                 safe->selectVariantForEditing(0);
                 return;
             }
             if(chosen==LibertyChannelABOverview::chooseB)
             {
                 safe->selectVariantForEditing(1);
                 return;
             }
             const auto command=LibertyChannelABOverview::decodeCommand(chosen);
             using Kind=LibertyChannelABOverview::CommandKind;
             if(command.kind==Kind::none)return;
             // The popup snapshot may be stale if an external input changes
             // the pending controls; check live differences before copying.
             const auto differences=safe->globalABDifferences();
             if(command.kind==Kind::details)
             {
                 const auto bounds=safe->variantButton.getBounds();
                 safe->showSectionComparisonMenu(
                     command.section,{bounds.getCentreX(),bounds.getY()});
             }
             else if(command.kind==Kind::sectionAToB
                  && differences.canCopySection(command.section))
                 safe->copySectionForEditing(0,1,command.section);
             else if(command.kind==Kind::sectionBToA
                  && differences.canCopySection(command.section))
                 safe->copySectionForEditing(1,0,command.section);
             else if(LibertyChannelABOverview::needsFullCopyConfirmation(
                         command.kind) && !differences.identical())
                 safe->confirmWholeVariantCopy(
                     command.kind==Kind::allAToB?0:1,
                     command.kind==Kind::allAToB?1:0);
         });
 }
 juce::PopupMenu makeSectionComparisonMenu(int section)
 {
    juce::PopupMenu comparison;
    if(section<0 || section>=4)return comparison;
    const auto* storedA=session.variantSettings(0);
    const auto* storedB=session.variantSettings(1);
    if(storedA==nullptr || storedB==nullptr)return comparison;
    // One bank can have knob edits that have not yet been copied to Session.
    // Read them locally, keeping this operation strictly read-only.
    const auto displayed=readControls();
    const auto& a=session.variantIndex()==0?displayed:*storedA;
    const auto& b=session.variantIndex()==1?displayed:*storedB;
    const auto detail=LibertyChannelSectionComparison::rows(
        a,b,static_cast<LibertyChannelInspectorState::Session::Section>(section));
    comparison.addSectionHeader(
        juce::String::fromUTF8("PARAMÈTRES DIFFÉRENTS : ")
        +juce::String(LibertyChannelSectionComparison::differenceCount(detail)));
    for(size_t i=0;i<detail.size();++i)
    {
        const auto& row=detail[i];
        const auto state=row.different
            ?juce::String::fromUTF8("DIFFÉRENT : ")
            :juce::String::fromUTF8("IDENTIQUE : ");
        const auto label=state+row.label+"  |  A : "+row.valueA
            +"  |  B : "+row.valueB;
        if(row.different)
        {
            juce::PopupMenu directions;
            directions.addItem(4000+2*(int)i,"COPIER A VERS B");
            directions.addItem(4001+2*(int)i,"COPIER B VERS A");
            comparison.addSubMenu(label,directions);
        }
        else comparison.addItem(3000+(int)i,label,false);
    }
    comparison.addSeparator();
    comparison.addItem(1700,juce::String::fromUTF8("ANNULER MODIFICATION A/B"),abHistory.canUndo());
    comparison.addItem(1701,juce::String::fromUTF8("RÉTABLIR MODIFICATION A/B"),abHistory.canRedo());
    return comparison;
 }
 void showSectionComparisonMenu(int section,juce::Point<int> localPosition)
 {
    if(section<0 || section>=4)return;
    const auto screen=localPointToGlobal(localPosition);
    makeSectionComparisonMenu(section).showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea(
            juce::Rectangle<int>(screen.x,screen.y,1,1)),
        [safe=juce::Component::SafePointer<LibertyChannelInspector>(this),
         section](int result)
        {
            if(safe==nullptr)return;
            if(result==1700)safe->restoreABEdit(false);
            else if(result==1701)safe->restoreABEdit(true);
            else safe->applyComparisonCopy(section,result);
        });
 }
 void applyComparisonCopy(int section,int result)
 {
     if(section<0 || section>=4 || result<4000 || result>=4008)return;
     const int chosen=result-4000;
     constexpr int rowCounts[4]={2,2,3,4};
     if(chosen/2>=rowCounts[section])return;
     // Save the entire draft before the targeted copy.
     rememberABEdit();
     using Section=LibertyChannelInspectorState::Session::Section;
     const int from=(chosen%2)==0?0:1;
     const int to=1-from;
     if(!session.copyParameter(from,to,static_cast<Section>(section),chosen/2))
         return;
     // Only refresh visible controls if the destination is the active bank.
     // No engine changes occur until the existing APPLIQUER button is pressed.
     if(session.variantIndex()==to)writeControls(session.pending());
     repaint();
 }
 void showSectionContextMenu(int section,juce::Point<int> localPosition)
 {
    if(section<0 || section>=4)return;
    using Section=LibertyChannelInspectorState::Session::Section;
    const juce::String names[4]={
        "INPUT","GATE","COMPRESSEUR",juce::String::fromUTF8("ÉGALISEUR")
    };
    juce::PopupMenu menu;
    menu.addSectionHeader(names[section]);
    menu.addItem(1700,juce::String::fromUTF8("ANNULER MODIFICATION A/B"),abHistory.canUndo());
    menu.addItem(1701,juce::String::fromUTF8("RÉTABLIR MODIFICATION A/B"),abHistory.canRedo());
    menu.addSeparator();
    menu.addItem(1,juce::String::fromUTF8("COPIER LA SECTION"));
    menu.addItem(2,juce::String::fromUTF8("COLLER LA SECTION"),
                 LibertyChannelInspectorState::Session::canPasteSection(
                     static_cast<Section>(section)));
    menu.addSeparator();
    menu.addItem(3,"COPIER A VERS B");
    menu.addItem(4,"COPIER B VERS A");
    menu.addItem(5,juce::String::fromUTF8("ÉCHANGER A ET B"));
    menu.addSeparator();
    menu.addItem(6,juce::String::fromUTF8("RÉINITIALISER LA SECTION"));
    menu.addItem(7,juce::String::fromUTF8("RESTAURER LA SECTION"));
    menu.addSeparator();
    juce::PopupMenu factoryPresets,personalPresets,presetSections;
    for(int id=1;id<=LibertyChannelInspectorPresets::count;++id)
        factoryPresets.addItem(2000+id,
            juce::String::fromUTF8(LibertyChannelInspectorPresets::name(id)));
    LibertyChannelUserPresets::Library library;
    const bool loaded=library.load(LibertyChannelUserPresets::Library::defaultFile());
    const auto saved=library.items();
    if(loaded)
        for(size_t i=0;i<saved.size();++i)
            personalPresets.addItem(2100+(int)i,saved[i].name);
    presetSections.addSubMenu(juce::String::fromUTF8("PRÉRÉGLAGES D'USINE"),
                             factoryPresets);
    presetSections.addSubMenu(juce::String::fromUTF8("MES PRÉRÉGLAGES"),
                             personalPresets,loaded && !saved.empty());
    menu.addSubMenu(juce::String::fromUTF8("CHARGER CETTE SECTION DEPUIS..."),
                    presetSections);
    menu.addSubMenu("COMPARER A ET B",makeSectionComparisonMenu(section));
    const auto screen=localPointToGlobal(localPosition);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea(
            juce::Rectangle<int>(screen.x,screen.y,1,1)),
        [safe=juce::Component::SafePointer<LibertyChannelInspector>(this),
         section,saved](int result)
        {
            if(safe==nullptr)return;
            if(result==1700){safe->restoreABEdit(false);return;}
            if(result==1701){safe->restoreABEdit(true);return;}
            if(result>=4000 && result<4008)
            {
                safe->applyComparisonCopy(section,result);
                return;
            }
            if(result>=2001 && result<=2000+LibertyChannelInspectorPresets::count)
            {
                safe->loadPresetSectionForEditing(
                    LibertyChannelInspectorPresets::make(result-2000),section);
                return;
            }
            if(result>=2100 && result<2100+(int)saved.size())
            {
                safe->loadPresetSectionForEditing(
                    saved[(size_t)(result-2100)].settings,section);
                return;
            }
            switch(result)
            {
                case 1: safe->copySectionToClipboardForEditing(section);break;
                case 2: safe->pasteSectionFromClipboardForEditing(section);break;
                case 3: safe->copySectionForEditing(0,1,section);break;
                case 4: safe->copySectionForEditing(1,0,section);break;
                case 5: safe->swapSectionForEditing(section);break;
                case 6: safe->resetSectionForEditing(section);break;
                case 7: safe->restoreSectionForEditing(section);break;
                default: break;
            }
        });
 }
 void copySectionToClipboardForEditing(int section)
 {
    session.setPending(readControls());
    using Section=LibertyChannelInspectorState::Session::Section;
    if(section<0 || section>3)return;
    session.copySectionToClipboard(static_cast<Section>(section));
 }
 void pasteSectionFromClipboardForEditing(int section)
 {
    // Keep the current section's unrelated settings and the inactive bank.
    using Section=LibertyChannelInspectorState::Session::Section;
    if(section<0 || section>3
       || !LibertyChannelInspectorState::Session::canPasteSection(static_cast<Section>(section)))
        return;
    rememberABEdit();
    if(!session.pasteSectionFromClipboard(static_cast<Section>(section)))
        return;
    writeControls(session.pending());
    // Preserve destination preset reference; MODIFIÉ flags the new values.
    repaint();
 }
 void swapSectionForEditing(int section)
 {
    // The active bank may contain unpublished edits from its controls.
    if(section<0 || section>3)return;
    rememberABEdit();
    using Section=LibertyChannelInspectorState::Session::Section;
    if(!session.swapSection(static_cast<Section>(section)))
        return;
    // Preset labels remain attached to each bank; their existing MODIFIÉ
    // indicator reflects the selective exchange automatically.
    writeControls(session.pending());
    repaint();
 }
 void restoreSectionForEditing(int section)
 {
    // Save any unpublished knob edits before restoring a single section.
    if(section<0 || section>3)return;
    rememberABEdit();
    using Section=LibertyChannelInspectorState::Session::Section;
    if(!session.restoreOpeningSection(static_cast<Section>(section)))
        return;
    writeControls(session.pending());
    // Keep its preset label: the existing MODIFIÉ indicator compares values.
    repaint();
 }
 void resetSectionForEditing(int section)
 {
    if(section<0 || section>3)return;
    rememberABEdit();
    using Section=LibertyChannelInspectorState::Session::Section;
    if(!session.resetSection(static_cast<Section>(section)))
        return;
    writeControls(session.pending());
    // Keep the loaded preset reference: MODIFIÉ reflects neutralised controls.
    repaint();
 }
 void copySectionForEditing(int from,int to,int section)
 {
    // Commit current knob/toggle edits to the draft before copying.
    if(section<0 || section>3 || from<0 || from>=2 || to<0 || to>=2 || from==to)
        return;
    rememberABEdit();
    using Section=LibertyChannelInspectorState::Session::Section;
    if(!session.copySection(from,to,static_cast<Section>(section)))
        return;
    // A partial mix remains associated with the target preset; the existing
    // MODIFIÉ indicator tells the user when its values have changed.
    if(session.variantIndex()==to)
        writeControls(session.pending());
    repaint();
 }
 void restoreVariantAtOpening(int bank)
 {
    // Capture edits in the selected bank, leaving the other bank intact.
    if(bank<0 || bank>=2)return;
    rememberABEdit();
    if(!session.restoreOpeningVariant(bank))return;
    presetIndicator.restoreBankFrom(openingPresetIndicator,bank);
    if(session.variantIndex()==bank)
        writeControls(session.pending());
    repaint();
 }
 void swapVariantsForEditing()
 {
    // Include the values most recently edited with the mouse.
    rememberABEdit();
    session.swapVariants();
    presetIndicator.swapBanks();
    writeControls(session.pending());
    repaint();
 }
 void copyVariantForEditing(int from,int to)
 {
    // A knob edit may not have been published to the pending bank yet.
    if(from<0 || from>=2 || to<0 || to>=2 || from==to)return;
    rememberABEdit();
    if(!session.copyVariant(from,to))return;
    presetIndicator.copyBank(from,to);
    if(session.variantIndex()==to)
        writeControls(session.pending());
    repaint();
 }
 void loadPresetForEditing(const juce::String& label,
                           const AudioEngine::ChannelProcessingSettings& settings)
 {
    rememberABEdit();
    session.setPending(settings);
    writeControls(session.pending());
    // A loaded preset is the reference point; later knob/toggle edits mark
    // it MODIFIÉ without applying any changes to the live channel.
    presetIndicator.select(session.variantIndex(),label,readControls());
    repaint();
 }
 void restoreMatchingPresetLabels()
 {
    const auto state=engine.getChannelABState(trackIndex);
    LibertyChannelUserPresets::Library library;
    const bool loaded=library.load(LibertyChannelUserPresets::Library::defaultFile());
    for(int bank=0;bank<2;++bank)
    {
        const auto& settings=state.variants[(size_t)bank];
        // Prefer user names when a personalized preset shares factory values.
        bool found=false;
        if(loaded)
            for(const auto& preset:library.items())
                if(LibertyChannelPresetStatus::sameSettings(preset.settings,settings))
                {
                    presetIndicator.select(bank,preset.name,settings);
                    found=true;
                    break;
                }
        if(found)continue;
        for(int id=1;id<=LibertyChannelInspectorPresets::count;++id)
            if(LibertyChannelPresetStatus::sameSettings(
                LibertyChannelInspectorPresets::make(id),settings))
            {
                presetIndicator.select(bank,
                    juce::String::fromUTF8(LibertyChannelInspectorPresets::name(id)),
                    settings);
                break;
            }
    }
 }
 void changePresetFavorite(const juce::String& presetName,bool favorite)
 {
    LibertyChannelUserPresets::Library library;
    const auto path=LibertyChannelUserPresets::Library::defaultFile();
    if(!library.load(path) || !library.setFavorite(presetName,favorite)
       || !library.save(path))
        reportPresetError(juce::String::fromUTF8(
            "Impossible de modifier les favoris CANAL. Vérifiez le dossier Liberty."));
 }
 void choosePresetImport()
 {
    const auto folder=juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    presetFileChooser=std::make_unique<juce::FileChooser>(
        juce::String::fromUTF8("Importer des préréglages CANAL"),folder,"*.xml",true);
    presetFileChooser->launchAsync(
        juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,
        [safe=juce::Component::SafePointer<LibertyChannelInspector>(this)]
        (const juce::FileChooser& chooser)
        {
            if(safe==nullptr)return;
            const auto selected=chooser.getResult();
            if(!selected.existsAsFile())return;
            LibertyChannelUserPresets::Library incoming,existing;
            const auto destination=LibertyChannelUserPresets::Library::defaultFile();
            if(!incoming.load(selected) || !existing.load(destination)
               || !existing.mergeFrom(incoming) || !existing.save(destination))
                safe->reportPresetError(juce::String::fromUTF8(
                    "Importation impossible : fichier invalide, accès refusé ou limite de 24 préréglages dépassée. "
                    "Les préréglages existants sont conservés."));
        });
 }
 void choosePresetExport()
 {
    const auto folder=juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    presetFileChooser=std::make_unique<juce::FileChooser>(
        juce::String::fromUTF8("Exporter des préréglages CANAL"),
        folder.getChildFile("Liberty-CANAL-Presets.xml"),"*.xml",true);
    presetFileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles
            |juce::FileBrowserComponent::warnAboutOverwriting,
        [safe=juce::Component::SafePointer<LibertyChannelInspector>(this)]
        (const juce::FileChooser& chooser)
        {
            if(safe==nullptr)return;
            auto destination=chooser.getResult();
            if(destination==juce::File{})return;
            if(!destination.hasFileExtension("xml"))
                destination=destination.withFileExtension("xml");
            const auto source=LibertyChannelUserPresets::Library::defaultFile();
            if(destination==source)
            {
                safe->reportPresetError(juce::String::fromUTF8(
                    "Choisissez un fichier différent de la bibliothèque Liberty active."));
                return;
            }
            LibertyChannelUserPresets::Library library;
            if(!library.load(source) || !library.save(destination))
                safe->reportPresetError(juce::String::fromUTF8(
                    "Impossible d'exporter les préréglages CANAL vers ce fichier."));
        });
 }
 void reportPresetError(const juce::String& message)
 {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon, "LIBERTY  |  CANAL", message, "OK");
 }
 void askToSavePreset()
 {
    auto* dialog=new juce::AlertWindow("LIBERTY  |  CANAL",
        juce::String::fromUTF8("Nom du préréglage personnel (48 caractères maximum). Un nom existant sera remplacé."),
        juce::AlertWindow::QuestionIcon);
    dialog->setColour(juce::AlertWindow::backgroundColourId,juce::Colour(0xff142438));
    dialog->setColour(juce::AlertWindow::textColourId,juce::Colours::white);
    dialog->addTextEditor("presetName",juce::String::fromUTF8("Mon préréglage"),
                          juce::String::fromUTF8("Nom"),false);
    dialog->addButton("ENREGISTRER",1,juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("ANNULER",0,juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->enterModalState(true,
        juce::ModalCallbackFunction::create(
            [safe=juce::Component::SafePointer<LibertyChannelInspector>(this),dialog](int result){
                const juce::String presetName=dialog->getTextEditorContents("presetName");
                delete dialog;
                if(safe==nullptr || result!=1)return;
                auto library=LibertyChannelUserPresets::Library{};
                const auto file=LibertyChannelUserPresets::Library::defaultFile();
                if(!library.load(file)
                   || !library.upsert(presetName,safe->readControls())
                   || !library.save(file))
                    safe->reportPresetError(juce::String::fromUTF8(
                        "Impossible d'enregistrer ce préréglage. Vérifiez son nom ou le dossier Liberty."));
            }),false);
 }
 void askToModifyPresetName(juce::String oldName,bool duplicate)
 {
    const juce::String suggested=duplicate
        ? (oldName+juce::String::fromUTF8(" - copie")).substring(0,48)
        : oldName;
    auto* dialog=new juce::AlertWindow("LIBERTY  |  CANAL",
        duplicate
            ? juce::String::fromUTF8("Dupliquer ce préréglage sous un nouveau nom (48 caractères maximum).")
            : juce::String::fromUTF8("Renommer ce préréglage (48 caractères maximum)."),
        juce::AlertWindow::QuestionIcon);
    dialog->setColour(juce::AlertWindow::backgroundColourId,juce::Colour(0xff142438));
    dialog->setColour(juce::AlertWindow::textColourId,juce::Colours::white);
    dialog->addTextEditor("newPresetName",suggested,
                           juce::String::fromUTF8("Nouveau nom"),false);
    dialog->addButton(duplicate?"DUPLIQUER":"RENOMMER",1,
                      juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("ANNULER",0,juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->enterModalState(true,
        juce::ModalCallbackFunction::create(
            [safe=juce::Component::SafePointer<LibertyChannelInspector>(this),
             dialog,oldName,duplicate](int result){
                const auto newName=dialog->getTextEditorContents("newPresetName");
                delete dialog;
                if(safe==nullptr || result!=1)return;
                LibertyChannelUserPresets::Library library;
                const auto file=LibertyChannelUserPresets::Library::defaultFile();
                const bool loaded=library.load(file);
                const bool changed=loaded && (duplicate
                    ? library.duplicate(oldName,newName)
                    : library.rename(oldName,newName));
                if(!changed || !library.save(file))
                    safe->reportPresetError(juce::String::fromUTF8(
                        "Modification impossible : nom invalide, déjà utilisé ou nombre maximal de préréglages atteint."));
            }),false);
 }
 void askToRemovePreset(juce::String presetName)
 {
    juce::AlertWindow::showOkCancelBox(juce::AlertWindow::QuestionIcon,
        "LIBERTY  |  CANAL",
        juce::String::fromUTF8("Supprimer le préréglage personnel : ") + presetName + " ?",
        "SUPPRIMER","ANNULER",nullptr,
        juce::ModalCallbackFunction::create(
            [safe=juce::Component::SafePointer<LibertyChannelInspector>(this),presetName](int result){
                if(safe==nullptr || result!=1)return;
                auto library=LibertyChannelUserPresets::Library{};
                const auto file=LibertyChannelUserPresets::Library::defaultFile();
                if(!library.load(file) || !library.erase(presetName) || !library.save(file))
                    safe->reportPresetError(juce::String::fromUTF8(
                        "Impossible de supprimer ce préréglage personnel."));
            }));
 }
 void refreshHistoryButtons()
 {
     const auto undoCount=abHistory.undoDepth();
     const auto redoCount=abHistory.redoDepth();
     if(undoButton.isEnabled()!=(undoCount>0))undoButton.setEnabled(undoCount>0);
     if(redoButton.isEnabled()!=(redoCount>0))redoButton.setEnabled(redoCount>0);
     // Tooltips disclose how many edits can be reversed without changing
     // the header geometry or using placeholder glyphs for French labels.
     if(undoCount!=shownUndoDepth)
     {
         undoButton.setTooltip(undoCount>0
             ? juce::String::fromUTF8("ANNULER A/B : ")+juce::String((int)undoCount)
                 +juce::String::fromUTF8(" étape(s) disponibles — Cmd+Z")
             : juce::String::fromUTF8("Aucune modification A/B à annuler."));
         shownUndoDepth=undoCount;
     }
     if(redoCount!=shownRedoDepth)
     {
         redoButton.setTooltip(redoCount>0
             ? juce::String::fromUTF8("RÉTABLIR A/B : ")+juce::String((int)redoCount)
                 +juce::String::fromUTF8(" étape(s) disponibles — Cmd+Maj+Z")
             : juce::String::fromUTF8("Aucune modification A/B à rétablir."));
         shownRedoDepth=redoCount;
     }
 }
 void refreshVariantButton(){
    const bool isB=session.variantIndex()==1;
    variantButton.setButtonText(isB?"MODE B":"MODE A");
    variantButton.setColour(juce::TextButton::buttonColourId,
                            isB?juce::Colour(0xff256e6c):juce::Colour(0xff28546a));
 }
 void timerCallback()override{
    const bool available=LibertyChannelInspectorState::Session::canPaste();
    if(pasteButton.isEnabled()!=available)pasteButton.setEnabled(available);
    refreshHistoryButtons();
 }
 AudioEngine::ChannelProcessingSettings readControls()const{
    auto v=session.pending();
    v.inputGainDb=(float)knobs[0].getValue();
    v.highPassHz=(float)knobs[1].getValue();
    v.gateThresholdDb=(float)knobs[2].getValue();
    v.compressorThresholdDb=(float)knobs[3].getValue();
    v.compressorRatio=(float)knobs[4].getValue();
    v.eqLowDb=(float)knobs[5].getValue();
    v.eqMidDb=(float)knobs[6].getValue();
    v.eqHighDb=(float)knobs[7].getValue();
    v.gateEnabled=power[0].getToggleState();
    v.compressorEnabled=power[1].getToggleState();
    v.eqEnabled=power[2].getToggleState();
    return v;
 }
 void writeControls(const AudioEngine::ChannelProcessingSettings& v){
    const double values[8]={v.inputGainDb,v.highPassHz,v.gateThresholdDb,
                            v.compressorThresholdDb,v.compressorRatio,
                            v.eqLowDb,v.eqMidDb,v.eqHighDb};
    for(int i=0;i<8;++i)knobs[(size_t)i].setValue(values[i],juce::dontSendNotification);
    const bool enabled[3]={v.gateEnabled,v.compressorEnabled,v.eqEnabled};
    for(int i=0;i<3;++i){
        power[(size_t)i].setToggleState(enabled[i],juce::dontSendNotification);
        refreshPower(i);
    }
 }
 void configure(juce::Slider& slider,double minimum,double maximum,double step,
                double value,const juce::String& suffix,juce::Colour accent){
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setRange(minimum,maximum,step);
    slider.setValue(value,juce::dontSendNotification);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,86,21);
    slider.setNumDecimalPlacesToDisplay(step>=1?0:1);
    slider.setTextValueSuffix(suffix);
    slider.setDoubleClickReturnValue(true,minimum<0&&maximum>0?0.0:
        (minimum==1?1.0:minimum));
    slider.setColour(juce::Slider::rotarySliderFillColourId,accent);
    slider.setColour(juce::Slider::rotarySliderOutlineColourId,juce::Colour(0xff435d6e));
    slider.setColour(juce::Slider::textBoxTextColourId,juce::Colour(0xffeef5fa));
    slider.setColour(juce::Slider::textBoxBackgroundColourId,juce::Colour(0xff0c1b29));
    slider.setColour(juce::Slider::textBoxOutlineColourId,juce::Colour(0xff3f667d));
    slider.setLookAndFeel(&metallicStyle);
    addAndMakeVisible(slider);
 }
 void refreshPower(int index){
    auto& button=power[(size_t)index];
    const bool on=button.getToggleState();
    button.setButtonText(on?"ACTIF":"INACTIF");
    button.setColour(juce::TextButton::buttonColourId,
         on?juce::Colour(0xff267164):juce::Colour(0xff324458));
    button.setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xff267164));
    button.setColour(juce::TextButton::textColourOffId,juce::Colours::white);
    button.setColour(juce::TextButton::textColourOnId,juce::Colours::white);
 }
 void closeDialog(int result){
    if(auto* dialog=findParentComponentOfClass<juce::DialogWindow>())
        dialog->exitModalState(result);
 }
 AudioEngine& engine;
 int trackIndex;
 juce::String name;
 std::function<void()> afterApply;
 LibertyChannelInspectorState::Session session;
 LibertyChannelPresetStatus::Indicator presetIndicator;
 LibertyChannelABHistory::History<HistoryFrame> abHistory;
 LibertyChannelABGestureHistory::Tracker gestureHistory;
 std::size_t shownUndoDepth=static_cast<std::size_t>(-1);
 std::size_t shownRedoDepth=static_cast<std::size_t>(-1);
 LibertyChannelPresetStatus::Indicator openingPresetIndicator;
 ConsoleLookAndFeel metallicStyle;
 std::array<juce::Slider,8> knobs;
 std::array<juce::TextButton,3> power;
 juce::TextButton resetButton{juce::String::fromUTF8("RÉINITIALISER")};
 juce::TextButton copyButton{"COPIER"};
 juce::TextButton pasteButton{"COLLER"};
 juce::TextButton variantButton{"MODE A"};
 juce::TextButton presetsButton{juce::String::fromUTF8("PRÉRÉGLAGES")};
 juce::TextButton undoButton{"ANN"};
 juce::TextButton redoButton{juce::String::fromUTF8("RÉT")};
 std::unique_ptr<juce::FileChooser> presetFileChooser;
 juce::TextButton cancelButton{"ANNULER"};
 juce::TextButton applyButton{"APPLIQUER"};
};

// An explicit, transparent name hit-target is required: the instrument
// buttons live in separate regions, while rename/color input must always
// land on the channel header, even after a panel is re-ordered.
class MixerNameHitTarget final:public juce::Component
{
public:
 std::function<void(const juce::MouseEvent&)> onDown,onDrag,onUp,onDouble;
 void mouseDown(const juce::MouseEvent& e)override
 {
     if(e.getNumberOfClicks()>=2){if(onDouble)onDouble(e);}
     else if(onDown)onDown(e);
 }
 void mouseDoubleClick(const juce::MouseEvent& e)override
 {
     if(onDouble)onDouble(e);
 }
 void mouseDrag(const juce::MouseEvent& e)override {if(onDrag)onDrag(e);}
 void mouseUp(const juce::MouseEvent& e)override {if(onUp)onUp(e);}
};
struct Strip{enum Kind{audio,midi,instrument,master,bus}kind=audio;int lane=0;LibertyMixConsoleMeter::StereoDisplay meter;LibertyMixConsoleMeter::StereoCorrelationDisplay correlation;std::unique_ptr<juce::Slider>fader,pan;std::unique_ptr<MixerNameHitTarget>nameHit;std::unique_ptr<juce::TextButton>mute,solo,plugin,unload,channel;
 std::unique_ptr<juce::Slider>inputGain,highPass,compThreshold,compRatio,gateThreshold;
 std::array<std::unique_ptr<juce::Slider>,3>eqKnobs;
 std::array<std::unique_ptr<juce::TextButton>,3>modulePower;
 std::array<bool,4> moduleExpanded{};std::array<std::unique_ptr<juce::TextButton>,4>moduleTabs;
 std::unique_ptr<juce::TextButton> fxHeader;bool insertsExpanded=true;
 int firstFxSlot=0;int preferredFaderHeight=320;int lastFxCount=0;
 bool instrumentLoaded=false; // Detect a newly loaded synth independently from FX insert occupancy.
 bool gateOn=false,eqOn=false;
 bool compOn=false;std::array<std::unique_ptr<juce::TextButton>,8>fx,fxUnload;juce::Rectangle<int>bounds;};
class MixConsoleView final:public juce::Component,private juce::Timer{
public:
 explicit MixConsoleView(MainComponent&o):owner(o){scrollListener.o=this;verticalListener.o=this;verticalListener.vertical=true;verticalScroll.addListener(&verticalListener);addAndMakeVisible(verticalScroll);setOpaque(true);setInterceptsMouseClicks(true,true);scroll.setRangeLimits(0,1);scroll.addListener(&scrollListener);addAndMakeVisible(scroll);owner.addAndMakeVisible(this);
 // The shared +BUS lives with +AUDIO/+MIDI/+INSTRUMENT under the logo.
 // Route choices occupy a reserved, non-overlapping right-hand inspector.
 routeClose.setButtonText("X");
 routeClose.setTooltip("Fermer le panneau de routage.");
 routeClose.setColour(juce::TextButton::buttonColourId,ModernMixer::panel);
 routeClose.setColour(juce::TextButton::textColourOffId,ModernMixer::text);
 addAndMakeVisible(routeClose);
 routeClose.setVisible(false);
 routeClose.onClick=[this]{routingLane=-1;layout();repaint();};
 for(int i=0;i<(int)routeChoices.size();++i)
 {
     auto& button=routeChoices[(size_t)i];
     button.setColour(juce::TextButton::buttonColourId,ModernMixer::panel);
     button.setColour(juce::TextButton::buttonOnColourId,ModernMixer::sage.darker(.4f));
     button.setColour(juce::TextButton::textColourOffId,ModernMixer::text);
     button.setColour(juce::TextButton::textColourOnId,ModernMixer::text);
     button.setMouseClickGrabsKeyboardFocus(false);
     button.setClickingTogglesState(false);
     addAndMakeVisible(button);
     button.setVisible(false);
     button.onClick=[this,i]
     {
         if(routingLane<0)return;
         const int destination=i-1; // 0 -> Master, 1...8 -> BUS
         if(routingKind==Strip::audio)
             owner.audioEngine.setTrackMixerBus(routingLane,destination);
         else
             owner.audioEngine.setInstrumentMixerBus(routingLane,destination);
         routingLane=-1;
         layout();
         repaint();
     };
 }
 detailButton.setButtonText(LibertyFrenchLabels::mixerDetails());
 detailButton.setTooltip(juce::String::fromUTF8(
     "Afficher ou masquer les modules de la Console sans déplacer les faders."));

 detailButton.setMouseClickGrabsKeyboardFocus(false);
 detailButton.setColour(juce::TextButton::buttonColourId,ModernMixer::panel);
 detailButton.setColour(juce::TextButton::textColourOffId,ModernMixer::text);
 detailButton.setColour(juce::TextButton::buttonOnColourId,ModernMixer::sage.darker(.65f));
 detailButton.setColour(juce::TextButton::textColourOnId,ModernMixer::text);
 detailButton.setClickingTogglesState(false);
 detailButton.setToggleState(false,juce::dontSendNotification);
 detailButton.onClick=[this]
 {
     detailedView=!detailedView;
     for(auto& strip:strips)
     {
         if(strip->kind==Strip::audio)strip->moduleExpanded.fill(detailedView);
         if(strip->kind==Strip::audio||strip->kind==Strip::instrument)
             strip->insertsExpanded=true; // Compact mode still shows next empty insert.
     }
     detailButton.setButtonText(detailedView
         ?LibertyFrenchLabels::mixerCompact()
         :LibertyFrenchLabels::mixerDetails());
     detailButton.setToggleState(detailedView,juce::dontSendNotification);
     detailButton.setColour(juce::TextButton::buttonColourId,
         detailedView?ModernMixer::sage.darker(.65f):ModernMixer::panel);
     layout();
     repaint();
 };
 addAndMakeVisible(detailButton);
 meterModeButton.setButtonText("PEAK");
 meterModeButton.setMouseClickGrabsKeyboardFocus(false);
 meterModeButton.setTooltip(juce::String::fromUTF8(
     "Afficher les crêtes PEAK ou les niveaux moyens RMS en dBFS."));
 meterModeButton.setColour(juce::TextButton::buttonColourId,ModernMixer::panel);
 meterModeButton.setColour(juce::TextButton::textColourOffId,ModernMixer::text);
 meterModeButton.onClick=[this]{toggleMeterReadout();};
 addAndMakeVisible(meterModeButton);
 meterModeButton.setVisible(false);
 searchBox.setMultiLine(false);
 searchBox.setReturnKeyStartsNewLine(false);
 searchBox.setEscapeAndReturnKeysConsumed(true);
 searchBox.setInputRestrictions(40);
 searchBox.setTextToShowWhenEmpty(
     juce::String::fromUTF8("RECHERCHER UNE PISTE"),ModernMixer::secondary);
 searchBox.setColour(juce::TextEditor::backgroundColourId,ModernMixer::panel);
 searchBox.setColour(juce::TextEditor::textColourId,ModernMixer::text);
 searchBox.setColour(juce::TextEditor::outlineColourId,ModernMixer::border);
 searchBox.setColour(juce::TextEditor::focusedOutlineColourId,ModernMixer::sage);
 searchBox.onTextChange=[this]{
     if(searchBox.getText().trim().isEmpty())lastObservedTrack=-99999;
     findMixerTrack(false);
     repaint();
 };
 searchBox.onReturnKey=[this]{findMixerTrack(true);};
 searchBox.onEscapeKey=[this]{clearMixerSearch();};
 addAndMakeVisible(searchBox);
 searchBox.setVisible(false);
 clearSearchButton.setButtonText("X");
 clearSearchButton.setMouseClickGrabsKeyboardFocus(false);
 clearSearchButton.setTooltip(juce::String::fromUTF8(
     "Effacer la recherche et revenir à la piste sélectionnée."));
 clearSearchButton.setColour(juce::TextButton::buttonColourId,ModernMixer::panel);
 clearSearchButton.setColour(juce::TextButton::textColourOffId,ModernMixer::text);
 clearSearchButton.onClick=[this]{clearMixerSearch();};
 addAndMakeVisible(clearSearchButton);
 clearSearchButton.setVisible(false);
 renameEditor.setMultiLine(false);
 renameEditor.setReturnKeyStartsNewLine(false);
 renameEditor.setEscapeAndReturnKeysConsumed(true);
 renameEditor.setSelectAllWhenFocused(true);
 renameEditor.setColour(juce::TextEditor::backgroundColourId,juce::Colour(0xff243638));
 renameEditor.setColour(juce::TextEditor::textColourId,juce::Colour(0xfff1faf8));
 renameEditor.setColour(juce::TextEditor::outlineColourId,juce::Colour(0xff8ecbb2));
 renameEditor.setColour(juce::TextEditor::focusedOutlineColourId,juce::Colour(0xff8ecbb2));
 renameEditor.onReturnKey=[this]{finishRename(true);};
 renameEditor.onEscapeKey=[this]{finishRename(false);};
 renameEditor.onFocusLost=[this]{finishRename(true);};
 addAndMakeVisible(renameEditor);
 renameEditor.setVisible(false);
 startTimerHz(24);setVisible(false);}
 ~MixConsoleView()override{
    stopTimer();
    searchBox.onTextChange=nullptr;
    searchBox.onReturnKey=nullptr;
    searchBox.onEscapeKey=nullptr;
    clearSearchButton.onClick=nullptr;
    meterModeButton.onClick=nullptr;
    renameEditor.onReturnKey=nullptr;
    renameEditor.onEscapeKey=nullptr;
    renameEditor.onFocusLost=nullptr;
    finishRename(true);
    scroll.removeListener(&scrollListener);
    verticalScroll.removeListener(&verticalListener);
    clear();
}
 void setConsoleVisible(bool b){visible=b;setVisible(b);if(b){lastObservedTrack=-99999;setBounds(0,transportHeight,owner.getWidth(),juce::jmax(1,owner.getHeight()-transportHeight));rebuild();toFront(true);sync();layout();updateMeters();repaint();}else{finishRename(true);selected=nullptr;}}
 bool isConsoleVisible()const{return visible;} void refreshStatic(){if(!visible)return;finishRename(true);routingLane=-1;rebuild();sync();layout();repaint();}
 int getDropTrackForOwnerPoint(juce::Point<int> p,bool instrumentPlugin)const
 {
     if(!visible||p.y<transportHeight)return -1;
     const auto q=p-juce::Point<int>(0,transportHeight);
     // A track hidden behind pinned Master or routing can never accept a drop.
     if(!LibertyNoOverlap::mixerChannelHit(
          getWidth(),getHeight(),routingLane>=0,q.x,q.y))return -1;
     for(const auto& strip:strips)
     {
         if(strip->kind==Strip::master||!strip->bounds.contains(q))continue;
         if(instrumentPlugin&&strip->kind==Strip::instrument)
             return owner.getAudioTrackCount()+owner.getMidiTrackCount()+strip->lane;
         if(!instrumentPlugin&&(strip->kind==Strip::audio||
                                strip->kind==Strip::instrument))
         {
             for(int slot=0;slot<8;++slot)
                 if(strip->fx[(size_t)slot]&&strip->fx[(size_t)slot]->isVisible()&&
                    strip->fx[(size_t)slot]->getBounds().contains(q))
                     return 1000000+(strip->kind==Strip::audio?strip->lane:
                        owner.getAudioTrackCount()+owner.getMidiTrackCount()+
                        strip->lane)*8+slot;
             return -1;
         }
     }
     return -1;
 }
 void paint(juce::Graphics&g)override
 {
     g.fillAll(ModernMixer::canvas);
     g.setColour(ModernMixer::header);
     g.fillRect(0,0,getWidth(),LibertyNoOverlap::mixerToolbarHeight);
     // The 330px left zone belongs exclusively to the creation controls.
     // Never draw a caption or another mixer command over it.
     g.setColour(juce::Colour(0xff1a2b2b));
     g.fillRect(0,0,LibertyNoOverlap::creationZoneWidth,
                LibertyNoOverlap::mixerToolbarHeight);
     g.setColour(ModernMixer::border);
     g.drawHorizontalLine(53,0.0f,(float)getWidth());
     g.drawVerticalLine(LibertyNoOverlap::creationZoneWidth,3.0f,50.0f);
     const auto hint=LibertyNoOverlap::mixerHint(getWidth());
     g.setColour(ModernMixer::sage);
     g.fillRoundedRectangle((float)hint.x,(float)hint.y+1.f,3.f,19.f,1.5f);
     g.setColour(ModernMixer::secondary);
     g.setFont(juce::Font(10.f,juce::Font::bold));
     juce::String caption=juce::String("LIBERTY MIXER  |  ")
         +juce::String(owner.getAudioTrackCount()+owner.getMidiTrackCount()
                       +owner.getInstrumentTrackCount())+" PISTES  |  "
         +juce::String(owner.audioEngine.getMixerBusCount())+" BUS  |  "
         +juce::String::fromUTF8("MAJ + MOLETTE : DÉFILER");
     const auto query=searchBox.getText().trim();
     if(query.isNotEmpty())
     {
         int matches=0,position=0;
         for(const auto& strip:strips)
             if(matchesMixerSearch(*strip,query))
             {
                 ++matches;
                 if(strip.get()==selected)position=matches;
             }
         caption=juce::String::fromUTF8("RECHERCHE : ")+query+"  |  "+
             juce::String(position)+"/"+juce::String(matches)+
             juce::String::fromUTF8("  |  ENTRÉE : SUIVANT");
     }
     g.drawFittedText(caption,hint.x+12,hint.y,juce::jmax(1,hint.w-12),
                      hint.h,juce::Justification::centredLeft,1);
     const auto bank=LibertyNoOverlap::scrollableChannels(getWidth(),getHeight(),routingLane>=0);
     const auto master=LibertyNoOverlap::pinnedMaster(getWidth(),getHeight(),routingLane>=0);
     {
         juce::Graphics::ScopedSaveState clip(g);
         g.reduceClipRegion(bank.x,bank.y,bank.w,bank.h);
         for(auto& strip:strips)if(strip->kind!=Strip::master)draw(g,*strip);
         if(draggingStrip&&dropStripIndex>=0&&dropStripIndex<(int)strips.size()-1)
         {
             const int dropX=strips[(size_t)dropStripIndex]->bounds.getX()-2;
             g.setColour(ModernMixer::sage);
             g.fillRect(dropX,bank.y,3,bank.h);
         }
     }
     {
         juce::Graphics::ScopedSaveState clip(g);
         g.reduceClipRegion(master.x,master.y,master.w,master.h);
         for(auto& strip:strips)if(strip->kind==Strip::master)draw(g,*strip);
     }
     g.setColour(ModernMixer::sage.withAlpha(.32f));
     g.drawVerticalLine(master.x-6,(float)master.y+4.f,
                        (float)master.bottom()-4.f);
     if(routingLane>=0)
     {
         const auto pane=LibertyNoOverlap::routingPanel(getWidth(),getHeight(),true);
         g.setColour(ModernMixer::strip);
         g.fillRoundedRectangle((float)pane.x+2.f,(float)pane.y+2.f,
                                (float)pane.w-4.f,(float)pane.h-4.f,7.f);
         g.setColour(ModernMixer::border);
         g.drawRoundedRectangle((float)pane.x+2.f,(float)pane.y+2.f,
                                (float)pane.w-4.f,(float)pane.h-4.f,7.f,1.f);
         g.setColour(ModernMixer::text);
         g.setFont(juce::Font(12.f,juce::Font::bold));
         g.drawFittedText("ROUTAGE AUDIO",pane.x+14,pane.y+15,pane.w-60,25,
                          juce::Justification::centredLeft,1);
         const int logical=routingKind==Strip::audio?routingLane:
             owner.getAudioTrackCount()+owner.getMidiTrackCount()+routingLane;
         g.setColour(ModernMixer::secondary);
         g.setFont(juce::Font(10.f));
         g.drawFittedText(owner.getTrackDisplayName(logical),
             pane.x+14,pane.y+42,pane.w-28,18,juce::Justification::centredLeft,1);
     }
 }
 void resized()override{layout();}
 void mouseWheelMove(const juce::MouseEvent& e,const juce::MouseWheelDetails& wheel)override{
   const auto bank=LibertyNoOverlap::scrollableChannels(getWidth(),getHeight(),routingLane>=0);
   if(!LibertyNoOverlap::mixerChannelHit(getWidth(),getHeight(),
             routingLane>=0,e.getPosition().x,e.getPosition().y))return;
   // Shift+wheel or a horizontal trackpad swipe scrolls ONLY tracks.
   if(e.mods.isShiftDown()||std::abs(wheel.deltaX)>0.001f)
   {
       const float amount=std::abs(wheel.deltaX)>0.001f?wheel.deltaX:wheel.deltaY;
       const int maximum=LibertyNoOverlap::channelMaxScroll(
           juce::jmax(0,(int)strips.size()-1),bank.w);
       const int next=LibertyNoOverlap::channelScroll(
           offset,(int)std::round(-amount*180.f),maximum);
       if(next!=offset){offset=next;layout();repaint();}
       return;
   }
   for(auto& strip:strips){
     if(!strip->bounds.contains(e.getPosition())||!strip->fxHeader||!strip->insertsExpanded)continue;
     const auto g=getStripGeometry(*strip);
     const auto y=e.getPosition().y-strip->bounds.getY();
     const int requested=neededFxSlots(*strip);
     if(g.visibleFx>=requested||y<g.fxY||y>=g.fxY+g.visibleFx*g.fxStep)continue;
     strip->firstFxSlot=LibertyMixerProgressiveFx::windowStart(
          requested,g.visibleFx,strip->firstFxSlot+(wheel.deltaY<0?1:-1));
     layout();repaint();return;
   }
 }
 void mouseDown(const juce::MouseEvent&e)override{
   const auto p=e.getPosition();
   const bool masterArea=LibertyNoOverlap::mixerMasterHit(
       getWidth(),getHeight(),routingLane>=0,p.x,p.y);
   const bool bankArea=LibertyNoOverlap::mixerChannelHit(
       getWidth(),getHeight(),routingLane>=0,p.x,p.y);
   if(!masterArea&&!bankArea)return;
   for(auto& strip:strips){
     if((strip->kind==Strip::master)!=masterArea||
        !strip->bounds.contains(p))continue;
     const bool nameHeader=e.getPosition().x>=strip->bounds.getX()+6 &&
         e.getPosition().x<strip->bounds.getRight()-6 &&
         e.getPosition().y>=strip->bounds.getY()+10 &&
         e.getPosition().y<strip->bounds.getY()+35;
     if(nameHeader && e.mods.isPopupMenu())
     {
         showStripColour(*strip,e.getScreenPosition());
         return;
     }
     if(nameHeader && e.mods.isLeftButtonDown() && e.getNumberOfClicks()>=2)
     {
         beginRename(*strip);
         return;
     }
     if(e.mods.isPopupMenu())
     {
         if(strip->kind!=Strip::midi)
         {
             const auto geom=getStripGeometry(*strip);
             const int meterTop=strip->bounds.getY()+geom.faderY;
             const int meterBottom=strip->bounds.getBottom()-36;
             if(LibertyMixConsoleMeter::resolveMeterClick(
                 p.x-strip->bounds.getX(),p.y,meterTop,meterBottom)!=
                 LibertyMixConsoleMeter::MeterClick::none)
                 showMeterMenu(*strip,e.getScreenPosition());
         }
         return; // No right-click starts a drag.
     }
     // The route label is clickable, with no conflict with COR/BAL or faders.
     if((strip->kind==Strip::audio||strip->kind==Strip::instrument)&&
        e.mods.isLeftButtonDown()&&
        e.getPosition().x>=strip->bounds.getX()+9&&
        e.getPosition().x<strip->bounds.getX()+77&&
        e.getPosition().y>=strip->bounds.getY()+55&&
        e.getPosition().y<strip->bounds.getY()+68)
     {
         routingKind=strip->kind;
         routingLane=strip->lane;
         layout();
         repaint();
         return;
     }

     if(strip->kind!=Strip::midi){
       const auto geometry=getStripGeometry(*strip);
       const int meterTop=strip->bounds.getY()+geometry.faderY;
       const int meterBottom=strip->bounds.getBottom()-36;
       // Click the numeric readout/legend to switch all strips PEAK/RMS.
       // Click the bars themselves to reset held sample peaks and CLIP.
       const auto hit=LibertyMixConsoleMeter::resolveMeterClick(
           e.getPosition().x-strip->bounds.getX(),e.getPosition().y,
           meterTop,meterBottom);
       if(hit==LibertyMixConsoleMeter::MeterClick::toggleDigital){
         toggleMeterReadout();
         return;
       }
       if(hit==LibertyMixConsoleMeter::MeterClick::resetPeaks){
         strip->meter.resetPeak();
         repaint(strip->bounds.getX()+3,meterTop,44,meterBottom-meterTop+35);
         return;
       }
     }
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
     // Drag only the name header; double click is handled above first.
     if(nameHeader&&e.mods.isLeftButtonDown())
     {
         draggingStrip=strip.get();
         dropStripIndex=(int)std::distance(strips.begin(),
             std::find_if(strips.begin(),strips.end(),
                 [stripPtr=strip.get()](const auto& s){return s.get()==stripPtr;}));
         setMouseCursor(juce::MouseCursor::DraggingHandCursor);
     }
     if(strip->kind==Strip::bus){selected=nullptr;repaint();return;}
     selected=strip.get();
     owner.selectedTrack=strip->kind==Strip::audio?strip->lane:
       (strip->kind==Strip::midi?owner.getAudioTrackCount()+strip->lane:
       owner.getAudioTrackCount()+owner.getMidiTrackCount()+strip->lane);
     repaint();owner.repaint();return;
   }
 }
 void mouseDrag(const juce::MouseEvent&e)override{
   if(renamingLane>=0)return;
   if(draggingStrip)
   {
       const auto bank=LibertyNoOverlap::scrollableChannels(getWidth(),getHeight(),routingLane>=0);
       if(e.getPosition().x<bank.x||e.getPosition().x>=bank.right())
       {
           dropStripIndex=-1; // Never reorder onto the fixed Master.
           repaint();
           return;
       }
       dropStripIndex=juce::jlimit(0,juce::jmax(0,(int)strips.size()-2),
           (e.getPosition().x-12+offset)/stripW);
       repaint();
       return;
   }
   if(!draggingHeight)return;
   draggingHeight->preferredFaderHeight=juce::jlimit(100,360,
       dragStartPreference+dragStartY-e.getPosition().y);
   layout();repaint();
 }
 void mouseUp(const juce::MouseEvent& e)override{
   if(draggingStrip)
   {
       const auto* moving=draggingStrip;
       draggingStrip=nullptr;
       const auto it=std::find_if(strips.begin(),strips.end(),
           [moving](const auto& strip){return strip.get()==moving;});
       const int from=it==strips.end()?-1:(int)std::distance(strips.begin(),it);
       const int dest=dropStripIndex;
       dropStripIndex=-1;
       if(from>=0&&dest>=0&&from!=dest&&e.getDistanceFromDragStart()>=5)
       {
           auto strip=std::move(strips[(size_t)from]);
           strips.erase(strips.begin()+from);
           strips.insert(strips.begin()+dest,std::move(strip));
           owner.mixerStripOrder.clear();
           owner.arrangeTrackOrder.clear();
           const int audio=owner.getAudioTrackCount(),midi=owner.getMidiTrackCount();
           for(const auto& item:strips)
           {
               if(item->kind==Strip::master)continue;
               owner.mixerStripOrder.add(stripKey(item->kind,item->lane));
               if(item->kind==Strip::bus)continue;
               const int logical=item->kind==Strip::audio?item->lane:
                   item->kind==Strip::midi?audio+item->lane:
                   audio+midi+item->lane;
               owner.arrangeTrackOrder.push_back(logical);
           }
           owner.reconcileArrangeTrackOrder();
           refreshLibertyTrackHeaderMixLayout(&owner);
           owner.repaint();
           layout();
       }
       setMouseCursor(juce::MouseCursor::NormalCursor);
       repaint();
       return;
   }
   draggingHeight=nullptr;setMouseCursor(juce::MouseCursor::NormalCursor);
 }
 void mouseMove(const juce::MouseEvent&e)override{
   const auto p=e.getPosition();
   const bool masterArea=LibertyNoOverlap::mixerMasterHit(
       getWidth(),getHeight(),routingLane>=0,p.x,p.y);
   const bool bankArea=LibertyNoOverlap::mixerChannelHit(
       getWidth(),getHeight(),routingLane>=0,p.x,p.y);
   if(!masterArea&&!bankArea)
   {
       setMouseCursor(juce::MouseCursor::NormalCursor);
       return;
   }
   bool onHandle=false,onReadout=false,onRoute=false;
   for(const auto& strip:strips){
     if((strip->kind==Strip::master)!=masterArea||
        !strip->bounds.contains(p))continue;
     if(strip->kind!=Strip::midi){
       const auto geometry=getStripGeometry(*strip);
       const int top=strip->bounds.getY()+geometry.faderY;
       const int bottom=strip->bounds.getBottom()-36;
       onReadout=LibertyMixConsoleMeter::resolveMeterClick(
           e.getPosition().x-strip->bounds.getX(),e.getPosition().y,
           top,bottom)==LibertyMixConsoleMeter::MeterClick::toggleDigital;
     }
     if(strip->kind==Strip::audio||strip->kind==Strip::instrument)
       onRoute=e.getPosition().x>=strip->bounds.getX()+9&&
           e.getPosition().x<strip->bounds.getX()+77&&
           e.getPosition().y>=strip->bounds.getY()+55&&
           e.getPosition().y<strip->bounds.getY()+68;
     if(strip->kind==Strip::audio||strip->kind==Strip::instrument)
       onHandle=std::abs(e.getPosition().y-strip->bounds.getY()-
                         getStripGeometry(*strip).splitterY)<=6;
     break;
   }
   setMouseCursor((onReadout||onRoute)?juce::MouseCursor::PointingHandCursor:
       onHandle?juce::MouseCursor::UpDownResizeCursor:
                juce::MouseCursor::NormalCursor);
 }
private:
 void clearMixerSearch()
 {
     searchBox.setText(juce::String(),false);
     searchBox.setColour(juce::TextEditor::outlineColourId,ModernMixer::border);
     lastObservedTrack=-99999;
     searchBox.giveAwayKeyboardFocus();
     followArrangementSelection();
     repaint();
 }
 void toggleMeterReadout()
 {
     meterReadoutMode=LibertyMixConsoleMeter::toggleReadoutMode(meterReadoutMode);
     meterModeButton.setButtonText(meterReadoutMode==
         LibertyMixConsoleMeter::ReadoutMode::peak?"PEAK":"RMS");
     repaint();
 }
 static LibertyMixerSearch::Kind mixerSearchKind(Strip::Kind kind) noexcept
 {
     switch(kind)
     {
         case Strip::audio:return LibertyMixerSearch::Kind::audio;
         case Strip::midi:return LibertyMixerSearch::Kind::midi;
         case Strip::instrument:return LibertyMixerSearch::Kind::instrument;
         case Strip::bus:return LibertyMixerSearch::Kind::bus;
         case Strip::master:return LibertyMixerSearch::Kind::master;
     }
     return LibertyMixerSearch::Kind::audio;
 }
 bool matchesMixerSearch(const Strip& strip,const juce::String& raw) const
 {
     const auto parsed=LibertyMixerSearch::parse(raw.toStdString());
     if(!LibertyMixerSearch::accepts(parsed.category,mixerSearchKind(strip.kind)))
         return false;
     const auto needle=raw.substring((int)parsed.prefixLength).trim();
     return needle.isEmpty()||name(strip).containsIgnoreCase(needle);
 }
 void showMeterMenu(const Strip& source,juce::Point<int> point)
 {
     juce::PopupMenu menu;
     menu.addItem(1,juce::String::fromUTF8("EFFACER LA CRÊTE DE CETTE PISTE"));
     menu.addItem(2,juce::String::fromUTF8("EFFACER TOUTES LES CRÊTES"));
     menu.showMenuAsync(
         juce::PopupMenu::Options().withTargetScreenArea(
             juce::Rectangle<int>(point.x,point.y,1,1)),
         [safe=juce::Component::SafePointer<MixConsoleView>(this),
          kind=source.kind,lane=source.lane](int action)
         {
             if(safe==nullptr||action==0)return;
             for(auto& channel:safe->strips)
                 if(action==2||(action==1&&channel->kind==kind&&
                                channel->lane==lane))
                     channel->meter.resetPeak();
             safe->repaint();
         });
 }
 void focusMixerStrip(Strip& strip)
 {
     if(routingLane>=0)routingLane=-1;
     selected=&strip;
     if(strip.kind!=Strip::master&&strip.kind!=Strip::bus)
         owner.selectedTrack=logicalTrack(strip);
     if(strip.kind!=Strip::master)
     {
         const auto bank=LibertyNoOverlap::scrollableChannels(
             getWidth(),getHeight(),false);
         const auto it=std::find_if(strips.begin(),strips.end(),
             [&strip](const auto& item){return item.get()==&strip;});
         if(it!=strips.end())
             offset=LibertyNoOverlap::centeredChannelOffset(
                 (int)std::distance(strips.begin(),it),
                 juce::jmax(0,(int)strips.size()-1),bank.w);
     }
     layout();
     repaint();
     owner.repaint();
 }
 void findMixerTrack(bool next)
 {
     const auto query=searchBox.getText().trim();
     if(query.isEmpty())
     {
         searchBox.setColour(juce::TextEditor::outlineColourId,ModernMixer::border);
         repaint();
         return;
     }
     const int count=(int)strips.size();
     if(count==0)return;
     int start=0;
     if(next&&selected)
     {
         const auto current=std::find_if(strips.begin(),strips.end(),
             [this](const auto& item){return item.get()==selected;});
         if(current!=strips.end())
             start=((int)std::distance(strips.begin(),current)+1)%count;
     }
     for(int step=0;step<count;++step)
     {
         auto& candidate=*strips[(size_t)((start+step)%count)];
         if(matchesMixerSearch(candidate,query))
         {
             searchBox.setColour(juce::TextEditor::outlineColourId,ModernMixer::border);
             focusMixerStrip(candidate);
             return;
         }
     }
     searchBox.setColour(juce::TextEditor::outlineColourId,ModernMixer::amber);
     repaint();
 }
 void followArrangementSelection()
 {
     const int logical=owner.selectedTrack;
     if(lastObservedTrack==logical)return;
     lastObservedTrack=logical;
     if(logical<0||!searchBox.getText().isEmpty()||draggingStrip||
        draggingHeight||renamingLane>=0)return;
     for(auto& item:strips)
     {
         if(item->kind!=Strip::bus&&item->kind!=Strip::master&&
            logicalTrack(*item)==logical)
         {
             focusMixerStrip(*item);
             return;
         }
     }
 }
 int logicalTrack(const Strip& strip) const noexcept
 {
     if(strip.kind==Strip::audio)return strip.lane;
     if(strip.kind==Strip::midi)return owner.getAudioTrackCount()+strip.lane;
     if(strip.kind==Strip::instrument)
         return owner.getAudioTrackCount()+owner.getMidiTrackCount()+strip.lane;
     return -1;
 }
 void showStripColour(const Strip& strip,juce::Point<int> screenPoint)
 {
     const auto kind=strip.kind;
     const int lane=strip.lane;
     const int logical=logicalTrack(strip);
     const int colour=kind==Strip::bus?owner.getMixerBusColourId(lane):
         kind==Strip::master?owner.getMixerMasterColourId():
         getLibertyTrackColourId(logical);
     juce::Component::SafePointer<MixConsoleView> safe(this);
     showLibertyMixerColourPalette(screenPoint,colour,
         [safe,kind,lane,logical](int id)
         {
             if(safe==nullptr)return;
             if(kind==Strip::bus)safe->owner.setMixerBusColourId(lane,id);
             else if(kind==Strip::master)safe->owner.setMixerMasterColourId(id);
             else setLibertyTrackColourId(logical,id);
             safe->owner.repaint();
             safe->repaint();
         });
 }
 void beginRename(const Strip& strip)
 {
     finishRename(true);
     renamingKind=strip.kind;
     renamingLane=strip.lane;
     renameEditor.setText(name(strip),false);
     const auto bounds=strip.bounds;
     renameEditor.setBounds(bounds.getX()+7,bounds.getY()+10,bounds.getWidth()-14,24);
     renameEditor.setVisible(true);
     renameEditor.toFront(true);
     renameEditor.grabKeyboardFocus();
     renameEditor.selectAll();
     repaint(bounds.getX(),bounds.getY()+8,bounds.getWidth(),30);
 }
 void finishRename(bool commit)
 {
     if(renamingLane<0)return;
     const int lane=renamingLane;
     const auto kind=renamingKind;
     renamingLane=-1; // Prevent reentrancy through focus callbacks.
     if(commit)
     {
         const auto label=renameEditor.getText().trim().substring(0,32);
         if(label.isNotEmpty())
         {
             if(kind==Strip::bus)owner.setMixerBusName(lane,label);
             else if(kind==Strip::master)owner.setMixerMasterName(label);
             else
             {
                 const int logical=kind==Strip::audio?lane:
                     kind==Strip::midi?owner.getAudioTrackCount()+lane:
                     owner.getAudioTrackCount()+owner.getMidiTrackCount()+lane;
                 setLibertyTrackName(logical,label);
             }
         }
     }
     renameEditor.setVisible(false);
     owner.repaint(); // Arrangement label and project state share the value.
     repaint();
 }
 struct ScrollListener:juce::ScrollBar::Listener{MixConsoleView*o=nullptr;bool vertical=false;void scrollBarMoved(juce::ScrollBar*,double v)override{if(o){if(vertical)o->verticalOffset=(int)std::round(v);else o->offset=(int)std::round(v);o->layout();o->repaint();}}}scrollListener,verticalListener;
 void clear(){selected=nullptr;draggingHeight=nullptr;draggingStrip=nullptr;dropStripIndex=-1;for(auto&s:strips){if(s->fader)s->fader->setLookAndFeel(nullptr);if(s->pan)s->pan->setLookAndFeel(nullptr);}strips.clear();}
 std::unique_ptr<Strip> makeStrip(Strip::Kind k,int lane){auto s=std::make_unique<Strip>();s->kind=k;s->lane=lane;s->fader=std::make_unique<juce::Slider>();s->fader->setSliderStyle(juce::Slider::LinearVertical);s->fader->setRange(-60,6,.1);s->fader->setDoubleClickReturnValue(true,0.0);s->fader->setTextBoxStyle(juce::Slider::TextBoxBelow,false,62,20);s->fader->setNumDecimalPlacesToDisplay(1);s->fader->setMouseClickGrabsKeyboardFocus(false);s->fader->setTextValueSuffix(" dB");s->fader->setLookAndFeel(&lookAndFeel);addAndMakeVisible(*s->fader);if(k==Strip::audio||k==Strip::instrument){
 s->fxHeader=std::make_unique<juce::TextButton>("INSERTS");
 s->fxHeader->setMouseClickGrabsKeyboardFocus(false);
 s->fxHeader->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff203b50));
 s->fxHeader->setColour(juce::TextButton::textColourOffId,juce::Colour(0xffe6edf3));
 addAndMakeVisible(*s->fxHeader);
 s->fxHeader->setTooltip("Ouvrir ou fermer les inserts. Maj + clic : toutes les pistes.");
 if(k==Strip::audio){for(int tab=0;tab<4;++tab){auto&button=s->moduleTabs[(size_t)tab];button=std::make_unique<juce::TextButton>(tab==0?"EQ":tab==1?"GATE":tab==2?"COMP":"INPUT");button->setMouseClickGrabsKeyboardFocus(false);button->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff263b4b));button->setColour(juce::TextButton::textColourOffId,juce::Colour(0xffe6edf3));button->setTooltip("Ouvrir ou fermer cette section. Maj + clic : toutes les pistes audio.");
 addAndMakeVisible(*button);}for(int section=0;section<3;++section){auto&button=s->modulePower[(size_t)section];button=std::make_unique<juce::TextButton>("OFF");button->setMouseClickGrabsKeyboardFocus(false);button->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff34404a));button->setColour(juce::TextButton::textColourOffId,juce::Colours::white);addAndMakeVisible(*button);}for(int band=0;band<3;++band){auto&knob=s->eqKnobs[(size_t)band];knob=std::make_unique<juce::Slider>();knob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);knob->setRange(-18.0,18.0,0.5);knob->setValue(0.0,juce::dontSendNotification);knob->setDoubleClickReturnValue(true,0.0);knob->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);knob->setMouseClickGrabsKeyboardFocus(false);knob->setLookAndFeel(&lookAndFeel);
knob->setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xff56cbdc));
knob->setTooltip(juce::String("EQ : ")+juce::String::fromUTF8(band==0?"graves":band==1?"médiums":"aigus")+juce::String::fromUTF8(" -18 à +18 dB"));addAndMakeVisible(*knob);}s->gateThreshold=std::make_unique<juce::Slider>();s->gateThreshold->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);s->gateThreshold->setRange(-80.0,-20.0,1.0);s->gateThreshold->setValue(-55.0,juce::dontSendNotification);s->gateThreshold->setDoubleClickReturnValue(true,-55.0);s->gateThreshold->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);s->gateThreshold->setMouseClickGrabsKeyboardFocus(false);s->gateThreshold->setLookAndFeel(&lookAndFeel);s->gateThreshold->setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xff8bc99e));s->gateThreshold->setTooltip(juce::String::fromUTF8("GATE : seuil en dB, déplacement pour activer"));addAndMakeVisible(*s->gateThreshold);s->compThreshold=std::make_unique<juce::Slider>();s->compRatio=std::make_unique<juce::Slider>();for(auto*knob:{s->compThreshold.get(),s->compRatio.get()}){knob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);knob->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);knob->setMouseClickGrabsKeyboardFocus(false);knob->setLookAndFeel(&lookAndFeel);knob->setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xffe2ac65));addAndMakeVisible(*knob);}s->compThreshold->setRange(-48.0,0.0,1.0);s->compThreshold->setValue(-18.0,juce::dontSendNotification);s->compThreshold->setDoubleClickReturnValue(true,-18.0);s->compThreshold->setTooltip("DYNAMIQUE : seuil du compresseur (dB)");s->compRatio->setRange(1.0,12.0,0.5);s->compRatio->setValue(4.0,juce::dontSendNotification);s->compRatio->setDoubleClickReturnValue(true,4.0);s->compRatio->setTooltip("DYNAMIQUE : ratio du compresseur");s->highPass=std::make_unique<juce::Slider>();s->highPass->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);s->highPass->setRange(0.0,500.0,1.0);s->highPass->setValue(0.0,juce::dontSendNotification);s->highPass->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);s->highPass->setDoubleClickReturnValue(true,0.0);s->highPass->setMouseClickGrabsKeyboardFocus(false);s->highPass->setLookAndFeel(&lookAndFeel);s->highPass->setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xff73bbdb));s->highPass->setTooltip(juce::String::fromUTF8("INPUT : coupe-bas de 0 (désactivé) à 500 Hz"));addAndMakeVisible(*s->highPass);s->inputGain=std::make_unique<juce::Slider>();s->inputGain->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);s->inputGain->setRange(-24.0,24.0,0.5);s->inputGain->setValue(0.0,juce::dontSendNotification);s->inputGain->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);s->inputGain->setDoubleClickReturnValue(true,0.0);s->inputGain->setMouseClickGrabsKeyboardFocus(false);s->inputGain->setLookAndFeel(&lookAndFeel);s->inputGain->setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xff73bbdb)); s->inputGain->setTooltip(juce::String::fromUTF8("INPUT : gain d'entrée de -24 à +24 dB"));addAndMakeVisible(*s->inputGain);s->channel=std::make_unique<juce::TextButton>("CANAL");s->channel->setMouseClickGrabsKeyboardFocus(false);s->channel->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff24465a));s->channel->setColour(juce::TextButton::textColourOffId,juce::Colours::white);addAndMakeVisible(*s->channel);}s->pan=std::make_unique<juce::Slider>();s->pan->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);s->pan->setRange(-1,1,.01);s->pan->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);s->pan->setDoubleClickReturnValue(true,0.0);s->pan->setMouseClickGrabsKeyboardFocus(false);s->pan->setLookAndFeel(&lookAndFeel);s->pan->setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xffe1c58a));addAndMakeVisible(*s->pan);s->mute=std::make_unique<juce::TextButton>("M");s->solo=std::make_unique<juce::TextButton>("S");s->mute->setClickingTogglesState(false);s->solo->setClickingTogglesState(false);s->mute->setMouseClickGrabsKeyboardFocus(false);s->solo->setMouseClickGrabsKeyboardFocus(false);s->mute->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff25323e));s->solo->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff25323e));s->mute->setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xffa44141));s->solo->setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xffa18432));s->mute->setColour(juce::TextButton::textColourOffId,juce::Colours::white);s->solo->setColour(juce::TextButton::textColourOffId,juce::Colours::white);s->mute->setColour(juce::TextButton::textColourOnId,juce::Colours::white);s->solo->setColour(juce::TextButton::textColourOnId,juce::Colours::white);addAndMakeVisible(*s->mute);addAndMakeVisible(*s->solo);s->solo->setTooltip(
 juce::String::fromUTF8("Clic : Solo normal. Option + clic : Solo exclusif. "
 "Répéter Option + clic : quitter le Solo exclusif. Maj + clic : supprimer tous les Solo."));}s->plugin=std::make_unique<juce::TextButton>();s->plugin->setMouseClickGrabsKeyboardFocus(false);s->plugin->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff18212b));s->plugin->setColour(juce::TextButton::textColourOffId,juce::Colour(0xffd4d8de));s->unload=std::make_unique<juce::TextButton>("X");s->unload->setMouseClickGrabsKeyboardFocus(false);s->unload->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff342126));s->unload->setColour(juce::TextButton::textColourOffId,juce::Colours::white);addAndMakeVisible(*s->plugin);addAndMakeVisible(*s->unload);if(k==Strip::audio||k==Strip::instrument)for(int i=0;i<8;++i){s->fx[(size_t)i]=std::make_unique<juce::TextButton>("FX "+juce::String(i+1)+"  + CHARGER");s->fxUnload[(size_t)i]=std::make_unique<juce::TextButton>("X");s->fx[(size_t)i]->setMouseClickGrabsKeyboardFocus(false);s->fxUnload[(size_t)i]->setMouseClickGrabsKeyboardFocus(false);s->fx[(size_t)i]->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff18212b));s->fx[(size_t)i]->setColour(juce::TextButton::textColourOffId,juce::Colour(0xffd4d8de));s->fxUnload[(size_t)i]->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff342126));s->fxUnload[(size_t)i]->setColour(juce::TextButton::textColourOffId,juce::Colours::white);addAndMakeVisible(*s->fx[(size_t)i]);addAndMakeVisible(*s->fxUnload[(size_t)i]);}if(k==Strip::bus){
    // Same pin-safe geometry for BUS M/S and stereo pan as instrument tracks.
    s->pan=std::make_unique<juce::Slider>();
    s->pan->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s->pan->setRange(-1,1,.01);
    s->pan->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
    s->pan->setDoubleClickReturnValue(true,0.0);
    s->pan->setMouseClickGrabsKeyboardFocus(false);
    s->pan->setLookAndFeel(&lookAndFeel);
    s->pan->setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xffbd95ed));
    addAndMakeVisible(*s->pan);
    s->mute=std::make_unique<juce::TextButton>("M");
    s->solo=std::make_unique<juce::TextButton>("S");
    for(auto* button:{s->mute.get(),s->solo.get()}){
        button->setClickingTogglesState(false);
        button->setMouseClickGrabsKeyboardFocus(false);
        button->setColour(juce::TextButton::buttonColourId,juce::Colour(0xff25323e));
        button->setColour(juce::TextButton::textColourOffId,juce::Colours::white);
        addAndMakeVisible(*button);
    }
    s->mute->setTooltip("Couper ce BUS.");
    s->solo->setTooltip("Solo des groupes BUS.");
}// Final appearance pass: consistent matte surfaces for the controls
 // created inside each strip. Only colors change, not hit rectangles.
 s->fader->setColour(juce::Slider::textBoxBackgroundColourId,
                     juce::Colour(0xff182829));
 s->fader->setColour(juce::Slider::textBoxTextColourId,ModernMixer::text);
 s->fader->setColour(juce::Slider::textBoxOutlineColourId,ModernMixer::border);
 s->fader->setColour(juce::Slider::textBoxHighlightColourId,ModernMixer::sage.withAlpha(.35f));
 auto buttonSkin=[](juce::TextButton* button,juce::Colour face,
                    juce::Colour foreground)
 {
     if(button==nullptr)return;
     button->setColour(juce::TextButton::buttonColourId,face);
     button->setColour(juce::TextButton::textColourOffId,foreground);
 };
 buttonSkin(s->fxHeader.get(),juce::Colour(0xff25403e),ModernMixer::text);
 buttonSkin(s->plugin.get(),juce::Colour(0xff1b3031),ModernMixer::secondary);
 buttonSkin(s->unload.get(),juce::Colour(0xff372c2c),juce::Colour(0xffe6bbbb));
 buttonSkin(s->channel.get(),juce::Colour(0xff36544e),ModernMixer::text);
 for(auto& button:s->moduleTabs)
     buttonSkin(button.get(),juce::Colour(0xff2a403e),ModernMixer::secondary);
 for(auto& button:s->modulePower)
     buttonSkin(button.get(),juce::Colour(0xff30413e),ModernMixer::secondary);
 for(auto& button:s->fx)
     buttonSkin(button.get(),juce::Colour(0xff1c3030),ModernMixer::text);
 for(auto& button:s->fxUnload)
     buttonSkin(button.get(),juce::Colour(0xff332b2b),juce::Colour(0xffd6a2a2));
 if(s->pan)
     s->pan->setColour(juce::Slider::rotarySliderFillColourId,ModernMixer::sage);
 if(s->kind==Strip::bus&&s->pan)
     s->pan->setColour(juce::Slider::rotarySliderFillColourId,juce::Colour(0xffc6a4de));
 if(s->pan)
     s->pan->setTooltip(juce::String::fromUTF8(
         "PANORAMIQUE : double-clic pour revenir au centre."));
 if(s->fader)
     s->fader->setTooltip(juce::String::fromUTF8(
         "VOLUME : double-clic pour revenir à 0 dB."));
 if(s->mute)
     s->mute->setTooltip(juce::String::fromUTF8(
         "MUET : couper ou rétablir cette piste."));
 // This target sits above the name text ONLY and cannot intercept routing,
 // channel plugins, pan controls, mute/solo, meters or faders.
 s->nameHit=std::make_unique<MixerNameHitTarget>();
 auto* hit=s->nameHit.get();
 auto* strip=s.get();
 // This transparent hit target is not a TooltipClient; the Help hint
 // appears in the mixer toolbar without adding an overlapping child.
 hit->onDown=[this](const juce::MouseEvent& event)
 {
     mouseDown(event.getEventRelativeTo(this));
 };
 hit->onDrag=[this](const juce::MouseEvent& event)
 {
     mouseDrag(event.getEventRelativeTo(this));
 };
 hit->onUp=[this](const juce::MouseEvent& event)
 {
     mouseUp(event.getEventRelativeTo(this));
 };
 hit->onDouble=[this,strip](const juce::MouseEvent&)
 {
     if(renamingLane!=strip->lane||renamingKind!=strip->kind)
         beginRename(*strip);
 };
 addAndMakeVisible(*hit);
 wire(*s);return s;}
 void configureValuePopup(Strip& s){auto configure=[](juce::Slider* slider,const juce::String&suffix){if(!slider)return;slider->setPopupDisplayEnabled(true,true,nullptr,1500);slider->setTextValueSuffix(suffix);slider->setNumDecimalPlacesToDisplay(1);};configure(s.inputGain.get()," dB");configure(s.highPass.get()," Hz");configure(s.compThreshold.get()," dB");configure(s.compRatio.get()," :1");configure(s.gateThreshold.get()," dB");for(auto&knob:s.eqKnobs)configure(knob.get()," dB");}
 void refreshPower(Strip& s){if(s.kind!=Strip::audio)return;const bool states[3]={s.eqOn,s.gateOn,s.compOn};for(int i=0;i<3;++i){auto&button=s.modulePower[(size_t)i];if(!button)continue;button->setButtonText(states[i]?"ON":"OFF");button->setColour(juce::TextButton::buttonColourId,states[i]?juce::Colour(0xff246b63):juce::Colour(0xff34404a));}}
 void applyMixerSolo(Strip& strip)
 {
     if(strip.kind!=Strip::audio && strip.kind!=Strip::instrument)return;
     using namespace LibertyMixConsoleSoloLogic;
     const auto modifiers=juce::ModifierKeys::getCurrentModifiersRealtime();
     const Action action=resolveAction(modifiers.isShiftDown(),modifiers.isAltDown());
     State before;
     before.audio.resize(static_cast<size_t>(owner.getAudioTrackCount()));
     before.instruments.resize(static_cast<size_t>(owner.getInstrumentTrackCount()));
     for(size_t i=0;i<before.audio.size();++i)
         before.audio[i]=owner.audioEngine.isTrackSolo(static_cast<int>(i));
     for(size_t i=0;i<before.instruments.size();++i)
         before.instruments[i]=owner.audioEngine.isInstrumentTrackSolo(static_cast<int>(i));
     const auto kind=strip.kind==Strip::audio?Kind::audio:Kind::instrument;
     const auto after=apply(before,{kind,strip.lane},action);
     for(size_t i=0;i<before.audio.size();++i)
         if(before.audio[i]!=after.audio[i])
             owner.audioEngine.setTrackSolo(static_cast<int>(i),after.audio[i]);
     for(size_t i=0;i<before.instruments.size();++i)
         if(before.instruments[i]!=after.instruments[i])
             owner.audioEngine.setInstrumentTrackSolo(static_cast<int>(i),after.instruments[i]);
     sync();
     repaint();
 }
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
 };if(sp->kind==Strip::midi){s.fader->setEnabled(false);s.plugin->setEnabled(false);s.unload->setEnabled(false);return;}if(sp->kind==Strip::bus)
 {
     s.fader->onValueChange=[this,sp]{
         if(syncing)return;
         owner.audioEngine.setMixerBusGain(sp->lane,dbToGain((float)sp->fader->getValue()));
         repaint();
     };
     s.pan->onValueChange=[this,sp]{
         if(syncing)return;
         owner.audioEngine.setMixerBusPan(sp->lane,(float)sp->pan->getValue());
         repaint();
     };
     s.mute->onClick=[this,sp]{
         owner.audioEngine.setMixerBusMuted(sp->lane,
             !owner.audioEngine.isMixerBusMuted(sp->lane));
         sync();repaint();
     };
     s.solo->onClick=[this,sp]{
         owner.audioEngine.setMixerBusSolo(sp->lane,
             !owner.audioEngine.isMixerBusSolo(sp->lane));
         sync();repaint();
     };
     return;
 }
 s.fader->onValueChange=[this,sp]{if(syncing)return;float g=dbToGain((float)sp->fader->getValue());if(sp->kind==Strip::audio)owner.audioEngine.setTrackGain(sp->lane,g);else if(sp->kind==Strip::instrument)owner.audioEngine.setInstrumentTrackGain(sp->lane,g);else owner.audioEngine.setMasterGain(g);repaint();};if(s.pan)s.pan->onValueChange=[this,sp]{if(syncing)return;float p=(float)sp->pan->getValue();if(sp->kind==Strip::audio)owner.audioEngine.setTrackPan(sp->lane,p);else owner.audioEngine.setInstrumentTrackPan(sp->lane,p);repaint();};if(s.mute)s.mute->onClick=[this,sp]{if(sp->kind==Strip::audio)owner.audioEngine.setTrackMuted(sp->lane,!owner.audioEngine.isTrackMuted(sp->lane));else owner.audioEngine.setInstrumentTrackMuted(sp->lane,!owner.audioEngine.isInstrumentTrackMuted(sp->lane));sync();repaint();};if(s.solo)s.solo->onClick=[this,sp]{applyMixerSolo(*sp);};for(int section=0;section<3;++section)if(s.modulePower[(size_t)section])s.modulePower[(size_t)section]->onClick=[this,sp,section]{bool on=false;if(section==0){sp->eqOn=!sp->eqOn;on=sp->eqOn;float gains[3];for(int j=0;j<3;++j)gains[j]=owner.audioEngine.getChannelEqBandDb(sp->lane,j);owner.audioEngine.setChannelEq(sp->lane,on,gains[0],gains[1],gains[2]);}else if(section==1){sp->gateOn=!sp->gateOn;on=sp->gateOn;owner.audioEngine.setChannelGate(sp->lane,on,(float)sp->gateThreshold->getValue());}else{sp->compOn=!sp->compOn;on=sp->compOn;owner.audioEngine.setChannelCompressor(sp->lane,on,(float)sp->compThreshold->getValue(),(float)sp->compRatio->getValue());}auto&button=*sp->modulePower[(size_t)section];button.setButtonText(on?"ON":"OFF");button.setColour(juce::TextButton::buttonColourId,on?juce::Colour(0xff246b63):juce::Colour(0xff34404a));repaint();};for(int band=0;band<3;++band)if(s.eqKnobs[(size_t)band])s.eqKnobs[(size_t)band]->onValueChange=[this,sp,band]{if(syncing)return;float gains[3];for(int j=0;j<3;++j)gains[j]=owner.audioEngine.getChannelEqBandDb(sp->lane,j);gains[band]=(float)sp->eqKnobs[(size_t)band]->getValue();sp->eqOn=true;refreshPower(*sp);owner.audioEngine.setChannelEq(sp->lane,true,gains[0],gains[1],gains[2]);repaint();};if(s.gateThreshold)s.gateThreshold->onValueChange=[this,sp]{if(syncing)return;sp->gateOn=true;refreshPower(*sp);owner.audioEngine.setChannelGate(sp->lane,true,(float)sp->gateThreshold->getValue());repaint();};if(s.compThreshold)s.compThreshold->onValueChange=[this,sp]{if(syncing)return;sp->compOn=true;refreshPower(*sp);owner.audioEngine.setChannelCompressor(sp->lane,true,(float)sp->compThreshold->getValue(),(float)sp->compRatio->getValue());repaint();};if(s.compRatio)s.compRatio->onValueChange=[this,sp]{if(syncing)return;sp->compOn=true;owner.audioEngine.setChannelCompressor(sp->lane,true,(float)sp->compThreshold->getValue(),(float)sp->compRatio->getValue());repaint();};if(s.highPass)s.highPass->onValueChange=[this,sp]{if(syncing)return;owner.audioEngine.setChannelHighPassHz(sp->lane,(float)sp->highPass->getValue());repaint();};if(s.inputGain)s.inputGain->onValueChange=[this,sp]{if(syncing)return;owner.audioEngine.setChannelInputGainDb(sp->lane,(float)sp->inputGain->getValue());repaint();};if(s.channel)s.channel->onClick=[this,sp]{
   juce::DialogWindow::LaunchOptions options;
   options.dialogTitle="LIBERTY  |  CANAL AUDIO";
   options.dialogBackgroundColour=juce::Colour(0xff101b2a);
   options.content.setOwned(new LibertyChannelInspector(
       owner.audioEngine,sp->lane,name(*sp),
       [safe=juce::Component::SafePointer<MixConsoleView>(this)]{
           if(safe!=nullptr){safe->sync();safe->repaint();}
       }));
   options.componentToCentreAround=&owner;
   options.escapeKeyTriggersCloseButton=true;
   options.useNativeTitleBar=false;
   options.resizable=false;
   options.launchAsync();
 };s.plugin->onClick=[sp]{auto&h=LibertyPluginHost::instance();if(sp->kind==Strip::instrument&&h.hasInstrumentForTrack(sp->lane))h.showInstrumentEditorForTrack(sp->lane);};s.unload->onClick=[this,sp]{auto&h=LibertyPluginHost::instance();if(sp->kind==Strip::instrument){h.unloadInstrumentForTrack(sp->lane);owner.notifyInstrumentChanged();}sync();repaint();};if(sp->kind==Strip::audio||sp->kind==Strip::instrument)for(int i=0;i<8;++i){sp->fx[(size_t)i]->onClick=[this,sp,i]{
    auto& h=LibertyPluginHost::instance();
    auto& k=LibertyOneKnobManager::instance();
    const int key=sp->kind==Strip::audio?sp->lane*8+i:100000+sp->lane*8+i;
    if(k.hasEffect(key))k.showEditor(key);
    else if(sp->kind==Strip::audio&&h.hasEffectForTrackSlot(sp->lane,i))
        h.showEditorForTrackSlot(sp->lane,i);
    else if(sp->kind==Strip::instrument&&h.hasEffectForInstrumentTrackSlot(sp->lane,i))
        h.showEffectEditorForInstrumentTrackSlot(sp->lane,i);
    else
    {
        owner.selectedTrack=sp->kind==Strip::audio?sp->lane:
            owner.getAudioTrackCount()+owner.getMidiTrackCount()+sp->lane;
        showLibertyEffectBrowserForSlot(&owner,sp->kind==Strip::instrument,sp->lane,i);
    }
};sp->fxUnload[(size_t)i]->onClick=[this,sp,i]{auto&h=LibertyPluginHost::instance();auto&k=LibertyOneKnobManager::instance();if(sp->kind==Strip::audio){h.unloadEffectForTrackSlot(sp->lane,i);k.clearEffect(sp->lane*8+i);}else{h.unloadEffectForInstrumentTrackSlot(sp->lane,i);k.clearEffect(100000+sp->lane*8+i);}sync();repaint();};}}
 

 static juce::String stripKey(Strip::Kind kind,int lane)
 {
     return (kind==Strip::audio?"A":kind==Strip::midi?"M":
             kind==Strip::instrument?"I":kind==Strip::bus?"B":"X")
            +juce::String(lane);
 }
 void rebuild()
 {
   const int audio=owner.getAudioTrackCount();
   const int midi=owner.getMidiTrackCount();
   const int instrument=owner.getInstrumentTrackCount();
   // Follow ARRANGEMENT on first open. Saved console order is an override.
   owner.reconcileArrangeTrackOrder();
   std::vector<std::pair<Strip::Kind,int>> wanted;
   for(int row=0;row<audio+midi+instrument;++row)
   {
       const int logical=owner.getLogicalTrackAtDisplayRow(row);
       if(logical<audio)wanted.push_back({Strip::audio,logical});
       else if(logical<audio+midi)wanted.push_back({Strip::midi,logical-audio});
       else wanted.push_back({Strip::instrument,logical-audio-midi});
   }
   for(int busIndex=0;busIndex<owner.audioEngine.getMixerBusCount();++busIndex)
       wanted.push_back({Strip::bus,busIndex});
   if(!owner.mixerStripOrder.isEmpty())
   {
       std::vector<std::pair<Strip::Kind,int>> ordered;
       for(const auto& key:owner.mixerStripOrder)
       {
           const auto found=std::find_if(wanted.begin(),wanted.end(),
               [&](const auto& item){return stripKey(item.first,item.second)==key;});
           if(found!=wanted.end()&&std::find(ordered.begin(),ordered.end(),*found)==ordered.end())
               ordered.push_back(*found);
       }
       for(const auto& item:wanted)
           if(std::find(ordered.begin(),ordered.end(),item)==ordered.end())
               ordered.push_back(item);
       wanted.swap(ordered);
   }
   wanted.push_back({Strip::master,0}); // Master is fixed on the right.
   bool matches=wanted.size()==strips.size();
   for(std::size_t i=0;matches&&i<wanted.size();++i)
       matches=strips[i]->kind==wanted[i].first&&strips[i]->lane==wanted[i].second;
   if(matches)return;
   struct ViewSnapshot
   {
       std::array<bool,4> modules{};
       bool inserts=true;
       int firstFx=0;
       int faderHeight=320;
       bool instrumentLoaded=false;
   };
   std::map<std::pair<int,int>,ViewSnapshot> previous;
   for(const auto& item:strips)
       previous[{(int)item->kind,item->lane}]=
          {item->moduleExpanded,item->insertsExpanded,
           item->firstFxSlot,item->preferredFaderHeight,item->instrumentLoaded};
   const auto selectedKind=selected?selected->kind:Strip::master;
   const int selectedLane=selected?selected->lane:-1;
   clear();
   for(const auto& item:wanted)
   {
       auto strip=makeStrip(item.first,item.second);
       const auto found=previous.find({(int)item.first,item.second});
       if(found==previous.end()&&item.first==Strip::audio)
           strip->moduleExpanded.fill(detailedView);
       if(found!=previous.end())
       {
           strip->moduleExpanded=found->second.modules;
           strip->insertsExpanded=found->second.inserts;
           strip->firstFxSlot=found->second.firstFx;
           strip->preferredFaderHeight=found->second.faderHeight;
           strip->instrumentLoaded=found->second.instrumentLoaded;
       }
       if(item.first==Strip::audio)configureValuePopup(*strip);
       if(item.first==selectedKind&&item.second==selectedLane)selected=strip.get();
       strips.push_back(std::move(strip));
   }
   sync();layout();
 }
 using StripGeometry=LibertyMixerLayout::Geometry;
 std::array<bool,LibertyMixerProgressiveFx::slotLimit>
 effectOccupancy(const Strip& s) const
 {
     std::array<bool,LibertyMixerProgressiveFx::slotLimit> occupied{};
     if(s.kind!=Strip::audio&&s.kind!=Strip::instrument)return occupied;
     auto& host=LibertyPluginHost::instance();
     auto& one=LibertyOneKnobManager::instance();
     for(int slot=0;slot<LibertyMixerProgressiveFx::slotLimit;++slot)
     {
         const int key=s.kind==Strip::audio?s.lane*8+slot:100000+s.lane*8+slot;
         occupied[(size_t)slot]=one.hasEffect(key)||
             (s.kind==Strip::audio?host.hasEffectForTrackSlot(s.lane,slot)
                 :host.hasEffectForInstrumentTrackSlot(s.lane,slot));
     }
     return occupied;
 }
 int neededFxSlots(const Strip& s) const
 {
     return LibertyMixerProgressiveFx::revealedCount(effectOccupancy(s));
 }
 StripGeometry getStripGeometry(const Strip&s)const{
    const auto kind=s.kind==Strip::audio?LibertyMixerLayout::Kind::Audio:
        (s.kind==Strip::instrument||s.kind==Strip::bus)?LibertyMixerLayout::Kind::Instrument:
        s.kind==Strip::master?LibertyMixerLayout::Kind::Master:
        LibertyMixerLayout::Kind::Midi;
    return LibertyMixerLayout::calculate(s.bounds.getHeight(),kind,
        s.moduleExpanded,s.insertsExpanded,s.preferredFaderHeight,
        neededFxSlots(s));
 }
 void layout(){
    const auto view=LibertyNoOverlap::mixerViewButton(getWidth());
    detailButton.setBounds(view.x,view.y,view.w,view.h);
    const auto meter=LibertyNoOverlap::mixerMeterButton(getWidth());
    meterModeButton.setBounds(meter.x,meter.y,meter.w,meter.h);
    meterModeButton.setVisible(meter.w>0);
    const auto search=LibertyNoOverlap::mixerSearchField(getWidth());
    const auto input=LibertyNoOverlap::mixerSearchInput(getWidth());
    const auto clear=LibertyNoOverlap::mixerSearchClear(getWidth());
    searchBox.setBounds(input.x,input.y,input.w,input.h);
    searchBox.setVisible(input.w>0&&input.h>0);
    clearSearchButton.setBounds(clear.x,clear.y,clear.w,clear.h);
    clearSearchButton.setVisible(clear.w>0&&clear.h>0);
    if(search.w==0&&searchBox.getText().isNotEmpty())
    {
        searchBox.setText(juce::String(),false);
        lastObservedTrack=-99999;
    }
    const bool routingOpen=routingLane>=0;
    const auto nativeViewport=LibertyNoOverlap::scrollableChannels(
        getWidth(),getHeight(),routingOpen);
    const auto fixedMaster=LibertyNoOverlap::pinnedMaster(
        getWidth(),getHeight(),routingOpen);
    const int viewportHeight=juce::jmax(1,nativeViewport.h);
    const int viewportWidth=juce::jmax(1,nativeViewport.w);
    const juce::Rectangle<int> viewport(
        nativeViewport.x,nativeViewport.y,viewportWidth,viewportHeight);
    const juce::Rectangle<int> pinned(fixedMaster.x,fixedMaster.y,
                                      fixedMaster.w,fixedMaster.h);
    const int max=LibertyNoOverlap::channelMaxScroll(
        juce::jmax(0,(int)strips.size()-1),viewportWidth);
    offset=juce::jlimit(0,max,offset);
    int x=12-offset;
    for(auto& s:strips){
      const bool isMaster=s->kind==Strip::master;
      s->bounds=isMaster?pinned:juce::Rectangle<int>(x,topY,stripW-4,viewportHeight);
      const bool vis=isMaster||
          (s->bounds.getRight()>viewport.getX()&&s->bounds.getX()<viewport.getRight());
      const int needed=neededFxSlots(*s);
      s->lastFxCount=needed;
      const auto g=getStripGeometry(*s);
      const int bx=s->bounds.getX(),by=s->bounds.getY(),cx=s->bounds.getCentreX();
      auto put=[&](juce::Component* control,int rx,int ry,int w,int h,bool enabled=true){
          if(!control)return;
          const juce::Rectangle<int> r(bx+rx,by+ry,w,h);
          control->setBounds(r);
          control->setVisible(vis&&enabled&&h>0&&s->bounds.contains(r)&&
              (isMaster?pinned.contains(r):viewport.contains(r)));
      };
      // A unique hit rectangle for every track, including instruments.
      // Child handlers forward drag to the existing channel-order system.
      put(s->nameHit.get(),7,10,s->bounds.getWidth()-14,24);
      if(s->kind==Strip::audio){
        for(int index=0;index<4;++index){
          const auto& p=g.modules[(size_t)index];
          auto& b=s->moduleTabs[(size_t)index];
          if(b){
            put(b.get(),7,p.headerY,index==3?68:s->bounds.getWidth()-45,20);
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
        put(s->channel.get(),s->bounds.getWidth()-60,input.headerY+1,53,19,true);
        put(s->inputGain.get(),39,input.bodyY+4,28,inputKnob,input.bodyHeight>=25);
        put(s->highPass.get(),96,input.bodyY+4,28,inputKnob,input.bodyHeight>=25);
      }else{
        for(auto&b:s->moduleTabs)if(b)b->setVisible(false);
        for(auto&b:s->modulePower)if(b)b->setVisible(false);
        for(auto&b:s->eqKnobs)if(b)b->setVisible(false);
        for(auto* k:{s->gateThreshold.get(),s->compThreshold.get(),s->compRatio.get(),
                     s->inputGain.get(),s->highPass.get()})if(k)k->setVisible(false);
        if(s->channel)s->channel->setVisible(false);
      }
      if(s->kind==Strip::audio||s->kind==Strip::instrument||s->kind==Strip::bus){
        if(s->fxHeader){
          put(s->fxHeader.get(),7,g.insertsHeaderY,s->bounds.getWidth()-14,21);
          const int active=LibertyMixerProgressiveFx::activeCount(effectOccupancy(*s));
          juce::String text="EFFETS "+juce::String(active)+"/8";
          if(s->insertsExpanded){
            const int first=LibertyMixerProgressiveFx::windowStart(needed,g.visibleFx,s->firstFxSlot);
            s->firstFxSlot=first;
            if(g.visibleFx>0)text+="  |  "+juce::String(first+1)+"-"+juce::String(first+g.visibleFx);
          }
          if(s->kind==Strip::bus)text="BUS VERS MASTER";
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
          const bool populated=s->fxUnload[(size_t)i]&&
              s->fxUnload[(size_t)i]->isEnabled();
          // Give an empty insert the full width: no hidden X hitbox.
          put(s->fx[(size_t)i].get(),8,row,s->bounds.getWidth()-(populated?43:16),
              g.fxStep>0?g.fxStep-3:0,show);
          put(s->fxUnload[(size_t)i].get(),s->bounds.getWidth()-29,row,18,
              g.fxStep>0?g.fxStep-3:0,show&&populated);
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
      if(!isMaster)x+=stripW;
    }
    scroll.setBounds(12,getHeight()-24,juce::jmax(1,viewportWidth-24),16);
    scroll.setRangeLimits(0,juce::jmax(1,max+viewportWidth));
    scroll.setCurrentRange(offset,viewportWidth,juce::dontSendNotification);
    scroll.setVisible(max>0);
    verticalScroll.setVisible(false);
    if(renamingLane>=0)
    {
        const auto it=std::find_if(strips.begin(),strips.end(),
            [this](const auto& strip)
            {
                return strip->kind==renamingKind && strip->lane==renamingLane;
            });
        if(it!=strips.end())
        {
            const auto header=juce::Rectangle<int>(
                (*it)->bounds.getX()+7,(*it)->bounds.getY()+10,
                (*it)->bounds.getWidth()-14,24);
            renameEditor.setBounds(header);
            renameEditor.setVisible(viewport.contains(header));
        }
    }
    routeClose.setVisible(routingOpen);
    if(routingOpen)
    {
        const auto closeRect=LibertyNoOverlap::routingCloseButton(getWidth());
        routeClose.setBounds(closeRect.x,closeRect.y,closeRect.w,closeRect.h);
    }
    const int buses=owner.audioEngine.getMixerBusCount();
    const int destination=routingOpen?(routingKind==Strip::audio?
        owner.audioEngine.getTrackMixerBus(routingLane):
        owner.audioEngine.getInstrumentMixerBus(routingLane)):-1;
    for(int i=0;i<(int)routeChoices.size();++i)
    {
        auto& button=routeChoices[(size_t)i];
        const bool show=routingOpen&&(i==0||i<=buses);
        button.setVisible(show);
        if(!show)continue;
        const auto r=LibertyNoOverlap::routingOptionButton(getWidth(),i);
        button.setBounds(r.x,r.y,r.w,r.h);
        button.setButtonText(i==0?"MASTER (direct)":"BUS "+juce::String(i));
        button.setToggleState(destination==i-1,juce::dontSendNotification);
        button.setColour(juce::TextButton::buttonColourId,destination==i-1?
            ModernMixer::sage.darker(.4f):ModernMixer::panel);
    }
 }
 juce::String name(const Strip&s)const{if(s.kind==Strip::master)return owner.getMixerMasterName();if(s.kind==Strip::bus)return owner.getMixerBusName(s.lane);const int logical=s.kind==Strip::audio?s.lane:(s.kind==Strip::midi?owner.getAudioTrackCount()+s.lane:owner.getAudioTrackCount()+owner.getMidiTrackCount()+s.lane);const auto n=getLibertyTrackName(logical);if(n.isNotEmpty())return n;if(s.kind==Strip::audio)return "Audio "+juce::String(s.lane+1);if(s.kind==Strip::midi)return "MIDI "+juce::String(s.lane+1);return "Instrument "+juce::String(s.lane+1);}
 bool isSelected(const Strip&s)const{if(selected==&s)return true;if(s.kind==Strip::master||s.kind==Strip::bus)return false;const int logical=s.kind==Strip::audio?s.lane:(s.kind==Strip::midi?owner.getAudioTrackCount()+s.lane:owner.getAudioTrackCount()+owner.getMidiTrackCount()+s.lane);return owner.selectedTrack==logical;}void draw(juce::Graphics&g,Strip&s){
    if(s.bounds.isEmpty()||s.bounds.getRight()<0||s.bounds.getX()>getWidth())return;
    juce::Graphics::ScopedSaveState savedState(g);
    g.reduceClipRegion(s.bounds);
    const bool chosen=isSelected(s);
    const int x=s.bounds.getX(),y=s.bounds.getY(),w=s.bounds.getWidth(),h=s.bounds.getHeight();
    g.setColour(chosen?ModernMixer::selected:ModernMixer::strip);
    g.fillRoundedRectangle(s.bounds.toFloat(),6.f);
    if(s.kind==Strip::bus)
    {
        g.setColour((owner.getMixerBusColourId(s.lane)>0?
             trackColour(owner.getMixerBusColourId(s.lane)):
             juce::Colour(0xffad8bda)).withAlpha(.18f));
        g.fillRoundedRectangle(s.bounds.toFloat(),6.f);
    }
    else if(s.kind!=Strip::master){
      const int logical=s.kind==Strip::audio?s.lane:
          (s.kind==Strip::midi?owner.getAudioTrackCount()+s.lane:
           owner.getAudioTrackCount()+owner.getMidiTrackCount()+s.lane);
      const auto colourId=getLibertyTrackColourId(logical);
      const auto tint=trackColour(colourId);
      g.setColour(tint.withAlpha(colourId>0?.17f:.04f));
      g.fillRoundedRectangle(s.bounds.toFloat(),6.f);
      if(colourId>0)
      {
          // A visible colour cue, including for instrumental tracks.
          g.setColour(tint.withAlpha(.96f));
          g.fillRoundedRectangle((float)x+1.f,(float)y+9.f,4.f,
                                 (float)juce::jmax(0,h-18),2.f);
      }
    }
    g.setColour(chosen?ModernMixer::sage:ModernMixer::border);
    g.drawRoundedRectangle(s.bounds.toFloat(),6.f,chosen?2.f:1.f);
    g.setColour(s.kind==Strip::bus?
        (owner.getMixerBusColourId(s.lane)>0?trackColour(owner.getMixerBusColourId(s.lane)):juce::Colour(0xffad8bda)):
        s.kind==Strip::master?
        (owner.getMixerMasterColourId()>0?trackColour(owner.getMixerMasterColourId()):juce::Colour(0xffe4a85e)):
        trackColour(getLibertyTrackColourId(s.kind==Strip::audio?s.lane:
           s.kind==Strip::midi?owner.getAudioTrackCount()+s.lane:
           owner.getAudioTrackCount()+owner.getMidiTrackCount()+s.lane)));
    g.fillRoundedRectangle((float)x+1.f,(float)y+1.f,(float)w-2.f,4.f,2.f);
    g.setColour(ModernMixer::border.withAlpha(.35f));
    g.drawVerticalLine(x+w-1,(float)y+8.f,(float)(y+h-8));
    g.setColour(ModernMixer::text);
    g.setFont(juce::Font(12.f,juce::Font::bold));
    if(renamingLane!=s.lane||renamingKind!=s.kind)
        g.drawFittedText(name(s),x+6,y+14,w-12,20,juce::Justification::centred,1);
    g.setColour(ModernMixer::secondary);g.setFont(juce::Font(8.5f,juce::Font::bold));
    g.drawText(s.kind==Strip::audio?"PISTE AUDIO":s.kind==Strip::midi?"PISTE MIDI":
       s.kind==Strip::instrument?"INSTRUMENT VIRTUEL":
       s.kind==Strip::bus?juce::String::fromUTF8("GROUPE STÉRÉO"):
       juce::String::fromUTF8("MASTER STÉRÉO"),
       x+4,y+36,w-8,15,juce::Justification::centred);
    g.setColour(juce::Colour(0xff132224));
    g.fillRoundedRectangle((float)x+8,(float)y+56,(float)w-16,24.f,3.f);
    // The 24px route capsule holds phase correlation (left) and RMS balance
    // (right) without moving faders, meters or any processing module.
    if(s.kind==Strip::midi){
       g.setColour(juce::Colour(0xffd1dce7));g.setFont(juce::Font(8.5f));
       g.drawText("ROUTE > MASTER",x+10,y+58,w-20,18,
                  juce::Justification::centred,true);
    }else{
       const auto balance=LibertyMixConsoleMeter::stereoRmsBalance(s.meter);
       juce::String label="BAL --";
       if(balance.side==LibertyMixConsoleMeter::BalanceSide::center)
           label=juce::String::fromUTF8("Δ 0.0 dB");
       else if(balance.side==LibertyMixConsoleMeter::BalanceSide::left||
               balance.side==LibertyMixConsoleMeter::BalanceSide::right){
           const auto side=balance.side==LibertyMixConsoleMeter::BalanceSide::left
               ?"L +":"R +";
           label=juce::String(side)+juce::String(balance.differenceDb,1)+" dB";
       }
       g.setColour(juce::Colour(0xffd1dce7));
       g.setFont(juce::Font(8.f,juce::Font::bold));
       juce::String routeLabel=s.kind==Strip::master?"OUT 1-2":
           s.kind==Strip::bus?"BUS > MASTER":"VERS MASTER";
       if(s.kind==Strip::audio||s.kind==Strip::instrument)
       {
           const int destination=s.kind==Strip::audio?
               owner.audioEngine.getTrackMixerBus(s.lane):
               owner.audioEngine.getInstrumentMixerBus(s.lane);
           routeLabel=destination<0?"VERS MASTER":"VERS BUS "+juce::String(destination+1);
       }
       g.drawFittedText(routeLabel,
                        x+10,y+57,67,10,juce::Justification::centredLeft,1);
       const auto corr=s.correlation;
       const auto colour=!corr.valid?juce::Colour(0xff8b9ca8):
           corr.value<0.f?juce::Colour(0xffff7777):
           corr.value<0.3f?juce::Colour(0xfff0bc80):
                             juce::Colour(0xff7fe0e5);
       const juce::String corrLabel=!corr.valid?"COR --":
           juce::String("COR ")+(corr.value>=0.f?"+":"")+juce::String(corr.value,2);
       g.setColour(colour);
       g.fillEllipse((float)x+11.f,(float)y+71.f,4.f,4.f);
       g.setFont(juce::Font(7.8f,juce::Font::bold));
       g.drawFittedText(corrLabel,x+19,y+68,56,10,
                        juce::Justification::centredLeft,1);
       g.setFont(juce::Font(8.f,juce::Font::bold));
       g.setColour(balance.side==LibertyMixConsoleMeter::BalanceSide::left
           ?juce::Colour(0xff7fe0e5):
           balance.side==LibertyMixConsoleMeter::BalanceSide::right
           ?juce::Colour(0xfff0bc80):juce::Colour(0xffd1dce7));
       g.drawFittedText(label,x+77,y+57,60,15,
                        juce::Justification::centredRight,1);
       // A miniature -24..+24 dB balance pointer below the text.
       g.setColour(juce::Colour(0xff4f6572));
       g.drawLine((float)x+81.f,(float)y+77.f,
                  (float)x+135.f,(float)y+77.f,1.f);
       g.setColour(ModernMixer::secondary);
       g.drawLine((float)x+108.f,(float)y+74.f,
                  (float)x+108.f,(float)y+79.f,1.f);
       if(balance.side!=LibertyMixConsoleMeter::BalanceSide::silent){
           const float dot=(float)x+108.f+27.f*balance.position;
           g.setColour(balance.side==LibertyMixConsoleMeter::BalanceSide::left
               ?juce::Colour(0xff7fe0e5):
               balance.side==LibertyMixConsoleMeter::BalanceSide::right
               ?juce::Colour(0xfff0bc80):juce::Colour(0xffd1dce7));
           g.fillEllipse(dot-2.f,(float)y+75.f,4.f,4.f);
       }
    }
    const auto geom=getStripGeometry(s);
    auto panel=[&](int top,int height,juce::Colour accent){
      if(height<=3)return;
      const auto r=juce::Rectangle<float>((float)x+5,(float)(y+top),
                        (float)w-10,(float)height);
      g.setColour(ModernMixer::panel);
      g.fillRoundedRectangle(r,3.f);
      g.setColour(accent.withAlpha(.38f));
      g.drawRoundedRectangle(r,3.f,.8f);
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
                juce::String::fromUTF8(band==0?"GRAVES":band==1?"MÉDIUMS":"AIGUS"),
                x+8+band*44,labelY,42,10,juce::Justification::centred);
            }else if(index==1)g.drawText("SEUIL",x+49,labelY,50,10,juce::Justification::centred);
            else if(index==2){
              g.drawText("SEUIL",x+22,labelY,42,10,juce::Justification::centred);
              g.drawText("RATIO",x+80,labelY,42,10,juce::Justification::centred);
            }else{
              g.drawText("GAIN",x+35,labelY,35,10,juce::Justification::centred);
              g.drawText("COUPE-BAS",x+88,labelY,45,10,juce::Justification::centred);
            }
          }
        }
      }
    }
    if(s.kind==Strip::audio||s.kind==Strip::instrument){
      panel(geom.insertsHeaderY-1,23,ModernMixer::sage);
      if(s.insertsExpanded&&geom.visibleFx>0)
        panel(geom.fxY-2,geom.fxStep*geom.visibleFx+3,ModernMixer::sage);
      // MorningDAW-style channel overview: show only loaded inserts
      // while the full eight-slot panel is collapsed. Bounds end ABOVE pan.
      if(!s.insertsExpanded)
      {
          const int areaTop=geom.insertsHeaderY+28;
          const int areaBottom=geom.splitterY-5;
          const int available=areaBottom-areaTop;
          if(available>=52)
          {
              const auto overview=juce::Rectangle<float>(
                  (float)x+9.f,(float)y+(float)areaTop,
                  (float)w-18.f,(float)available);
              g.setColour(juce::Colour(0xff182728));
              g.fillRoundedRectangle(overview,3.f);
              g.setColour(ModernMixer::border.withAlpha(.43f));
              g.drawRoundedRectangle(overview,3.f,.7f);
              g.setColour(ModernMixer::secondary);
              g.setFont(juce::Font(8.f,juce::Font::bold));
              g.drawFittedText(juce::String::fromUTF8("CHAÎNE D'EFFETS"),x+15,y+areaTop+7,w-30,12,
                               juce::Justification::centredLeft,1);
              int shown=0;
              const auto occupied=effectOccupancy(s);
              const int loaded=LibertyMixerProgressiveFx::activeCount(occupied);
              const int maxLines=juce::jmax(0,juce::jmin(3,(available-34)/18));
              for(int slot=0;slot<LibertyMixerProgressiveFx::slotLimit;++slot)
              {
                  if(!occupied[(size_t)slot]||!s.fx[(size_t)slot])continue;
                  if(shown>=maxLines)continue;
                  const auto label=juce::String("FX ")+juce::String(slot+1)+
                      " : "+s.fx[(size_t)slot]->getButtonText();
                  g.setColour(ModernMixer::sage);
                  g.fillEllipse((float)x+16.f,(float)(y+areaTop+27+shown*18),4.f,4.f);
                  g.setColour(ModernMixer::text);
                  g.setFont(juce::Font(9.f));
                  g.drawFittedText(label,x+25,y+areaTop+23+shown*18,w-42,15,
                                   juce::Justification::centredLeft,1);
                  ++shown;
              }
              if(loaded==0)
              {
                  g.setColour(ModernMixer::secondary.withAlpha(.65f));
                  g.setFont(juce::Font(9.f));
                  g.drawFittedText("AUCUN INSERT ACTIF",x+15,y+areaTop+25,w-30,16,
                                   juce::Justification::centredLeft,1);
              }
              else if(loaded>shown&&shown>0)
              {
                  g.setColour(ModernMixer::sage);
                  g.setFont(juce::Font(8.f,juce::Font::bold));
                  g.drawFittedText("+"+juce::String(loaded-shown)+" FX",
                      x+w-49,y+areaTop+7,36,12,juce::Justification::centredRight,1);
              }
          }
      }
      panel(geom.panY-2,38,ModernMixer::sage);
      panel(geom.muteY-2,30,ModernMixer::sage);
      g.setColour(juce::Colour(0xff6a9eb9));
      g.fillRoundedRectangle((float)x+12,(float)(y+geom.splitterY),
                             (float)w-24,3.f,1.5f);
      g.setColour(juce::Colour(0xffbed4e0));
      g.fillRoundedRectangle((float)x+(float)w*.5f-16.f,
                             (float)(y+geom.splitterY-1),32.f,5.f,2.f);
    }else if(s.kind==Strip::bus){
      g.setColour(juce::Colour(0xffbda1e2));g.setFont(juce::Font(8.f,juce::Font::bold));
      g.drawText("GROUP BUS",x,y+92,w,18,juce::Justification::centred);
    }else if(s.kind==Strip::master){
      g.setColour(juce::Colour(0xffb5c3d1));g.setFont(juce::Font(8.f,juce::Font::bold));
      g.drawText("MASTER FIXE",x,y+92,w,18,juce::Justification::centred);
    }
    if(s.kind!=Strip::midi){
      panel(geom.faderY-3,h-geom.faderY+1,ModernMixer::amber);
      drawMeter(g,s);
      const int meterTop=y+geom.faderY,meterBottom=s.bounds.getBottom()-36;
      if(meterBottom-meterTop>=40){
        g.setColour(juce::Colour(0xffb5c3d1));g.setFont(juce::Font(8.f));
        // Correct dBFS graduation, not evenly spaced amplitude labels.
        // Keep sparse labels when a collapsed mixer provides less height.
        const int ticks[]={0,-18,-36,-60};
        for(int i=0;i<4;++i){
          if(meterBottom-meterTop<110 && (i==1||i==2))continue;
          const float fraction=(ticks[i]+60.f)/66.f;
          const int yy=meterBottom-(int)std::round((meterBottom-meterTop)*fraction);
          g.drawText(juce::String(ticks[i]),s.bounds.getRight()-30,
                     yy-6,22,12,juce::Justification::right);
        }
      }
    }
 }
 void drawMeter(juce::Graphics&g,const Strip&s)
 {
     if(s.kind==Strip::midi)return;
     const int top=s.bounds.getY()+getStripGeometry(s).faderY;
     const int bottom=s.bounds.getBottom()-36;
     if(bottom-top<40)return;
     // Stereo twin bars, no change to existing fader/control geometry.
     const float x=(float)s.bounds.getX()+8.f;
     const auto frame=juce::Rectangle<float>(x,(float)top,21.f,(float)(bottom-top));
     g.setColour(juce::Colour(0xff0b1721));
     g.fillRoundedRectangle(frame,2.f);
     g.setColour(juce::Colour(0xff3e596e));
     g.drawRoundedRectangle(frame,2.f,1.f);
     for(int channel=0;channel<2;++channel)
     {
         const auto& value=s.meter.channel[(size_t)channel];
         const auto bar=juce::Rectangle<float>(
             x+1.f+10.f*(float)channel,(float)top+1.f,8.f,
             (float)(bottom-top-2));
         const float amount=LibertyMixConsoleMeter::meterPosition(value.level);
         if(amount>0.f)
         {
             auto fill=bar;
             fill.setY(bar.getBottom()-bar.getHeight()*amount);
             fill.setHeight(bar.getHeight()*amount);
             juce::ColourGradient gradient(
                 juce::Colour(0xff43cdbd),bar.getX(),bar.getBottom(),
                 juce::Colour(0xffff5959),bar.getX(),bar.getY(),false);
             gradient.addColour(0.68,juce::Colour(0xffe1ba57));
             gradient.addColour(0.91,juce::Colour(0xfff08c49));
             g.setGradientFill(gradient);
             g.fillRect(fill);
         }
         // Actual RMS level: narrow cyan centre inside each peak bar.
         const float rmsAmount=LibertyMixConsoleMeter::meterPosition(value.rms);
         if(rmsAmount>0.f)
         {
             const auto rmsBar=juce::Rectangle<float>(
                 bar.getX()+2.f,
                 bar.getBottom()-bar.getHeight()*rmsAmount,
                 4.f,bar.getHeight()*rmsAmount);
             g.setColour(juce::Colour(0xff8cf4fb));
             g.fillRect(rmsBar);
         }
         if(value.heldPeak>0.f)
         {
             const float y=bar.getBottom()-bar.getHeight()*
                 LibertyMixConsoleMeter::meterPosition(value.heldPeak);
             g.setColour(value.heldPeak>=1.f?
                 juce::Colour(0xffff6565):juce::Colour(0xfff2f6f8));
             g.fillRect((int)bar.getX(),(int)std::round(y)-1,
                        (int)bar.getWidth(),2);
         }
     }
     if(s.meter.clipped())
     {
         g.setColour(juce::Colour(0xffff4e4e));
         g.fillEllipse(frame.getRight()+2.f,frame.getY()+2.f,7.f,7.f);
     }
     // Each stereo channel has its own dBFS number: PEAK is the held
     // sample peak, RMS the last measured interval (not its visual tail).
     // The global PEAK/RMS toggle and persistent per-channel CLIP remain.
     const bool isPeak=meterReadoutMode==LibertyMixConsoleMeter::ReadoutMode::peak;
     const auto labelBox=juce::Rectangle<float>(x-4.f,(float)bottom+3.f,39.f,24.f);
     g.setColour(juce::Colour(0xff182b3a));
     g.fillRoundedRectangle(labelBox,2.f);
     g.setColour(isPeak?juce::Colour(0xffd8b66e):juce::Colour(0xff8cf4fb));
     g.drawRoundedRectangle(labelBox,2.f,1.f);
     g.setFont(juce::Font(7.f,juce::Font::bold));
     for(int channel=0;channel<2;++channel)
     {
         const float amplitude=LibertyMixConsoleMeter::channelReadoutAmplitude(
             s.meter,meterReadoutMode,channel);
         const auto value=amplitude>0.f
             ?juce::String(LibertyMixConsoleMeter::toDb(amplitude),1)
             :juce::String("--");
         const auto label=juce::String(channel==0?"L ":"R ")+value;
         g.setColour(s.meter.channel[(size_t)channel].clipped
             ?juce::Colour(0xffff7777):juce::Colour(0xffe6f2f8));
         g.drawFittedText(label,(int)x-4,bottom+4+channel*11,39,11,
                          juce::Justification::centred,1);
     }
     g.setColour(isPeak?juce::Colour(0xffd8b66e):
                         juce::Colour(0xff8cf4fb));
     g.setFont(juce::Font(6.f,juce::Font::bold));
     g.drawFittedText(isPeak?"PEAK dBFS":"RMS dBFS",
                      (int)x-4,bottom+28,39,8,
                      juce::Justification::centred,1);
 }

 void updateMeters()
 {
     // Consume each rendered channel independently at the existing 24 Hz.
     for(auto& strip:strips)
     {
         std::array<float,2> peaks{{0.f,0.f}};
         std::array<float,2> rms{{0.f,0.f}};
         if(strip->kind==Strip::audio)
         {
             peaks=owner.audioEngine.consumeTrackStereoMeterPeaks(strip->lane);
             rms=owner.audioEngine.consumeTrackStereoMeterRms(strip->lane);
             strip->correlation.advance(owner.audioEngine.consumeTrackStereoCorrelation(strip->lane));
         }
         else if(strip->kind==Strip::instrument)
         {
             peaks=owner.audioEngine.consumeInstrumentStereoMeterPeaks(strip->lane);
             rms=owner.audioEngine.consumeInstrumentStereoMeterRms(strip->lane);
             strip->correlation.advance(owner.audioEngine.consumeInstrumentStereoCorrelation(strip->lane));
         }
         else if(strip->kind==Strip::bus)
         {
             peaks=owner.audioEngine.consumeMixerBusPeaks(strip->lane);
             rms=owner.audioEngine.consumeMixerBusRms(strip->lane);
             strip->correlation.advance(owner.audioEngine.consumeMixerBusCorrelation(strip->lane));
         }
         else if(strip->kind==Strip::master)
         {
             peaks=owner.audioEngine.consumeMasterStereoMeterPeaks();
             rms=owner.audioEngine.consumeMasterStereoMeterRms();
             strip->correlation.advance(owner.audioEngine.consumeMasterStereoCorrelation());
         }
         else
             continue;
         LibertyMixConsoleMeter::advance(strip->meter,peaks,rms);
     }
 }
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
 s->plugin->setButtonText("AUDIO");s->plugin->setEnabled(false);for(int i=0;i<8;++i){const bool has=h.hasEffectForTrackSlot(s->lane,i);const int oneKey=s->lane*8+i;const bool hasOne=k.hasEffect(oneKey);const auto label=has?h.getEffectNameForTrackSlot(s->lane,i):(hasOne?k.getName(oneKey):"FX "+juce::String(i+1)+"  + CHARGER");if(s->fx[(size_t)i]->getButtonText()!=label)s->fx[(size_t)i]->setButtonText(label);if(s->fxUnload[(size_t)i]->isEnabled()!=(has||hasOne))s->fxUnload[(size_t)i]->setEnabled(has||hasOne);}{const bool m=owner.audioEngine.isTrackMuted(s->lane),so=owner.audioEngine.isTrackSolo(s->lane);s->mute->setToggleState(m,juce::dontSendNotification);s->solo->setToggleState(so,juce::dontSendNotification);s->mute->setColour(juce::TextButton::buttonColourId,m?juce::Colour(0xffa44141):juce::Colour(0xff25323e));s->solo->setColour(juce::TextButton::buttonColourId,so?juce::Colour(0xffa18432):juce::Colour(0xff25323e));}}else if(s->kind==Strip::midi){s->fader->setValue(-60.0,juce::dontSendNotification);s->fader->setTextValueSuffix("");s->plugin->setButtonText("MIDI");s->plugin->setEnabled(false);s->unload->setEnabled(false);}else if(s->kind==Strip::instrument){s->fader->setEnabled(true);s->plugin->setEnabled(true);s->fader->setTextValueSuffix(" dB");s->fader->setValue(gainToDb(owner.audioEngine.getInstrumentTrackGain(s->lane)),juce::dontSendNotification);s->pan->setValue(owner.audioEngine.getInstrumentTrackPan(s->lane),juce::dontSendNotification);bool ho=h.hasInstrumentForTrack(s->lane);
 // Loading the instrument does not use an FX insert. Make the first empty FX
 // slot visible immediately, even if the rack was previously collapsed.
 // Preserve a manual collapse while the same instrument remains loaded.
 if(LibertyMixerProgressiveFx::revealOnInstrumentLoad(s->instrumentLoaded,ho))
 {
     s->insertsExpanded=true;
     s->firstFxSlot=0;
 }
 s->instrumentLoaded=ho;
 juce::String n=ho?h.getInstrumentNameForTrack(s->lane):"INSTRUMENT - EMPTY";s->plugin->setButtonText(n);s->unload->setEnabled(ho);for(int i=0;i<8;++i){const bool has=h.hasEffectForInstrumentTrackSlot(s->lane,i);const int oneKey=100000+s->lane*8+i;const bool hasOne=k.hasEffect(oneKey);const auto label=has?h.getEffectNameForInstrumentTrackSlot(s->lane,i):(hasOne?k.getName(oneKey):"FX "+juce::String(i+1)+"  + CHARGER");if(s->fx[(size_t)i]->getButtonText()!=label)s->fx[(size_t)i]->setButtonText(label);if(s->fxUnload[(size_t)i]->isEnabled()!=(has||hasOne))s->fxUnload[(size_t)i]->setEnabled(has||hasOne);}{const bool m=owner.audioEngine.isInstrumentTrackMuted(s->lane),so=owner.audioEngine.isInstrumentTrackSolo(s->lane);s->mute->setToggleState(m,juce::dontSendNotification);s->solo->setToggleState(so,juce::dontSendNotification);s->mute->setColour(juce::TextButton::buttonColourId,m?juce::Colour(0xffa44141):juce::Colour(0xff25323e));s->solo->setColour(juce::TextButton::buttonColourId,so?juce::Colour(0xffa18432):juce::Colour(0xff25323e));}}else if(s->kind==Strip::bus){
     s->fader->setEnabled(true);
     s->fader->setTextValueSuffix(" dB");
     s->fader->setValue(gainToDb(owner.audioEngine.getMixerBusGain(s->lane)),juce::dontSendNotification);
     s->pan->setValue(owner.audioEngine.getMixerBusPan(s->lane),juce::dontSendNotification);
     const bool muted=owner.audioEngine.isMixerBusMuted(s->lane);
     const bool solo=owner.audioEngine.isMixerBusSolo(s->lane);
     s->mute->setToggleState(muted,juce::dontSendNotification);
     s->solo->setToggleState(solo,juce::dontSendNotification);
     s->mute->setColour(juce::TextButton::buttonColourId,
         muted?juce::Colour(0xffa44141):juce::Colour(0xff25323e));
     s->solo->setColour(juce::TextButton::buttonColourId,
         solo?juce::Colour(0xffa18432):juce::Colour(0xff25323e));
 }else{s->fader->setEnabled(true);s->fader->setTextValueSuffix(" dB");s->fader->setValue(gainToDb(owner.audioEngine.getMasterGain()),juce::dontSendNotification);s->plugin->setButtonText("MASTER OUTPUT");s->plugin->setEnabled(false);s->unload->setEnabled(false);}}
 // Occupied inserts stand out from the next empty slot, on both audio
 // and instrument channels. Existing FX indices and DSP are untouched.
 for(const auto& strip:strips)
 {
     if(strip->kind!=Strip::audio&&strip->kind!=Strip::instrument)continue;
     const auto occupied=effectOccupancy(*strip);
     for(int slot=0;slot<LibertyMixerProgressiveFx::slotLimit;++slot)
     {
         auto* button=strip->fx[(size_t)slot].get();
         if(!button)continue;
         const bool loaded=occupied[(size_t)slot];
         button->setColour(juce::TextButton::buttonColourId,
             loaded?ModernMixer::sage.darker(.65f):ModernMixer::panel);
         button->setTooltip(loaded
             ?juce::String::fromUTF8("Cliquer pour ouvrir cet effet chargé.")
             :juce::String::fromUTF8("Cliquer pour charger un effet dans ce slot."));
     }
 }
 syncing=false;
 }
 void timerCallback()override
 {
     if(!visible)return;
     updateMeters();
     followArrangementSelection();
     bool fxCountChanged=false;
     auto& host=LibertyPluginHost::instance();
     for(const auto& strip:strips)
     {
         if((strip->kind==Strip::audio||strip->kind==Strip::instrument)&&
             strip->lastFxCount!=neededFxSlots(*strip))
             fxCountChanged=true;
         // An instrument may be loaded from the browser, drag & drop or
         // project restore. None of those is necessarily an FX-slot change.
         if(strip->kind==Strip::instrument &&
            strip->instrumentLoaded!=host.hasInstrumentForTrack(strip->lane))
             fxCountChanged=true;
     }
     if(fxCountChanged)
     {
         sync();layout();repaint();
         return;
     }
     for(auto& strip:strips)
         if(!strip->bounds.isEmpty() && strip->kind!=Strip::midi){
             // Refresh only the 24px route/correlation/balance capsule.
             // Faders and other channel modules are never repainted.
             repaint(strip->bounds.getX()+8,strip->bounds.getY()+56,
                     strip->bounds.getWidth()-16,24);
             const int top=strip->bounds.getY()+getStripGeometry(*strip).faderY;
             const int bottom=strip->bounds.getBottom()-36;
             if(bottom-top>=40)
                 repaint(strip->bounds.getX()+3,top,44,bottom-top+35);
         }
 }
 LibertyMixConsoleMeter::ReadoutMode meterReadoutMode=
     LibertyMixConsoleMeter::ReadoutMode::peak;
 Strip*draggingHeight=nullptr;Strip*draggingStrip=nullptr;int dropStripIndex=-1;
 int dragStartY=0,dragStartPreference=0;
 MainComponent&owner;ConsoleLookAndFeel lookAndFeel;
 juce::TextButton routeClose;
 std::array<juce::TextButton,9> routeChoices;
 juce::TextButton detailButton;
 juce::TextButton meterModeButton;
 bool detailedView=false;
 juce::TextEditor searchBox;
 juce::TextButton clearSearchButton;
 int lastObservedTrack=-99999;
 juce::TextEditor renameEditor;
 int renamingLane=-1;
 Strip::Kind renamingKind=Strip::audio;
 int routingLane=-1;
 Strip::Kind routingKind=Strip::audio;
 std::vector<std::unique_ptr<Strip>>strips;juce::ScrollBar scroll{false};juce::ScrollBar verticalScroll{true};int verticalOffset=0;int offset=0;bool syncing=false,visible=false;Strip*selected=nullptr;
};
// The page tab in PageModeCoordinator is the only mixer navigation button.
// Do not create duplicate controls over the transport.
class MixConsoleController final{
public:
 explicit MixConsoleController(MainComponent&o):owner(o),view(o){}
 ~MixConsoleController(){shutdown();}
 void shutdown(){if(stopped.exchange(true))return;shown=false;view.setConsoleVisible(false);}
 void show(bool b)
 {
     if(stopped.load())return;
     shown=b;
     view.setConsoleVisible(b);
     if(b)
     {
         resizeToOwner();
         view.toFront(true);
     }
     owner.repaint();
 }
 bool isVisible()const{return shown;}
 void refresh(){if(stopped.load())return;view.refreshStatic();}
 void resizeToOwner()
 {
     if(stopped.load()||!shown)return;
     const int available=juce::jmax(1,owner.getWidth()-
         getLibertyBrowserReservedWidth(&owner));
     const auto target=juce::Rectangle<int>(0,transportHeight,available,
         juce::jmax(1,owner.getHeight()-transportHeight));
     if(view.getBounds()!=target)view.setBounds(target);
 }
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
