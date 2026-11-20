#pragma once
// Accurate gain-fader graduation for the compact Mixer in the lower dock.
// Coordinates, spacing and gain conversion are shared by paint, mouse and tests.
// All labels use plain ASCII "-" to avoid corrupted typographic minus glyphs.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace LibertyDockMixerDbScale
{
constexpr float floorDb = -60.0f;
constexpr float ceilingDb = 6.020599913f; // 2.0 x linear gain.
constexpr int labelHeight = 12;
constexpr int minLabelSpacing = 14;

inline float gainToDb(float gain) noexcept
{
    if (!std::isfinite(gain) || gain <= 0.0f) return floorDb;
    return std::clamp(20.0f * std::log10(gain), floorDb, ceilingDb);
}
inline float gainToPosition(float gain) noexcept
{
    if (!std::isfinite(gain) || gain <= 0.0f) return 0.0f;
    return std::clamp((gainToDb(gain) - floorDb) / (ceilingDb - floorDb), 0.0f, 1.0f);
}
inline float positionToGain(float position) noexcept
{
    if (!std::isfinite(position) || position <= 0.0f) return 0.0f; // Silence
    if (position >= 1.0f) return 2.0f;
    return std::pow(10.0f, (floorDb + position * (ceilingDb - floorDb)) / 20.0f);
}

struct Graduation
{
    float db = 0.0f;
    const char* label = "";
    bool major = false;
    int y = 0;
    bool showLabel = false;
};
// Exact -60 to +6 dB values; horizontal marks are at the SAME position
// used by the gain fader. Marks do not represent audio meter dBFS readings.
constexpr std::array<Graduation, 16> ticks = {{
    {ceilingDb, "+6", true}, {0.0f, "0", true},
    {-3.0f, "", false}, {-6.0f, "-6", true},
    {-9.0f, "", false}, {-12.0f, "-12", true},
    {-15.0f, "", false}, {-18.0f, "-18", true},
    {-21.0f, "", false}, {-24.0f, "-24", true},
    {-30.0f, "", false}, {-36.0f, "-36", true},
    {-42.0f, "", false}, {-48.0f, "-48", true},
    {-54.0f, "", false}, {-60.0f, "-60", true}
}};

inline std::array<Graduation, ticks.size()> atPixels(int top, int bottom) noexcept
{
    auto marks = ticks;
    if (bottom <= top) return marks;
    for (auto& mark : marks)
    {
        const float fraction = (mark.db - floorDb) / (ceilingDb - floorDb);
        mark.y = top + (int)std::lround((1.0f - fraction) * (bottom - top));
    }
    // Fixed visual priorities: 0 dB (unity), -12, -24, -36, -48, -60,
    // then intermediate ticks and +6 when they fit without overlapping.
    constexpr int order[] = {1, 5, 9, 11, 13, 15, 3, 7, 0};
    for (const int index : order)
    {
        if (bottom - top < labelHeight) break;
        auto& candidate = marks[(std::size_t)index];
        bool fits = true;
        for (const auto& placed : marks)
            if (placed.showLabel &&
                std::abs(candidate.y - placed.y) < minLabelSpacing)
            {
                fits = false;
                break;
            }
        candidate.showLabel = fits;
    }
    return marks;
}
} // namespace LibertyDockMixerDbScale
