#pragma once
#include <algorithm>
#include <cmath>
namespace LibertyMotion
{
inline float advance(float current,float target,float seconds) noexcept
{
    current=std::clamp(current,0.f,1.f);
    target=std::clamp(target,0.f,1.f);
    const float weight=1.f-std::exp(-std::clamp(seconds,0.f,0.1f)*24.f);
    const float next=current+(target-current)*weight;
    return std::abs(next-target)<0.012f?target:next;
}
inline float pulse(double seconds,double period) noexcept
{
    return static_cast<float>(0.5+0.5*std::sin(6.283185307179586*
        seconds/std::max(0.1,period)));
}
inline float meterSegmentFill(float amount,int segment,int count) noexcept
{
    if(count<=0||segment<0||segment>=count)return 0.f;
    return std::clamp(std::clamp(amount,0.f,1.f)*count-segment,0.f,1.f);
}
}
