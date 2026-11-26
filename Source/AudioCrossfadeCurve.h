#pragma once

#include <algorithm>
#include <cmath>

// Crossfade level laws shared by the real-time audio engine and Arrangement
// preview. Curves are calculated from the normalized overlap position.
// 0: linear, 1: equal power, 2: smooth S (complementary levels).
namespace LibertyAudioCrossfade
{
inline constexpr int customFirst=10, customLast=30, maxCurve=customLast;
inline bool validCurve(int c) noexcept {return c>=0&&c<=maxCurve;}
inline double advancedShape(int curve,double p) noexcept
{
    if(p<=0.0)return 0.0;
    if(p>=1.0)return 1.0;
    switch(curve){
        case 3:return std::log1p(9.0*p)/std::log(10.0);
        case 4:return std::expm1(3.0*p)/std::expm1(3.0);
        case 5:return .5-.5*std::cos(3.14159265358979323846*p);
        case 6:return .5*p+.5*p*p*(3.0-2.0*p);
        case 7:return p*p*p*(10.0+p*(-15.0+6.0*p));
        case 8:return p*p*p;
        case 9:return 1.0-(1.0-p)*(1.0-p)*(1.0-p);
        default:
            if(curve>=customFirst&&curve<=customLast){
                const double bias=(curve-20)*.075;
                // Monotonic rational response, bounded away from zero denominator.
                return p*(1.0+bias)/(1.0+bias*(2.0*p-1.0));
            }
            return p;
    }
}
// Normalized gain for independent clip fades. The existing linear law is
// preserved as the default; additional laws can be selected per clip later.
inline double manualFadeLevel(int curve, double progress, bool incoming) noexcept
{
    const double p = std::clamp(progress, 0.0, 1.0);
    if (curve >= 3 && validCurve(curve)){
        const double shaped=advancedShape(curve,p);
        return incoming?shaped:1.0-shaped;
    }
    if (curve == 1)
        return incoming ? p * p : (1.0 - p) * (1.0 - p);
    if (curve == 2)
    {
        const double smooth = p * p * (3.0 - 2.0 * p);
        return incoming ? smooth : (1.0 - smooth);
    }
    return incoming ? p : (1.0 - p);
}
inline double level(int curve, double progress, bool incoming) noexcept
{
    const double p = std::clamp(progress, 0.0, 1.0);
    if (curve >= 3 && validCurve(curve)){
        const double shaped=advancedShape(curve,p);
        return incoming?shaped:1.0-shaped;
    }
    if (curve == 1)
        return std::sqrt(incoming ? p : (1.0 - p));
    if (curve == 2)
    {
        const double smooth = p * p * (3.0 - 2.0 * p);
        return incoming ? smooth : 1.0 - smooth;
    }
    return incoming ? p : (1.0 - p);
}
}
