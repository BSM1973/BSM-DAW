#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

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

// A single 64-bit atomic keeps the sample count and weighted mean-square
// together, avoiding mismatched GUI reads and any audio-thread locks.
// High word: sample count. Low word: IEEE-754 mean-square float.
inline std::uint32_t rmsBits(float value) noexcept
{
    std::uint32_t bits=0;
    std::memcpy(&bits,&value,sizeof(bits));
    return bits;
}
inline float rmsFromBits(std::uint32_t bits) noexcept
{
    float value=0.f;
    std::memcpy(&value,&bits,sizeof(value));
    return sanitize(value);
}
inline void publishRms(std::atomic<std::uint64_t>& accumulator,
                       float blockRms,int samples) noexcept
{
    if(samples<=0)return;
    const float safe=std::min(16.f,sanitize(blockRms));
    const double square=static_cast<double>(safe)*safe;
    const auto blockCount=static_cast<std::uint32_t>(samples);
    std::uint64_t old=accumulator.load(std::memory_order_relaxed);
    for(;;)
    {
        const auto prevCount=static_cast<std::uint32_t>(old>>32);
        const float prevSquare=rmsFromBits(static_cast<std::uint32_t>(old));
        const auto total=static_cast<std::uint64_t>(prevCount)+blockCount;
        const auto count=static_cast<std::uint32_t>(
            std::min<std::uint64_t>(total,std::numeric_limits<std::uint32_t>::max()));
        const float meanSquare=static_cast<float>(
            (static_cast<double>(prevCount)*prevSquare+
             static_cast<double>(blockCount)*square) / static_cast<double>(total));
        const auto next=(static_cast<std::uint64_t>(count)<<32)|rmsBits(meanSquare);
        if(accumulator.compare_exchange_weak(old,next,
            std::memory_order_release,std::memory_order_relaxed))return;
    }
}
inline void publishRmsSamples(std::atomic<std::uint64_t>& accumulator,
                              const float* data,int samples) noexcept
{
    if(data==nullptr||samples<=0)return;
    double sumSquares=0.0;
    for(int i=0;i<samples;++i)
    {
        const double x=std::isfinite(data[i])?data[i]:0.0;
        sumSquares+=x*x;
    }
    const float rms=static_cast<float>(
        std::sqrt(sumSquares/static_cast<double>(samples)));
    publishRms(accumulator,rms,samples);
}

inline float consumeRms(std::atomic<std::uint64_t>& accumulator) noexcept
{
    const auto state=accumulator.exchange(0,std::memory_order_acq_rel);
    if((state>>32)==0)return 0.f;
    return std::sqrt(rmsFromBits(static_cast<std::uint32_t>(state)));
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
    float rms=0.f;
    float measuredRms=0.f; // exact per-refresh RMS before visual release
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

// Two independent channel readings. Keeping the mono Display and its
// regression tests intact preserves existing one-channel behaviour.
using StereoPeaks=std::array<float,2>;
enum class ReadoutMode { peak, rms };

inline constexpr ReadoutMode toggleReadoutMode(ReadoutMode current) noexcept
{
    return current==ReadoutMode::peak?ReadoutMode::rms:ReadoutMode::peak;
}

enum class MeterClick { none, resetPeaks, toggleDigital };

// Pure pointer-region logic, same for audio, instrument and master strips.
// Local X coordinates leave the fader starting at x=44 untouched.
inline MeterClick resolveMeterClick(int localX,int y,int top,int bottom) noexcept
{
    if(bottom-top<40||localX<4||localX>=44)return MeterClick::none;
    if(y>=bottom+4&&y<bottom+36)return MeterClick::toggleDigital;
    if(y>=top&&y<bottom+2)return MeterClick::resetPeaks;
    return MeterClick::none;
}

struct StereoDisplay
{
    std::array<Display,2> channel{};

    void resetPeak() noexcept
    {
        for(auto& meter:channel)meter.resetPeak();
    }
    bool clipped() const noexcept
    {
        return channel[0].clipped||channel[1].clipped;
    }
    float heldPeak() const noexcept
    {
        return std::max(channel[0].heldPeak,channel[1].heldPeak);
    }
};

inline void advance(StereoDisplay& display,const StereoPeaks& peaks) noexcept
{
    advance(display.channel[0],peaks[0]);
    advance(display.channel[1],peaks[1]);
}
// RMS is the actual sample-weighted mean-square energy for each channel
// over the latest GUI interval. A gentle visual release avoids jumping to
// zero between device callbacks while preserving the original peak latches.
inline void advance(StereoDisplay& display,const StereoPeaks& peaks,
                    const StereoPeaks& rms) noexcept
{
    advance(display,peaks);
    for(std::size_t i=0;i<2;++i)
    {
        auto& channel=display.channel[i];
        const auto current=std::min(4.f,sanitize(rms[i]));
        channel.measuredRms=current;
        channel.rms=std::max(current,channel.rms*0.88f);
    }
}

// PEAK shows the existing 1.5s held sample peak. RMS shows the actual
// weighted reading over the latest ~42ms GUI interval, not its smoothed
// visual release. Both readouts use the loudest stereo channel in dBFS.
inline float readoutAmplitude(const StereoDisplay& display,ReadoutMode mode) noexcept
{
    if(mode==ReadoutMode::peak)return display.heldPeak();
    return std::max(display.channel[0].measuredRms,
                    display.channel[1].measuredRms);
}
}
