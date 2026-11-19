#pragma once
// Transport-relative, bounded event enumeration; safe for the audio callback.
// A clip's source events remain one cycle long. Transport playback repeats
// them at musical cycle boundaries without generating a large MIDI note list.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace LibertyStepTransport
{
template<class Callback>
void eachLoopOccurrence(double blockStart, double blockEnd,
                        double clipStart, double cycleSeconds,
                        double noteStart, double noteEnd,
                        Callback&& callback) noexcept
{
    if (!std::isfinite(blockStart) || !std::isfinite(blockEnd) ||
        !std::isfinite(clipStart) || !std::isfinite(cycleSeconds) ||
        !std::isfinite(noteStart) || !std::isfinite(noteEnd) ||
        blockStart >= blockEnd || cycleSeconds <= 0.0 ||
        noteStart < 0.0 || noteEnd <= noteStart ||
        noteEnd > cycleSeconds + 0.0000001) return;
    // Clamp before integer conversion to avoid overflow in an unusual
    // transport position supplied by an external audio host.
    const double firstReal = std::floor((blockStart - clipStart - noteEnd) / cycleSeconds);
    const double lastReal = std::ceil((blockEnd - clipStart - noteStart) / cycleSeconds);
    constexpr double limit = (double)(std::numeric_limits<std::int64_t>::max() / 2);
    if (!std::isfinite(firstReal) || !std::isfinite(lastReal) ||
        firstReal > limit || lastReal > limit) return;
    std::int64_t first = (std::int64_t)std::max(0.0, firstReal);
    const std::int64_t last = (std::int64_t)std::max(0.0, lastReal);
    // Even pathological audio buffer sizes cannot cause unbounded work.
    for (int count = 0; first <= last && count < 512; ++first, ++count)
    {
        const double onset = clipStart + noteStart + (double)first * cycleSeconds;
        const double ending = clipStart + noteEnd + (double)first * cycleSeconds;
        if ((onset >= blockStart && onset < blockEnd) ||
            (ending >= blockStart && ending < blockEnd) ||
            (onset < blockStart && ending > blockStart))
            callback(onset, ending);
    }
}
} // namespace LibertyStepTransport
