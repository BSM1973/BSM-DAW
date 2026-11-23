#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "LibertyFactoryDrumData.h"
#include <array>
#include <cmath>
#include <algorithm>
#include <memory>

// Curated, redistributable CC0 recordings and drum-machine hits.
// Phazite Sounds pack pinned in CMakeLists.txt; includes Versilian,
// Karoryfer, FreePats and synthesised originals. See NOTICE file.
namespace LibertyDrumFactory
{
constexpr int styles=10,pads=16;
inline constexpr const char* names[styles][pads]={
 {"Kick Punchy.flac","Snare Crack.flac","Clap Finger.flac","Hat Closed Tight.flac","Hat Open Analog.flac","Tom Low.flac","Tom Electronic.flac","Tom High.flac","Rim Side Stick.flac","Shaker.flac","Conga.flac","Cowbell.flac","Crash.flac","Ride.flac","Impact Boom.flac","808 Punchy C.flac"},
 {"Kick Acoustic.flac","Snare Acoustic.flac","Clap Hand.flac","Hat Closed Acoustic.flac","Hat Open Acoustic.flac","Tom Floor.flac","Tom Low.flac","Tom High.flac","Rim Side Stick.flac","Shaker.flac","Bongo Low.flac","Bongo High.flac","Crash.flac","Ride.flac","Impact Concert Drum.flac","Kick Acoustic Big.flac"},
 {"Kick Acoustic Big.flac","Snare Rimshot.flac","Clap Studio.flac","Hat Closed Acoustic.flac","Hat Open Acoustic.flac","Tom Floor.flac","Tom Low.flac","Tom High.flac","Rim Side Stick.flac","Tambourine.flac","Cowbell.flac","Conga.flac","Crash.flac","Ride Bell.flac","Impact Hit.flac","Kick Punchy.flac"},
 {"Kick Electronic Hard.flac","Snare Crack.flac","Clap Studio.flac","Hat Closed Tight.flac","Hat Open Electronic.flac","Tom Floor.flac","Tom Low.flac","Tom High.flac","Rim Side Stick.flac","Shaker.flac","Cowbell Analog.flac","Cowbell.flac","Crash Electronic.flac","Ride Bell.flac","Impact Boom.flac","Kick Electronic Hard.flac"},
 {"Kick Electronic.flac","Snare Electronic.flac","Clap Electronic.flac","Hat Closed Electronic.flac","Hat Open Electronic.flac","Tom Electronic.flac","Tom Low.flac","Tom High.flac","Cowbell Analog.flac","Shaker.flac","Claves.flac","Cowbell Analog.flac","Crash Electronic.flac","Ride Bell.flac","Impact Hit.flac","808 Short G.flac"},
 {"Kick Soft.flac","Rim Side Stick.flac","Clap Hand.flac","Hat Closed Analog.flac","Hat Open Acoustic.flac","Tom Floor.flac","Bongo Low.flac","Bongo High.flac","Rim Side Stick.flac","Shaker.flac","Conga Low.flac","Conga.flac","Crash.flac","Ride.flac","Impact Boom.flac","Sub Bass C.flac"},
 {"Kick Punchy.flac","Snare Tight.flac","Clap Studio.flac","Hat Closed Acoustic.flac","Hat Open Analog.flac","Tom Floor.flac","Tom Low.flac","Tom High.flac","Rim Side Stick.flac","Tambourine.flac","Bongo Low.flac","Cowbell.flac","Crash.flac","Ride.flac","Impact Hit.flac","Kick Deep.flac"},
 {"Conga Low.flac","Bongo High.flac","Clap Hand.flac","Tambourine Shake.flac","Tambourine.flac","Conga Low.flac","Conga.flac","Bongo High.flac","Claves.flac","Shaker.flac","Bongo Low.flac","Conga.flac","Crash.flac","Tambourine.flac","Gong.flac","Sub Bass C.flac"},
 {"Kick Tight.flac","Snare Rimshot.flac","Clap Finger.flac","Hat Closed Tight.flac","Hat Open Acoustic.flac","Tom Low.flac","Bongo Low.flac","Tom High.flac","Rim Side Stick.flac","Tambourine Shake.flac","Cowbell.flac","Conga.flac","Crash.flac","Ride.flac","Impact Hit.flac","Kick Deep.flac"},
 {"808 Long F.flac","Snare Electronic.flac","Clap Electronic.flac","Hat Closed Electronic.flac","Hat Open Electronic.flac","Tom Electronic.flac","Tom Low.flac","Tom High.flac","Rim Side Stick.flac","Hat Closed Analog.flac","Cowbell Analog.flac","Claves.flac","Crash Electronic.flac","Ride Bell.flac","Impact Boom.flac","808 Punchy C.flac"}
};
inline constexpr const char* descriptions[styles]={
 "Vintage et gras / breaks poussiéreux","Kit acoustique naturel / prise rapprochée",
 "Batterie puissante / rimshot","Attaques rapides / grosse caisse dure",
 "Drum machine électronique / clap","One drop chaud / rim et percussions",
 "Pop moderne / transitoires équilibrées","Percussions réelles / congás et tambourins",
 "Sec, claquant et syncopé","808 profond / hats électroniques"
};
struct Colour{float drive,lowPass,brightness,transient,tail;};
inline constexpr std::array<Colour,styles> profiles={{
 {1.27f,6800.f,.12f,1.08f,.91f},  // Hip-hop: rounded and warm
 {1.02f,17000.f,.03f,1.05f,1.00f},// Acoustic: preserve the microphone
 {1.30f,14500.f,.09f,1.21f,.96f},// Rock: punchy
 {1.53f,16000.f,.14f,1.33f,.87f},// Metal: tight, fast
 {1.31f,11500.f,.12f,1.04f,.96f},// Electro: analog edge
 {1.07f,8500.f,.06f,.94f,1.00f},// Reggae: deep and soft
 {1.11f,16500.f,.07f,1.12f,.97f},// Pop: open
 {1.02f,15500.f,.04f,1.10f,1.00f},// Oriental: preserve real hand drums
 {1.16f,14200.f,.09f,1.25f,.85f},// Funk: dry and sharp
 {1.43f,10200.f,.11f,1.07f,1.00f} // Trap: weight and reduced treble
}};
inline bool getData(const char* filename,const char*& bytes,int& size) noexcept
{
    size=0;bytes=nullptr;
    for(int i=0;i<LibertyDrumAssets::namedResourceListSize;++i)
    {
        const auto* original=LibertyDrumAssets::originalFilenames[i];
        if(!original)continue;
        const juce::String leaf=juce::File(juce::String::fromUTF8(original)).getFileName();
        if(leaf==juce::String::fromUTF8(filename))
        {
            bytes=LibertyDrumAssets::getNamedResource(
                LibertyDrumAssets::namedResourceList[i],size);
            return bytes!=nullptr&&size>128;
        }
    }
    return false;
}

struct Supplement
{
    const char* source=nullptr;
    float level=0.f,highPassHz=0.f,lowPassHz=20000.f;
};
// The primary one-shot always remains the leading sound. Curated secondary
// CC0 material adds genuinely different attack/body according to the genre,
// without creating a synthetic replacement for the acoustic recordings.
inline Supplement layer(int style,int pad) noexcept
{
    switch(style)
    {
        case 0: if(pad==0)return {"Kick Deep.flac",.27f,0.f,170.f};
                if(pad==1)return {"Clap Studio.flac",.23f,330.f,12000.f};
                break;
        case 1: return {}; // Acoustic: preserve the real mic recording.
        case 2: if(pad==0)return {"Kick Tight.flac",.34f,650.f,16000.f};
                if(pad==1)return {"Snare Acoustic.flac",.24f,150.f,13500.f};
                break;
        case 3: if(pad==0)return {"Kick Tight.flac",.42f,1100.f,19000.f};
                if(pad==1)return {"Snare Rimshot.flac",.38f,450.f,18500.f};
                if(pad==12)return {"Crash Electronic.flac",.14f,1900.f,20000.f};
                break;
        case 4: if(pad==0)return {"808 Short G.flac",.28f,0.f,150.f};
                if(pad==2)return {"Clap Finger.flac",.24f,300.f,15500.f};
                break;
        case 5: if(pad==0)return {"Sub Bass C.flac",.28f,0.f,105.f};
                if(pad==8)return {"Claves.flac",.15f,500.f,12500.f};
                break;
        case 6: if(pad==0)return {"Kick Acoustic.flac",.24f,110.f,7000.f};
                if(pad==1)return {"Clap Studio.flac",.22f,410.f,17000.f};
                break;
        case 7: if(pad==0)return {"Bongo Low.flac",.20f,85.f,15000.f};
                if(pad==2)return {"Tambourine.flac",.12f,1100.f,17000.f};
                break;
        case 8: if(pad==0)return {"Kick Acoustic.flac",.19f,180.f,8500.f};
                if(pad==1)return {"Rim Side Stick.flac",.22f,700.f,15000.f};
                break;
        case 9: if(pad==0)return {"808 Punchy C.flac",.32f,240.f,12500.f};
                if(pad==1)return {"Clap Electronic.flac",.24f,350.f,15000.f};
                if(pad==3)return {"Hat Closed Tight.flac",.14f,3500.f,20000.f};
                break;
    }
    return {};
}
inline std::unique_ptr<juce::AudioBuffer<float>> decode(
    juce::AudioFormatManager& formats,const char* filename,double& rate)
{
    const char* bytes=nullptr;int size=0;
    if(!getData(filename,bytes,size))return {};
    auto source=std::make_unique<juce::MemoryInputStream>(bytes,(size_t)size,false);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::move(source)));
    if(!reader||reader->lengthInSamples<=0||reader->lengthInSamples>48000LL*8||
       reader->numChannels<1||reader->numChannels>2||
       !std::isfinite(reader->sampleRate)||reader->sampleRate<8000.)return {};
    rate=reader->sampleRate;
    auto audio=std::make_unique<juce::AudioBuffer<float>>((int)reader->numChannels,
        (int)reader->lengthInSamples);
    if(!reader->read(audio.get(),0,audio->getNumSamples(),0,true,true))return {};
    return audio;
}
inline std::shared_ptr<const juce::AudioBuffer<float>> render(
    juce::AudioFormatManager& formats,int style,int pad,double& sourceRate)
{
    if(style<0||style>=styles||pad<0||pad>=pads)return {};
    auto audio=decode(formats,names[style][pad],sourceRate);
    if(!audio)return {};
    const auto secondary=layer(style,pad);
    double secondaryRate=0.;
    auto second=secondary.source?decode(formats,secondary.source,secondaryRate):nullptr;
    // A secondary missing from the pinned source pack must never silently
    // change a supposedly complete kit to a different sound.
    if(secondary.source&&!second)return {};
    const auto& profile=profiles[(size_t)style];
    const int count=audio->getNumSamples(),channels=audio->getNumChannels();
    const float cutoff=(pad>=3&&pad<=4)?std::min(19000.f,profile.lowPass*1.3f):profile.lowPass;
    const float lowAlpha=1.f-std::exp(-6.283185307f*cutoff/(float)sourceRate);
    const bool lowInstrument=pad==0||pad==15||style==7;
    const float highCut=lowInstrument?(style==9?16.f:22.f):pad==1?65.f:pad>=3&&pad<=4?370.f:90.f;
    const float highAlpha=1.f-std::exp(-6.283185307f*highCut/(float)sourceRate);
    const float secondaryHighAlpha=secondary.highPassHz>0.f?
        1.f-std::exp(-6.283185307f*secondary.highPassHz/(float)sourceRate):0.f;
    const float secondaryLowAlpha=1.f-std::exp(-6.283185307f*
        secondary.lowPassHz/(float)sourceRate);
    // All gain, EQ, layering and normalisation are computed off the audio
    // callback, and published as a complete immutable sample buffer.
    const int fadeCount=std::min(count/4,(int)(sourceRate*.007));
    for(int ch=0;ch<channels;++ch)
    {
        float low=0.f,dc=0.f,layerHigh=0.f,layerLow=0.f;
        auto* dst=audio->getWritePointer(ch);
        const float* other=second?second->getReadPointer(std::min(ch,second->getNumChannels()-1)):nullptr;
        for(int n=0;n<count;++n)
        {
            const float raw=std::isfinite(dst[n])?dst[n]:0.f;
            low+=lowAlpha*(raw-low);
            dc+=highAlpha*(low-dc);
            float x=(low-dc)*(1.f-profile.brightness)+
                (raw-dc)*profile.brightness;
            // Character is not a uniform distortion preset across all kits.
            if(style==1||style==7)
                x=raw*.85f+x*.15f; // natural acoustic/hand-percussion
            else
                x=std::tanh(x*profile.drive)/
                    std::tanh(profile.drive);
            if(other)
            {
                const double frame=(double)n*secondaryRate/sourceRate;
                const int at=(int)frame;
                if(at+1<second->getNumSamples())
                {
                    const float v=other[at]+(other[at+1]-other[at])*(float)(frame-at);
                    layerHigh+=secondaryHighAlpha*(v-layerHigh);
                    const float high=secondary.highPassHz>0.f?v-layerHigh:v;
                    layerLow+=secondaryLowAlpha*(high-layerLow);
                    x+=secondary.level*layerLow;
                }
            }
            const float t=(float)n/(float)sourceRate;
            if(style==3&&(pad==0||pad==1))
                x*=std::exp(-t*(pad==0?2.1f:1.6f)); // metal: tight decay
            else if(style==8&&(pad==0||pad==1))
                x*=std::exp(-t*1.65f); // funk: shorter, drier
            else if(style==5&&pad==0)
                x*=std::exp(-t*.12f); // reggae: preserve the sub tail
            else if(profile.tail<1.f)
                x*=std::pow(profile.tail,t*4.f);
            if(t<.008f)
            {
                const float attack=1.f+(profile.transient-1.f)*(1.f-t/.008f);
                x*=attack;
            }
            if(fadeCount>0&&n>=count-fadeCount)
                x*=juce::jlimit(0.f,1.f,(float)(count-1-n)/(float)fadeCount);
            dst[n]=std::isfinite(x)?x:0.f;
        }
    }
    // Equal perceived punch between source recordings while preserving a
    // conservative -1 dBFS sample peak ceiling. No hard digital clipping.
    double rmsEnergy=0.;float peak=0.f;
    const int early=std::min(count,(int)(sourceRate*.16));
    for(int ch=0;ch<channels;++ch)
    {
        const float* p=audio->getReadPointer(ch);
        for(int n=0;n<count;++n)
        {
            const float a=std::abs(p[n]);peak=std::max(peak,a);
            if(n<early)rmsEnergy+=(double)p[n]*p[n];
        }
    }
    if(peak<.0000001f||early<1)return {};
    const float rms=(float)std::sqrt(rmsEnergy/(double)(early*channels));
    const float target=(pad==0||pad==15)? .165f :
        pad==1?.14f:pad==2?.11f:pad==3?.078f:pad==4?.077f:
        pad>=12&&pad<=13?.070f:.105f;
    const float gain=std::min(.88f/peak,std::clamp(target/std::max(rms,.00001f),.35f,3.f));
    audio->applyGain(gain);
    return std::shared_ptr<const juce::AudioBuffer<float>>(std::move(audio));
}
}
