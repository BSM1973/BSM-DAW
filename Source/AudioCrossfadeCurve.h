#pragma once

#include <algorithm>
#include <cmath>

// Crossfade level laws shared by the real-time audio engine and Arrangement
// preview. Curves are calculated from the normalized overlap position.
// 0: linear, 1: equal power, 2: smooth S (complementary levels).
namespace LibertyAudioCrossfade
{
inline double level(int curve, double progress, bool incoming) noexcept
{
    const double p = std::clamp(progress, 0.0, 1.0);
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
