#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace LibertyPerformClip
{
struct Settings
{
    float gainDb = 0.0f;
    int transpose = 0;
    float inFraction = 0.0f;
    float outFraction = 1.0f;
    bool loop = true;
    int quantization = -1; // -1: global, 0: immediate, 1: beat, 2: bar
    int launchMode = 0; // 0: restart, 1: toggle
    int midiBars = 0; // 0: original length
};
inline Settings validated(Settings s) noexcept
{
    if (!std::isfinite(s.gainDb)) s.gainDb = 0.0f;
    if (!std::isfinite(s.inFraction)) s.inFraction = 0.0f;
    if (!std::isfinite(s.outFraction)) s.outFraction = 1.0f;
    s.gainDb = std::clamp(s.gainDb, -24.0f, 12.0f);
    s.transpose = std::clamp(s.transpose, -24, 24);
    s.inFraction = std::clamp(s.inFraction, 0.0f, 0.99f);
    s.outFraction = std::clamp(s.outFraction, s.inFraction + 0.01f, 1.0f);
    s.quantization = std::clamp(s.quantization, -1, 2);
    s.launchMode = std::clamp(s.launchMode, 0, 1);
    if (s.midiBars != 0 && s.midiBars != 1 && s.midiBars != 2 &&
        s.midiBars != 4 && s.midiBars != 8 && s.midiBars != 16) s.midiBars = 0;
    return s;
}
inline float linearGain(float db) noexcept
{
    return std::pow(10.0f, std::clamp(std::isfinite(db) ? db : 0.0f, -24.0f, 12.0f) / 20.0f);
}
inline int notePitch(int original, int transpose) noexcept
{
    return std::clamp(original + std::clamp(transpose, -24, 24), 0, 127);
}
inline int launchQuantization(int global, int overrideValue) noexcept
{
    return std::clamp(overrideValue < 0 ? global : overrideValue, 0, 2);
}
inline int launchedScene(int active, int pressed, int mode) noexcept
{
    return mode == 1 && active == pressed ? -1 : pressed;
}
inline std::int64_t midiLengthTicks(std::int64_t original, int bars,
                                   int numerator, int denominator) noexcept
{
    if (bars <= 0) return std::max<std::int64_t>(1, original);
    const auto measureTicks = std::max<std::int64_t>(1,
        (std::int64_t)std::max(1,numerator) * 960 * 4 / std::max(1,denominator));
    return (std::int64_t)bars * measureTicks;
}
struct AudioSlice { int first = 0; int length = 0; };
inline AudioSlice audioSlice(int total, Settings s) noexcept
{
    if (total <= 0) return {};
    s = validated(s);
    const int first = std::clamp((int)std::floor(total * s.inFraction), 0, total-1);
    const int last = std::clamp((int)std::ceil(total * s.outFraction), first+1, total);
    return {first, last-first};
}
}
