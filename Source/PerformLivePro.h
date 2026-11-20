#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace LibertyPerformLive
{
constexpr int scenes = 8;
constexpr int macros = 4;
constexpr int targets = 13; // 8 scenes, STOP, 4 macros
struct Follow
{
    int action = 0;     // 0 off, 1 next, 2 previous, 3 random, 4 stop
    int afterBars = 4;  // 1..32, absolute bar duration
};
inline Follow validFollow(Follow f) noexcept
{
    f.action = std::clamp(f.action,0,4);
    f.afterBars = std::clamp(f.afterBars,1,32);
    return f;
}
inline std::int64_t barSamples(double bpm,int numerator,int denominator,
                               double sampleRate) noexcept
{
    if(!std::isfinite(bpm)||bpm<=0||!std::isfinite(sampleRate)||sampleRate<=0)
        return 1;
    const auto seconds = 60.0 / bpm * (double)std::clamp(numerator,1,32)
                         * 4.0 / (double)std::clamp(denominator,1,32);
    return std::max<std::int64_t>(1,(std::int64_t)std::llround(sampleRate*seconds));
}
inline std::int64_t nextFollowSample(std::int64_t started,Follow f,
                                    double bpm,int num,int den,double rate) noexcept
{
    const auto bars=validFollow(f).afterBars;
    const auto length=barSamples(bpm,num,den,rate);
    return std::max<std::int64_t>(0,started)+length*bars;
}
inline std::uint32_t nextSeed(std::uint32_t& s) noexcept
{
    return s = s*1664525u+1013904223u;
}
inline int followScene(int scene,int action,std::uint32_t& seed) noexcept
{
    scene=std::clamp(scene,0,scenes-1);
    if(action==1)return (scene+1)%scenes;
    if(action==2)return (scene+scenes-1)%scenes;
    if(action==3)return (scene+1+(int)(nextSeed(seed)%(scenes-1)))%scenes;
    if(action==4)return -1;
    return scene;
}
inline float crossfadeIn(std::int64_t elapsed,std::int64_t duration) noexcept
{
    if(duration<=0)return 1.0f;
    return (float)std::clamp((double)elapsed/(double)duration,0.0,1.0);
}
inline float crossfadeOut(std::int64_t elapsed,std::int64_t duration) noexcept
{
    return 1.0f-crossfadeIn(elapsed,duration);
}
inline std::int64_t transitionSamples(int ms,double sampleRate) noexcept
{
    return std::max<std::int64_t>(0,
        (std::int64_t)std::llround(std::clamp(ms,0,1000) *
                                  std::max(1.0,sampleRate)/1000.0));
}
struct Binding
{
    int channel=0; // 0 = any, 1..16 = specific channel
    int kind=0;    // 0 none, 1 MIDI CC, 2 note-on
    int number=-1; // CC/note 0..127
};
inline Binding validBinding(Binding b) noexcept
{
    b.channel=std::clamp(b.channel,0,16);
    b.kind=std::clamp(b.kind,0,2);
    b.number=b.kind==0?-1:std::clamp(b.number,0,127);
    return b;
}
inline bool matches(Binding b,int kind,int channel,int number) noexcept
{
    b=validBinding(b);
    return b.kind!=0 && b.kind==kind &&
           (b.channel==0 || b.channel==channel) &&
           b.number==number;
}
inline std::uint64_t packMidi(int kind,int channel,int number,int value,
                              std::uint32_t serial) noexcept
{
    return ((std::uint64_t)serial<<32)|
           ((std::uint64_t)(kind&3)<<24)|
           ((std::uint64_t)(channel&31)<<16)|
           ((std::uint64_t)(number&127)<<8)|
           (std::uint64_t)(value&127);
}
struct MidiEvent {int kind=0,channel=0,number=0,value=0;std::uint32_t serial=0;};
inline MidiEvent unpackMidi(std::uint64_t packed) noexcept
{
    return {(int)((packed>>24)&3),(int)((packed>>16)&31),
            (int)((packed>>8)&127),(int)(packed&127),
            (std::uint32_t)(packed>>32)};
}
inline float macroGain(int value) noexcept
{
    return std::clamp(value,0,200)/100.0f;
}
inline int macroFromMidi(int value) noexcept
{
    return std::clamp((int)std::lround(std::clamp(value,0,127)*200.0/127),0,200);
}
inline std::int64_t wrapSource(std::int64_t sample,std::int64_t start,
                              std::int64_t length) noexcept
{
    if(length<=0)return start;
    return start + ((sample%length)+length)%length;
}
}
