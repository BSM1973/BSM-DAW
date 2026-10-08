#include "StepSequencer.h"
#include <algorithm>
#include <cmath>

std::vector<MidiEngine::NoteEvent> LibertyStepSequencer::render(const Pattern& pattern, std::int64_t startTick)
{
    std::vector<MidiEngine::NoteEvent> notes;
    if (!pattern.enabled || pattern.stepTicks <= 0) return notes;
    const int count = std::clamp(pattern.stepCount, 1, maxSteps);
    try { notes.reserve((size_t) count * 4u); } catch (...) { return {}; }
    for (int i = 0; i < count; ++i)
    {
        const auto& step = pattern.steps[(size_t)i];
        if (!step.enabled || step.probability == 0) continue;
        const unsigned hash = (unsigned)(i * 1103515245u + 12345u);
        if ((hash % 100u) >= std::min<unsigned>(100u, step.probability)) continue;
        const int ratchets = std::clamp<int>(step.ratchet, 1, 8);
        const auto subdivision = std::max<std::int64_t>(1, pattern.stepTicks / ratchets);
        const auto swingOffset = (i & 1) ? (std::int64_t)std::llround((double)pattern.stepTicks * std::clamp((double)pattern.swing, 0.0, 0.75) * 0.5) : 0;
        const int pitch = std::clamp((int)step.pitch + step.octave * 12, MidiEngine::minMidiNote, MidiEngine::maxMidiNote);
        const int velocity = std::clamp((int)step.velocity + (step.accent ? 18 : 0), 1, 127);
        const auto gateTicks = std::max<std::int64_t>(1, (std::int64_t)std::llround((double)subdivision * std::clamp((double)step.gate, 0.01, 1.0)));
        for (int r = 0; r < ratchets; ++r)
        {
            MidiEngine::NoteEvent note;
            note.startTick = startTick + (std::int64_t)i * pattern.stepTicks + swingOffset + (std::int64_t)r * subdivision;
            note.lengthTicks = step.tie ? pattern.stepTicks : gateTicks;
            note.pitch = (std::uint8_t)pitch; note.velocity = (std::uint8_t)velocity;
            note.channel = (std::uint8_t)std::clamp((int)step.channel, 1, 16);
            notes.push_back(note);
        }
    }
    return notes;
}
