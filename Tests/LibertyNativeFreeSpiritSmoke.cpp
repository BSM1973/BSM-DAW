#include <JuceHeader.h>
#include "PluginHost.h"
#include "OneKnobEffects.h"
#include "Plugin.h"
#include "PluginProcessor.h"
#include "FactoryPresets.h"
#include "LibertyManufacturerGrouping.h"
#include "LibertyBrowserIntelligence.h"
#include "LibertyPresetCenter.h"
#include "LibertyDeviceChains.h"
#include <limits>
#include <algorithm>
#include <vector>
#include <cmath>
#include <iostream>
#include <cstdlib>
static void check(bool ok,const char* what)
{
    if(!ok){std::cerr<<"FAIL: "<<what<<'\n';std::exit(1);}
}
int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto tags=LibertyBrowserIntelligence::parseTags("Tape, vintage; TAPE ; vocal");
    check(tags.size()==3&&tags.contains("tape",true),"tag normalization and duplicate rejection");
    check(LibertyBrowserIntelligence::matches("FreeSpirit Delay Tape Vintage","delay vintage"),"multi-token search");
    check(!LibertyBrowserIntelligence::matches("FreeSpirit Delay","delay piano"),"all terms required");
    using LibertyManufacturerGrouping::canonicalName;
    check(canonicalName("Liberty")=="Liberty Music Software","legacy Liberty folder alias");
    check(canonicalName("  LIBERTY  ")=="Liberty Music Software","case and whitespace alias");
    check(canonicalName("Liberty Music Software")=="Liberty Music Software","canonical manufacturer");
    check(canonicalName("Liberty Audio")=="Liberty Audio","unrelated manufacturer unchanged");
    check(canonicalName("Steinberg")=="Steinberg","third party unchanged");
    std::vector<juce::String> makers{"Liberty", "Liberty Audio", "Liberty Music Software"};
    std::sort(makers.begin(),makers.end(),[](const auto& a,const auto& b)
        {return canonicalName(a).compareNatural(canonicalName(b),false)<0;});
    check(canonicalName(makers[1])==canonicalName(makers[2]),"canonical sorting keeps aliases adjacent");
    makers = {"Steinberg","Native Instruments","Liberty","Arturia","Liberty Music Software"};
    std::sort(makers.begin(),makers.end(),[](const auto& a,const auto& b)
        {return LibertyManufacturerGrouping::compareManufacturers(a,b)<0;});
    check(LibertyManufacturerGrouping::isPinned(makers[0]) &&
          LibertyManufacturerGrouping::isPinned(makers[1]),"Liberty aliases pinned first");
    check(makers[2]=="Arturia" && makers[3]=="Native Instruments" && makers[4]=="Steinberg",
          "remaining manufacturers alphabetic");
    check(!LibertyManufacturerGrouping::isPinned("Liberty Audio"),"unrelated brand never pinned");
    auto& host=LibertyPluginHost::instance();
    host.initialise(48000,128);
    host.setNativeTempo(96);
    juce::PluginDescription d;
    check(host.findKnownPluginByIdentifier("liberty.native.freespirit-delay",d),"native lookup without scan");
    check(d.pluginFormatName=="Liberty Native" && !d.isInstrument,"native identity");
    check(canonicalName(d.manufacturerName)=="Liberty Music Software","FreeSpirit native grouped under official brand");
    int found=0;
    for(const auto& candidate:host.getPluginDescriptions())
        if(candidate.fileOrIdentifier==d.fileOrIdentifier)++found;
    check(found==1,"native browser entry unique");
    juce::String error;
    check(host.loadEffectForTrackSlot(0,0,d,error),"audio insert load");
    check(host.loadEffectForInstrumentTrackSlot(0,0,d,error),"instrument insert load");
    check(!host.loadInstrumentForTrack(1,d,error),"cannot use effect as instrument");
    auto wrong=d;wrong.fileOrIdentifier="liberty.native.horizon-reverb";
    check(!host.loadEffectForTrackSlot(0,0,wrong,error),"unregistered native rejected");
    check(host.hasEffectForTrackSlot(0,0),"failed load preserves current effect");
    FreeSpiritProcessor original;
    check(original.presets.size()==100,"all 100 original presets");
    const auto catalogue=host.getNativeFactoryPresets();check(catalogue.size()==200,"native preset center exact catalogue");
    juce::MemoryBlock preset;check(host.getNativeFactoryPresetState(d.fileOrIdentifier,0,preset),"factory preset state");
    check(host.validateNativePresetState(d.fileOrIdentifier,preset),"validated factory state");
    check(host.loadEffectForTrackSlot(1,0,d,error,&preset),"atomic preset insert");
    juce::MemoryBlock rejected;check(!host.loadEffectForTrackSlot(1,0,d,error,&rejected),"invalid preset rejected before replacement");
    check(host.hasEffectForTrackSlot(1,0),"invalid preset preserves instance");
    check(!host.getNativeFactoryPresetState(d.fileOrIdentifier,100,rejected),"invalid factory index");
    LibertyPluginHost::NativeChain chain;
    check(host.captureNativeChain(false,1,chain,error)&&chain.size()==1,"capture native chain");
    auto second=chain.front();second.slot=3;chain.push_back(second);
    check(host.insertNativeChain(false,2,chain,error),"atomic chain insertion");
    check(host.hasEffectForTrackSlot(2,0)&&host.hasEffectForTrackSlot(2,3)&&!host.hasEffectForTrackSlot(2,1),"exact chain positions");
    check(!host.insertNativeChain(false,2,chain,error),"occupied destination rejected");
    auto& oneKnob=LibertyOneKnobManager::instance();
    oneKnob.setEffect(4*8,LibertyOneKnobRack::Type::delay);
    check(!host.insertNativeChain(false,4,chain,error),"One Knob destination protected");
    check(oneKnob.hasEffect(4*8)&&!host.hasEffectForTrackSlot(4,0),"existing One Knob preserved");
    LibertyPluginHost::NativeChain blocked;
    check(!host.captureNativeChain(false,4,blocked,error)&&blocked.empty(),"unsupported One Knob capture rejected");
    oneKnob.clearAllEffects();
    auto broken=chain;broken[1].state.reset();
    check(!host.insertNativeChain(false,3,broken,error)&&!host.hasEffectForTrackSlot(3,0),"invalid chain never partially published");
    check(host.insertNativeChain(true,2,chain,error),"instrument track FX chain");
    check(host.hasEffectForInstrumentTrackSlot(2,3),"instrument slot restored");
    juce::AudioBuffer<float> auditionInput(2,4800);auditionInput.clear();
    for(int ch=0;ch<2;++ch)auditionInput.setSample(ch,100,.25f);
    LibertyPluginHost::AuditionResult audition;std::atomic<bool> cancel{false};
    juce::MemoryBlock beforeAudition,afterAudition;check(host.getEffectStateForTrackSlot(2,0,beforeAudition),"snapshot before audition");
    check(host.renderNativeAudition(chain,auditionInput,48000.,120.,audition,error,cancel),"isolated audition render");
    check(audition.dry.getNumSamples()==audition.wet.getNumSamples()&&audition.wet.getMagnitude(0,audition.wet.getNumSamples())>0.f,"audible aligned A/B");
    check(host.getEffectStateForTrackSlot(2,0,afterAudition)&&beforeAudition==afterAudition,"audition preserves project plugin state");
    cancel.store(true);check(!host.renderNativeAudition(chain,auditionInput,48000.,120.,audition,error,cancel),"cancelled audition rejected");
    const auto searchStart=juce::Time::getMillisecondCounterHiRes();
    int matches=0;for(int i=0;i<1800;++i)if(LibertyBrowserIntelligence::matches("Liberty Music Software Delay "+juce::String(i),"liberty delay"))++matches;
    check(matches==1800,"1800 synthetic search entries");
    std::cout<<"1800 search matches ms: "<<juce::Time::getMillisecondCounterHiRes()-searchStart<<'\n';
    for(int i=0;i<100;++i)
    {
        original.loadPreset(i);check(original.presetName.isNotEmpty(),"named preset");
        juce::MemoryBlock raw;check(host.getNativeFactoryPresetState(d.fileOrIdentifier,i,raw),"factory state roundtrip");
        LibertyPresetCenter::Entry e;e.id="user:test";e.name=original.presetName;e.deviceId=d.fileOrIdentifier;e.category="Test";e.state=raw;
        auto xml=LibertyPresetCenter::toXml(e);LibertyPresetCenter::Entry decoded;
        check(LibertyPresetCenter::userFromXml(*xml,decoded)&&decoded.state==raw&&decoded.name==e.name,"preset XML roundtrip");
        cancel.store(false);LibertyPluginHost::NativeChain single{{0,d.fileOrIdentifier,raw}};
        check(host.renderNativeAudition(single,auditionInput,48000.,120.,audition,error,cancel),"all factory presets DSP render");
        check(std::isfinite(audition.cpuPercent)&&audition.wet.getMagnitude(0,audition.wet.getNumSamples())<=.891f,"finite bounded factory audition");
    }
    LibertyDeviceChains::Entry entry;entry.name="QC";entry.devices=chain;auto chainXml=LibertyDeviceChains::encode(entry);LibertyDeviceChains::Entry parsed;
    check(LibertyDeviceChains::decode(*chainXml,parsed)&&parsed.devices.size()==2&&parsed.devices[1].slot==3&&parsed.devices[1].state==chain[1].state,"chain XML roundtrip");
    chainXml->getFirstChildElement()->setAttribute("slot",-1);LibertyDeviceChains::Entry bad;
    check(!LibertyDeviceChains::decode(*chainXml,bad),"effect cannot occupy instrument position");
    juce::PluginDescription eqDesc;const juce::String eqId="liberty.native.love-eq";
    check(host.findKnownPluginByIdentifier(eqId,eqDesc)&&eqDesc.category=="EQ"&&!eqDesc.isInstrument,"LOVE EQ native identity");
    juce::MemoryBlock eqState;
    check(host.getNativeFactoryPresetState(eqId,0,eqState)&&host.validateNativePresetState(eqId,eqState),"LOVE EQ original factory state");
    check(!host.validateNativePresetState(d.fileOrIdentifier,eqState)&&!host.validateNativePresetState(eqId,preset),"cross-device states rejected");
    check(host.loadEffectForTrackSlot(5,0,eqDesc,error,&eqState),"LOVE EQ audio insert");
    check(host.loadEffectForInstrumentTrackSlot(5,0,eqDesc,error,&eqState),"LOVE EQ instrument FX insert");
    LoveEQAudioProcessor eqOriginal;
    {std::unique_ptr<juce::AudioProcessorEditor> editor(eqOriginal.createEditorIfNeeded());check(editor&&editor->getWidth()>0&&editor->getHeight()>0,"LOVE EQ original editor");}
    for(int i=0;i<100;++i)
    {
        juce::MemoryBlock raw;check(host.getNativeFactoryPresetState(eqId,i,raw),"LOVE EQ factory state");
        LibertyPluginHost::NativeChain single{{0,eqId,raw}};
        cancel.store(false);check(host.renderNativeAudition(single,auditionInput,48000.,120.,audition,error,cancel),"LOVE EQ preset DSP audition");
        check(std::isfinite(audition.cpuPercent)&&audition.wet.getMagnitude(0,audition.wet.getNumSamples())<=.891f,"LOVE EQ finite bounded output");
    }
    LibertyPluginHost::NativeChain mixed{{0,eqId,eqState},{2,d.fileOrIdentifier,preset}};
    check(host.insertNativeChain(false,6,mixed,error),"mixed EQ and delay chain");
    LibertyPluginHost::NativeChain restoredMixed;
    check(host.captureNativeChain(false,6,restoredMixed,error)&&restoredMixed.size()==2,"mixed chain capture");
    check(host.renderNativeAudition(restoredMixed,auditionInput,48000.,120.,audition,error,cancel),"mixed chain isolated audition");
    if(auto* param=eqOriginal.parameters.getParameter("relationship_active"))param->setValueNotifyingHost(1.f);
    juce::MemoryBlock relational;eqOriginal.getStateInformation(relational);
    check(!host.renderNativeAudition({{0,eqId,relational}},auditionInput,48000.,120.,audition,error,cancel),"live relationship audition rejected");
    auditionInput.setSample(1,100,std::numeric_limits<float>::quiet_NaN());
    cancel.store(false);check(!host.renderNativeAudition(chain,auditionInput,48000.,120.,audition,error,cancel),"nonfinite input rejected");
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(original.createEditorIfNeeded());
        check(editor && editor->getWidth()>0 && editor->getHeight()>0,"original editor");
    }
    original.reset();
    original.set("mode",0);original.set("sync",0);original.set("left",100);
    original.set("right",100);original.set("link",1);original.set("feedback",0);
    original.set("mix",100);original.set("depth",0);original.set("saturation",0);
    original.prepareToPlay(48000,128);
    juce::MemoryBlock state;original.getStateInformation(state);
    check(host.setEffectStateForTrackSlot(0,0,state),"restore audio insert state");
    check(host.setEffectStateForInstrumentTrackSlot(0,0,state),"restore instrument insert state");
    juce::MemoryBlock saved;
    check(host.getEffectStateForTrackSlot(0,0,saved),"serialize native effect");
    FreeSpiritProcessor recovered;
    recovered.setStateInformation(saved.getData(),(int)saved.getSize());
    check(std::abs(recovered.get("left")-100)<.1f && recovered.get("mix")>99,"state roundtrip");
    float tail=0;
    for(int block=0;block<80;++block)
    {
        juce::AudioBuffer<float> expected(2,128), audio(2,128), instrument(2,128);
        expected.clear();audio.clear();instrument.clear();
        if(block==0){expected.setSample(0,0,.4f);audio.setSample(0,0,.4f);instrument.setSample(0,0,.4f);}
        juce::MidiBuffer midi;
        original.processBlock(expected,midi);
        host.processAudioEffectSlot(0,0,audio);
        host.processInstrumentEffectSlot(0,0,instrument);
        for(int ch=0;ch<2;++ch)
            for(int n=0;n<128;++n)
            {
                const auto a=audio.getSample(ch,n), b=expected.getSample(ch,n), c=instrument.getSample(ch,n);
                check(std::isfinite(a) && std::isfinite(c),"finite native output");
                check(std::abs(a-b)<1.e-5f && std::abs(c-b)<1.e-5f,"native original DSP parity");
                if(block>1)tail+=std::abs(a);
            }
    }
    check(tail>.001f,"audible delay tail");
    host.unloadEffectForTrackSlot(0,0);
    check(host.loadEffectForTrackSlot(0,0,d,error),"reload by stable ID");
    check(host.setEffectStateForTrackSlot(0,0,saved),"reload saved state");
    host.clearProjectPlugins();
    host.shutdown();
    std::cout<<"PASS: native identity, original UI, 100 presets, two insert paths, original DSP parity, state reload\n";
}
