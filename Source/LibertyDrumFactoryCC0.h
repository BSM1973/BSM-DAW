#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "LibertyFactoryDrumData.h"
#include <array>
#include <cmath>
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
inline std::shared_ptr<const juce::AudioBuffer<float>> render(
    juce::AudioFormatManager& formats,int style,int pad,double& sourceRate)
{
    if(style<0||style>=styles||pad<0||pad>=pads)return {};
    const char* bytes=nullptr;int size=0;
    if(!getData(names[style][pad],bytes,size))return {};
    auto stream=std::make_unique<juce::MemoryInputStream>(bytes,(size_t)size,false);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::move(stream)));
    if(!reader||reader->lengthInSamples<=0||reader->lengthInSamples>48000LL*8||
       reader->numChannels<1||reader->numChannels>2)return {};
    sourceRate=reader->sampleRate;
    const int frames=(int)reader->lengthInSamples;
    juce::AudioBuffer<float> audio((int)reader->numChannels,frames);
    if(!reader->read(&audio,0,frames,0,true,true))return {};
    const auto& profile=profiles[(std::size_t)style];
    const float level=pad==0?.79f:pad==1?.71f:pad==2?.59f:
        (pad==3||pad==9)?.36f:pad==4?.40f:
        (pad==12||pad==13)?.35f:pad==15?.70f:.58f;
    const float cutoff=(pad>=3&&pad<=4)?std::min(18500.f,profile.lowPass*1.45f):profile.lowPass;
    const float alpha=1.f-std::exp(-2.f*3.141592654f*cutoff/(float)sourceRate);
    const int fadeSamples=std::min(frames/4,(int)(sourceRate*.012));
    for(int channel=0;channel<audio.getNumChannels();++channel)
    {
        float low=0.f;auto* samples=audio.getWritePointer(channel);
        for(int n=0;n<frames;++n)
        {
            const float dry=std::isfinite(samples[n])?samples[n]:0.f;
            low+=alpha*(dry-low);
            float x=(low*(1.f-profile.brightness)+dry*profile.brightness);
            x=std::tanh(x*profile.drive)/std::tanh(profile.drive);
            if(n<sourceRate*.007)
                x*=1.f+(profile.transient-1.f)*(1.f-(float)n/(float)(sourceRate*.007));
            if(profile.tail<1.f)
                x*=std::pow(profile.tail,(float)n/(float)sourceRate*4.f);
            if(fadeSamples>0&&n>=frames-fadeSamples)
                x*=juce::jlimit(0.f,1.f,(float)(frames-1-n)/(float)fadeSamples);
            samples[n]=juce::jlimit(-.97f,.97f,x*level);
        }
    }
    return std::make_shared<const juce::AudioBuffer<float>>(std::move(audio));
}
}
