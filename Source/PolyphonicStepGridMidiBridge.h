#pragma once
// JUCE-independent Step Sequencer <-> Piano Roll bridge. Source MIDI notes
// are never modified; reject imports which cannot be represented losslessly.
#include "PolyphonicStepGrid.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

namespace LibertyPolyGridMidi
{
template<class MidiNote>
bool importPianoRoll(const std::vector<MidiNote>& notes, LibertyPolyphonicGrid& out,
                     int steps, std::int64_t stepTicks)
{
    using Grid = LibertyPolyphonicGrid;
    if (notes.empty() || steps < 1 || steps > Grid::maxSteps ||
        stepTicks < 1 || stepTicks > Grid::maxStepTicks ||
        notes.size() > (size_t)Grid::laneCount * Grid::maxNotesPerLane)
        return false;
    Grid candidate;
    candidate.stepCount = steps;
    candidate.stepTicks = stepTicks;
    const auto cycle = candidate.cycleTicks();
    if (!cycle) return false;
    std::vector<int> pitches;
    pitches.reserve(Grid::laneCount);
    for (const auto& n : notes)
    {
        const int pitch = (int)n.pitch;
        if (pitch < 0 || pitch > 127 || n.startTick < 0 ||
            n.startTick >= cycle || n.startTick % stepTicks != 0 ||
            n.lengthTicks < 1 || n.lengthTicks > cycle - n.startTick ||
            n.velocity < 1 || n.velocity > 127 || n.channel < 1 || n.channel > 16 ||
            n.probability > 100) return false;
        if (std::find(pitches.begin(), pitches.end(), pitch) == pitches.end())
        {
            if (pitches.size() == Grid::laneCount) return false;
            pitches.push_back(pitch);
        }
    }
    std::sort(pitches.begin(), pitches.end(), std::greater<int>());
    for (size_t row = 0; row < pitches.size(); ++row)
        candidate.lanes[row].pitch = (std::uint8_t)pitches[row];
    for (const auto& n : notes)
    {
        const auto it = std::find(pitches.begin(), pitches.end(), (int)n.pitch);
        if (it == pitches.end()) return false;
        const int row = (int)std::distance(pitches.begin(), it);
        const int step = (int)(n.startTick / stepTicks);
        const auto id = candidate.addNote(row, step, n.lengthTicks,
                                           (int)n.velocity, (int)n.channel, (int)n.probability);
        if (!id) return false; // No silent deletion of duplicate grid positions.
        for (auto& e : candidate.lanes[(size_t)row].events)
            if (e.id == id) { e.muted = n.muted; break; }
    }
    candidate.active = true;
    out = std::move(candidate);
    return true;
}

template<class MidiNote>
bool matchesPianoRoll(const LibertyPolyphonicGrid& grid, const std::vector<MidiNote>& notes)
{
    // Important for round-trip tests: a Piano Roll edit should never silently
    // lose pitch, duration, velocity, chance, channel or note mute.
    auto rendered = grid.render();
    if (rendered.size() != notes.size()) return false;
    for (const auto& n : notes)
    {
        auto it = std::find_if(rendered.begin(), rendered.end(),
            [&](const auto& e)
            {
                return e.startTick == n.startTick &&
                    e.lengthTicks == n.lengthTicks && e.pitch == n.pitch &&
                    e.velocity == n.velocity && e.channel == n.channel &&
                    e.probability == n.probability && e.muted == n.muted;
            });
        if (it == rendered.end()) return false;
        rendered.erase(it);
    }
    return rendered.empty();
}
} // namespace LibertyPolyGridMidi
