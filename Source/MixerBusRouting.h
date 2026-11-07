#pragma once
#include <algorithm>
namespace LibertyMixerBusRouting {
constexpr int maxBuses=8;
inline int destination(int route,int count) noexcept
{
    return route>=0&&route<std::clamp(count,0,maxBuses)?route:-1;
}
inline float leftGain(float gain,float pan) noexcept
{
    return std::clamp(gain,0.f,2.f)*(1.f-std::clamp(pan,0.f,1.f));
}
inline float rightGain(float gain,float pan) noexcept
{
    return std::clamp(gain,0.f,2.f)*(1.f+std::clamp(pan,-1.f,0.f));
}
inline bool busAudible(bool muted,bool solo,bool anySolo) noexcept
{
    return !muted&&(!anySolo||solo);
}
}