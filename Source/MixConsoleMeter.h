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
// visual release. Stereo readouts keep L and R independent.
inline float channelReadoutAmplitude(const StereoDisplay& display,
                                    ReadoutMode mode,int channel) noexcept
{
    if(channel<0||channel>=2)return 0.f;
    const auto& meter=display.channel[static_cast<std::size_t>(channel)];
    return sanitize(mode==ReadoutMode::peak?meter.heldPeak:meter.measuredRms);
}

// Backwards-compatible combined readout used elsewhere in the mixer.
inline float readoutAmplitude(const StereoDisplay& display,ReadoutMode mode) noexcept
{
    return std::max(channelReadoutAmplitude(display,mode,0),
                    channelReadoutAmplitude(display,mode,1));
}

// An RMS-based stereo balance indicator. Positive difference means LEFT
// dominates, negative means RIGHT. The position is -1 for left, 0 centre,
// +1 for right. It is a level balance, not a phase/correlation meter.
enum class BalanceSide { silent, left, center, right };
struct StereoBalance
{
    BalanceSide side=BalanceSide::silent;
    float differenceDb=0.f; // non-negative magnitude for digital display
    float position=0.f;     // -1 left, +1 right; saturates at 24 dB
};

inline StereoBalance evaluateStereoBalance(float rmsLeft,float rmsRight) noexcept
{
    constexpr float floor=0.001f; // -60 dBFS: ignore silence/noise floor
    const float left=sanitize(rmsLeft);
    const float right=sanitize(rmsRight);
    if(std::max(left,right)<floor)return {};
    const float signedDb=std::clamp(
        20.f*std::log10(std::max(left,floor)/std::max(right,floor)),
        -60.f,60.f);
    // Prevent a jittering L/R direction on nearly balanced material.
    if(std::abs(signedDb)<0.15f)
        return {BalanceSide::center,0.f,0.f};
    return {signedDb>0.f?BalanceSide::left:BalanceSide::right,
            std::abs(signedDb),std::clamp(-signedDb/24.f,-1.f,1.f)};
}

inline StereoBalance stereoRmsBalance(const StereoDisplay& display) noexcept
{
    // The console's gently released RMS provides a stable, musically useful
    // stereo placement indication independently of the PEAK/RMS digits.
    return evaluateStereoBalance(display.channel[0].rms,
                                 display.channel[1].rms);
}
 
// A zero-lag stereo correlation is based on matched L/R samples, not
// their individual RMS magnitudes. Each sample-weighted running mean and
// count share one lock-free atomic. A torn GUI snapshot is marked invalid.
struct StereoCorrelationAccumulator
{
    std::atomic<std::uint64_t> leftSquare{0},rightSquare{0},product{0};
};
struct StereoCorrelationReading
{
    float value=0.f; // -1 opposite polarity, 0 uncorrelated, +1 identical
    bool valid=false;
};
inline float correlationMeanFromBits(std::uint32_t bits) noexcept
{
    float value=0.f;
    std::memcpy(&value,&bits,sizeof(value));
    return std::isfinite(value)?std::clamp(value,-256.f,256.f):0.f;
}
inline void publishCorrelationMean(std::atomic<std::uint64_t>& accumulator,
                                   double blockMean,int samples) noexcept
{
    if(samples<=0)return;
    const float safe=std::isfinite(blockMean)
        ?static_cast<float>(std::clamp(blockMean,-256.0,256.0)):0.f;
    const auto blockCount=static_cast<std::uint32_t>(samples);
    std::uint64_t old=accumulator.load(std::memory_order_relaxed);
    for(;;)
    {
        const auto previousCount=static_cast<std::uint32_t>(old>>32);
        const double previous=correlationMeanFromBits(static_cast<std::uint32_t>(old));
        const auto total=static_cast<std::uint64_t>(previousCount)+blockCount;
        const auto count=static_cast<std::uint32_t>(
            std::min<std::uint64_t>(total,std::numeric_limits<std::uint32_t>::max()));
        const float mean=static_cast<float>(
            (static_cast<double>(previousCount)*previous+
             static_cast<double>(blockCount)*safe)/static_cast<double>(total));
        const auto next=(static_cast<std::uint64_t>(count)<<32)|rmsBits(mean);
        if(accumulator.compare_exchange_weak(old,next,
            std::memory_order_release,std::memory_order_relaxed))return;
    }
}
inline void publishStereoCorrelationSamples(StereoCorrelationAccumulator& accumulator,
                                             const float* left,const float* right,
                                             int samples) noexcept
{
    if(left==nullptr||right==nullptr||samples<=0)return;
    double leftEnergy=0.0,rightEnergy=0.0,cross=0.0;
    for(int i=0;i<samples;++i)
    {
        const double l=std::isfinite(left[i])
            ?std::clamp(static_cast<double>(left[i]),-16.0,16.0):0.0;
        const double r=std::isfinite(right[i])
            ?std::clamp(static_cast<double>(right[i]),-16.0,16.0):0.0;
        leftEnergy+=l*l;rightEnergy+=r*r;cross+=l*r;
    }
    const double inverse=1.0/static_cast<double>(samples);
    publishCorrelationMean(accumulator.leftSquare,leftEnergy*inverse,samples);
    publishCorrelationMean(accumulator.rightSquare,rightEnergy*inverse,samples);
    publishCorrelationMean(accumulator.product,cross*inverse,samples);
}
inline StereoCorrelationReading consumeStereoCorrelation(
    StereoCorrelationAccumulator& accumulator) noexcept
{
    const auto left=accumulator.leftSquare.exchange(0,std::memory_order_acq_rel);
    const auto right=accumulator.rightSquare.exchange(0,std::memory_order_acq_rel);
    const auto cross=accumulator.product.exchange(0,std::memory_order_acq_rel);
    const auto count=left>>32;
    if(count==0||count!=(right>>32)||count!=(cross>>32))return {};
    const double le=correlationMeanFromBits(static_cast<std::uint32_t>(left));
    const double re=correlationMeanFromBits(static_cast<std::uint32_t>(right));
    // Below -60 dBFS in either channel, phase is indeterminate.
    if(le<1.e-6||re<1.e-6)return {};
    const double product=correlationMeanFromBits(static_cast<std::uint32_t>(cross));
    const double value=product/std::sqrt(le*re);
    if(!std::isfinite(value))return {};
    return {static_cast<float>(std::clamp(value,-1.0,1.0)),true};
}
struct StereoCorrelationDisplay
{
    float value=0.f;
    bool valid=false;
    void advance(StereoCorrelationReading reading) noexcept
    {
        if(!reading.valid){value=0.f;valid=false;return;}
        if(!valid)value=reading.value;
        else
        {
            // Phase opposition appears quickly; positive recovery is gentler.
            const float response=reading.value<value?0.65f:0.24f;
            value+=response*(reading.value-value);
        }
        valid=true;
    }
};

}
