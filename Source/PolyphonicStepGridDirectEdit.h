#pragma once
// Direct, JUCE-free note interaction contract shared by UI and CI.
// Horizontal drag moves notes, vertical drag changes the selected property,
// and the existing right-edge handle continues to resize notes.
#include "PolyphonicStepGridEditing.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>

namespace LibertyPolyGridDirect
{
using Property = LibertyPolyGridEditing::Property;

enum class DragIntent { Pending, Move, Parameter };

inline DragIntent chooseIntent(int deltaX, int deltaY, bool forceMove = false) noexcept
{
    const auto ax = std::llabs((std::int64_t)deltaX);
    const auto ay = std::llabs((std::int64_t)deltaY);
    if (ax < 5 && ay < 5) return DragIntent::Pending;
    if (forceMove) return DragIntent::Move;
    // Slight diagonal hand movement must not accidentally change pitch rows
    // or values. Decide one direction only once per mouse gesture.
    return ay >= 5 && ay * 5 >= ax * 6 ? DragIntent::Parameter
                                       : DragIntent::Move;
}

inline int deltaForVerticalDrag(Property property, int upwardPixels,
                                std::int64_t stepTicks, bool fine = false) noexcept
{
    // Signed integer division gives stable symmetric steps, with no drag drift.
    const std::int64_t ticks = std::max<std::int64_t>(1, stepTicks);
    std::int64_t amount = 1;
    int pixels = 2;
    switch (property)
    {
        case Property::Velocity: amount = 1; pixels = fine ? 6 : 2; break;
        case Property::Chance:   amount = 1; pixels = fine ? 6 : 2; break;
        case Property::Offset:
            amount = std::max<std::int64_t>(1, ticks / (fine ? 96 : 24));
            pixels = 6;
            break;
        case Property::Length:
            amount = std::max<std::int64_t>(1, ticks / (fine ? 16 : 4));
            pixels = 12;
            break;
        case Property::Repeat: amount = 1; pixels = fine ? 28 : 14; break;
        case Property::Pitch:  amount = 1; pixels = fine ? 28 : 12; break;
    }
    const std::int64_t steps = std::clamp((std::int64_t)upwardPixels,
                                          (std::int64_t)-4096, (std::int64_t)4096) / pixels;
    const std::int64_t safeMax = std::numeric_limits<int>::max();
    return (int)std::clamp(steps * amount, -safeMax, safeMax);
}

inline std::string signedText(std::int64_t value)
{
    return (value > 0 ? "+" : "") + std::to_string(value);
}

inline std::string noteValue(const LibertyPolyphonicGrid::Event& note,
                             Property property, std::int64_t stepTicks)
{
    switch (property)
    {
        case Property::Velocity: return std::to_string(note.velocity);
        case Property::Offset:   return signedText(note.offsetTicks) + " t";
        case Property::Length:
            if (stepTicks > 0 && note.lengthTicks % stepTicks == 0)
                return std::to_string(note.lengthTicks / stepTicks) + " pas";
            return std::to_string(note.lengthTicks) + " t";
        case Property::Repeat:   return "x" + std::to_string(note.repeats);
        case Property::Pitch:    return signedText(note.pitchOffset) + " st";
        case Property::Chance:   return std::to_string(note.probability) + "%";
    }
    return {};
}

inline float normalizedValue(const LibertyPolyphonicGrid& grid,
                              const LibertyPolyphonicGrid::Event& note,
                              Property property) noexcept
{
    switch (property)
    {
        case Property::Velocity: return (float)note.velocity / 127.0f;
        case Property::Offset: return std::clamp(0.5f +
            (float)note.offsetTicks / (float)std::max<std::int64_t>(1, grid.stepTicks), 0.0f, 1.0f);
        case Property::Length: return std::clamp((float)note.lengthTicks /
            (float)std::max<std::int64_t>(1, grid.cycleTicks()), 0.0f, 1.0f);
        case Property::Repeat: return (float)note.repeats / 8.0f;
        case Property::Pitch: return std::clamp(((float)note.pitchOffset + 24.0f) / 48.0f, 0.0f, 1.0f);
        case Property::Chance: return (float)note.probability / 100.0f;
    }
    return 0.0f;
}
} // namespace LibertyPolyGridDirect
