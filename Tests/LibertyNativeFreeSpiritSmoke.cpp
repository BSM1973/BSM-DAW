#include <JuceHeader.h>
#include "PluginHost.h"
#include "Plugin.h"
#include "LibertyManufacturerGrouping.h"
#include "LibertyBrowserIntelligence.h"
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
    for(int i=0;i<100;++i)
    {
        original.loadPreset(i);
        check(original.presetName.isNotEmpty(),"named preset");
    }
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
