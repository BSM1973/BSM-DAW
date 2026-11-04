#pragma once
#include <algorithm>
#include <vector>

// Pure per-track Solo logic shared by audio and instrument mixer strips.
// Does not modify mute, fader levels, pan or other DSP parameters.
namespace LibertyMixConsoleSoloLogic
{
enum class Kind { audio, instrument };
enum class Action { toggle, exclusive, clearAll };
 // Shared modifier priority for both the Console and Arrangement header.
 // Shift wins if Shift and Option happen to be held together.
 constexpr Action resolveAction(bool shift,bool alt) noexcept
 {
     return shift?Action::clearAll:(alt?Action::exclusive:Action::toggle);
 }

struct Target
{
    Kind kind=Kind::audio;
    int lane=-1;
};
struct State
{
    std::vector<bool> audio;
    std::vector<bool> instruments;
};

inline bool isValid(const State& current,Target target) noexcept
{
    if(target.lane<0)return false;
    const auto& lanes=target.kind==Kind::audio?current.audio:current.instruments;
    return static_cast<size_t>(target.lane)<lanes.size();
}

inline int countActive(const State& state) noexcept
{
    return static_cast<int>(std::count(state.audio.begin(),state.audio.end(),true)
        +std::count(state.instruments.begin(),state.instruments.end(),true));
}

// A normal click only toggles the selected Solo. Option-click isolates the
// selected track across BOTH audio and instrument buses; a second Option-click
// on that already-exclusive track clears Solo. Shift-click clears all Solos.
// Invalid target indices are never allowed to change another track.
inline State apply(State state,Target target,Action action)
{
    if(!isValid(state,target))return state;
    auto& lanes=target.kind==Kind::audio?state.audio:state.instruments;
    const size_t index=static_cast<size_t>(target.lane);
    if(action==Action::clearAll)
    {
        std::fill(state.audio.begin(),state.audio.end(),false);
        std::fill(state.instruments.begin(),state.instruments.end(),false);
    }
    else if(action==Action::exclusive)
    {
        const bool wasExclusive=lanes[index] && countActive(state)==1;
        std::fill(state.audio.begin(),state.audio.end(),false);
        std::fill(state.instruments.begin(),state.instruments.end(),false);
        lanes[index]=!wasExclusive;
    }
    else
        lanes[index]=!lanes[index];
    return state;
}
}
