#pragma once
#include <algorithm>
#include <array>

// A track begins with one empty insert. When an FX is loaded, the
// following empty insert is revealed. Existing slot IDs never move:
// non-consecutive occupied slots still appear in their original order.
namespace LibertyMixerProgressiveFx {
inline constexpr int slotLimit=8;
// The virtual instrument occupies its own dedicated generator position,
// not FX 1. A newly loaded synth must open the FX rack automatically.
constexpr bool revealOnInstrumentLoad(bool wasLoaded,bool nowLoaded) noexcept
{
    return !wasLoaded && nowLoaded;
}
inline int activeCount(const std::array<bool,slotLimit>& occupied) noexcept
{
    return static_cast<int>(std::count(occupied.begin(),occupied.end(),true));
}
inline int revealedCount(const std::array<bool,slotLimit>& occupied) noexcept
{
    int last=-1;
    for(int slot=0;slot<slotLimit;++slot)
        if(occupied[(size_t)slot])last=slot;
    return std::clamp(last+2,1,slotLimit);
}
inline int windowStart(int requested,int visible,int previous) noexcept
{
    const int count=std::clamp(requested,1,slotLimit);
    return std::clamp(previous,0,std::max(0,count-visible));
}
}
