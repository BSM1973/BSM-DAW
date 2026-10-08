#include "StepSequencer.h"
#include <algorithm>
#include <cmath>

std::vector<MidiEngine::NoteEvent> LibertyStepSequencer::render(const Pattern& pattern, std::int64_t startTick)
{
    std::vector<MidiEngine::NoteEvent> notes;
    if (!pattern.enabled || pattern.stepTicks <= 0) return notes;
    const int count = std::clamp(pattern.cycleSteps, 1, std::clamp(pattern.stepCount, 1, maxSteps));
    try { notes.reserve((size_t) count * 4u); } catch (...) { return {}; }
    for (int i = 0; i < count; ++i)
    {
        const auto& step = pattern.steps[(size_t)i];
        const int velocityLane = i % std::clamp(pattern.velocityLaneSteps, 1, count);
        const int gateLane = i % std::clamp(pattern.gateLaneSteps, 1, count);
        const int probabilityLane = i % std::clamp(pattern.probabilityLaneSteps, 1, count);
        const int ratchetLane = i % std::clamp(pattern.ratchetLaneSteps, 1, count);
        const auto& velocityStep = pattern.steps[(size_t)velocityLane];
        const auto& gateStep = pattern.steps[(size_t)gateLane];
        const auto& probabilityStep = pattern.steps[(size_t)probabilityLane];
        const auto& ratchetStep = pattern.steps[(size_t)ratchetLane];
        if (!step.enabled || probabilityStep.probability == 0) continue;
        const unsigned hash = (unsigned)(i * 1103515245u + 12345u);
        if ((hash % 100u) >= std::min<unsigned>(100u, probabilityStep.probability)) continue;
        const int ratchets = std::clamp<int>(ratchetStep.ratchet, 1, 8);
        const auto subdivision = std::max<std::int64_t>(1, pattern.stepTicks / ratchets);
        const auto swingOffset = (i & 1) ? (std::int64_t)std::llround((double)pattern.stepTicks * std::clamp((double)pattern.swing, 0.0, 0.75) * 0.5) : 0;
        int pitch = std::clamp((int)step.pitch + step.octave * 12 + std::clamp(pattern.transpose, -12, 12), MidiEngine::minMidiNote, MidiEngine::maxMidiNote);
        if (pattern.scale != Scale::Off)
        {
            static constexpr bool major[12] = {true,false,true,false,true,true,false,true,false,true,false,true};
            static constexpr bool minor[12] = {true,false,true,true,false,true,false,true,true,false,true,false};
            static constexpr bool penta[12] = {true,false,false,true,false,true,false,true,false,false,true,false};
            const bool* allowed = pattern.scale == Scale::Major ? major : (pattern.scale == Scale::Minor ? minor : penta);
            const int root = std::clamp((int)pattern.root, 0, 11);
            auto inScale = [&](int note) { int pc=(note-root)%12; if(pc<0) pc+=12; return allowed[pc]; };
            if (!inScale(pitch))
            {
                for (int distance=1; distance<12; ++distance)
                {
                    const int down=pitch-distance, up=pitch+distance;
                    if (down>=MidiEngine::minMidiNote && inScale(down)) { pitch=down; break; }
                    if (up<=MidiEngine::maxMidiNote && inScale(up)) { pitch=up; break; }
                }
            }
        }
        const unsigned humanHash = (unsigned)(i * 747796405u + 2891336453u);
        const double human = std::clamp((double)pattern.humanize, 0.0, 1.0);
        const int velocityJitter = (int)std::llround((((int)((humanHash >> 8) % 2001u) - 1000) / 1000.0) * human * 12.0);
        const int velocity = std::clamp((int)velocityStep.velocity + (step.accent ? 18 : 0) + velocityJitter, 1, 127);
        const auto microOffset = (std::int64_t)std::llround((double)pattern.stepTicks * std::clamp((double)step.microTiming, -0.5, 0.5));
        const auto humanOffset = (std::int64_t)std::llround((((int)(humanHash % 2001u) - 1000) / 1000.0) * human * (double)pattern.stepTicks * 0.10);
        const auto gateTicks = std::max<std::int64_t>(1, (std::int64_t)std::llround((double)subdivision * std::clamp((double)gateStep.gate, 0.01, 1.0)));
        int intervals[4] = {0, 0, 0, 0};
        int chordNotes = 1;
        switch (step.chord)
        {
            case Chord::Major: intervals[1]=4; intervals[2]=7; chordNotes=3; break;
            case Chord::Minor: intervals[1]=3; intervals[2]=7; chordNotes=3; break;
            case Chord::Power: intervals[1]=7; intervals[2]=12; chordNotes=3; break;
            case Chord::Seventh: intervals[1]=4; intervals[2]=7; intervals[3]=10; chordNotes=4; break;
            default: break;
        }
        for (int r = 0; r < ratchets; ++r)
        {
            for (int chordIndex=0; chordIndex<chordNotes; ++chordIndex)
            {
                MidiEngine::NoteEvent note;
                note.startTick = std::max<std::int64_t>(startTick, startTick + (std::int64_t)i * pattern.stepTicks + swingOffset + microOffset + humanOffset + (std::int64_t)r * subdivision);
                note.lengthTicks = step.tie ? pattern.stepTicks : gateTicks;
                note.pitch = (std::uint8_t)std::clamp(pitch + intervals[chordIndex], MidiEngine::minMidiNote, MidiEngine::maxMidiNote);
                note.velocity = (std::uint8_t)velocity;
                note.channel = (std::uint8_t)std::clamp((int)step.channel, 1, 16);
                notes.push_back(note);
            }
        }
    }
    return notes;
}
