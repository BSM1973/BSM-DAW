#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>

// Signal peaks are published by the audio callback and exchanged by the GUI.
// The maximum is accumulated without locks or allocating on the audio thread.
// All levels are sample peaks (dBFS), NOT inter-sample true peaks or LUFS.
namespace LibertyMixConsoleMeter
{
inline float sanitize(float peak) noexcept
{
    return std::isfinite(peak) && peak>0.f ? peak:0.f;
}

inline void publish(std::atomic<float>& maximum,float peak) noexcept
{
    const float safe=sanitize(peak);
    float old=maximum.load(std::memory_order_relaxed);
    while(old<safe && !maximum.compare_exchange_weak(
        old,safe,std::memory_order_release,std::memory_order_relaxed)){}
}

inline float consume(std::atomic<float>& maximum) noexcept
{
    return sanitize(maximum.exchange(0.f,std::memory_order_acq_rel));
}

inline float toDb(float amplitude) noexcept
{
    return amplitude>0.f && std::isfinite(amplitude)
        ?20.f*std::log10(amplitude):-60.f;
}

inline float meterPosition(float amplitude) noexcept
{
    return std::clamp((toDb(amplitude)+60.f)/66.f,0.f,1.f);
}

struct Display
{
    float level=0.f;
    float heldPeak=0.f;
    int holdFrames=0;
    bool clipped=false;

    void resetPeak() noexcept
    {
        heldPeak=0.f;
        holdFrames=0;
        clipped=false;
    }
};

// Designed for the console's 24 Hz UI timer. Hold ~1.5 seconds, then
// decay gradually. CLIP stays lit until the user clicks on the meter.
inline void advance(Display& display,float blockPeak) noexcept
{
    const float current=std::min(4.f,sanitize(blockPeak));
    display.level=std::max(current,display.level*0.82f);
    if(current>=display.heldPeak && current>0.f)
    {
        display.heldPeak=current;
        display.holdFrames=36;
    }
    else if(display.holdFrames>0)
        --display.holdFrames;
    else
        display.heldPeak=std::max(current,display.heldPeak*0.94f);
    if(current>=1.f)
        display.clipped=true;
}
}
