#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace LibertyPerformTiming
{
inline std::int64_t ticksToSamples(std::int64_t ticks, double bpm, double sampleRate) noexcept
{
    if (ticks <= 0 || !std::isfinite(bpm) || !std::isfinite(sampleRate)
        || bpm <= 0.0 || sampleRate <= 0.0) return 0;
    const double samples=(double)ticks * sampleRate * 60.0 / (bpm * 960.0);
    return (std::int64_t)std::llround(std::min(samples, (double)(std::numeric_limits<std::int64_t>::max()/2)));
}
inline std::int64_t quantumSamples(int mode, double bpm, int numerator,
                                   int denominator, double sampleRate) noexcept
{
    if (mode <= 0) return 1;
    const double beats=mode==1?1.0:
        (double)std::max(1,numerator)*4.0/(double)std::max(1,denominator);
    if (!std::isfinite(bpm) || bpm <= 0.0 || sampleRate <= 0.0) return 1;
    return std::max<std::int64_t>(1,
        (std::int64_t)std::llround(sampleRate*60.0*beats/bpm));
}
inline std::int64_t nextBoundary(std::int64_t sample, std::int64_t quantum) noexcept
{
    if (quantum <= 1) return std::max<std::int64_t>(0,sample);
    sample=std::max<std::int64_t>(0,sample);
    const auto rem=sample%quantum;
    return rem==0?sample:sample+quantum-rem;
}
inline int instrumentForLogicalTrack(int logical, int audioTracks,
                                     int midiTracks, int instrumentTracks) noexcept
{
    if(instrumentTracks<=0 || logical<audioTracks) return -1;
    const int index=logical-audioTracks;
    if(index<midiTracks) return index%instrumentTracks;
    const int instrument=index-midiTracks;
    return instrument<instrumentTracks?instrument:-1;
}
}
