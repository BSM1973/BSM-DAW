#include "StepSequencer.h"
#include "MainComponent.h"
#include <algorithm>
#include <cmath>
#include <limits>

std::int64_t LibertyStepSequencer::getCycleLengthTicks(const Pattern& pattern) noexcept
{
    if (pattern.stepTicks <= 0) return 0;
    const int count = std::clamp(pattern.cycleSteps, 1, std::clamp(pattern.stepCount, 1, maxSteps));
    const auto base = std::max<std::int64_t>(1, pattern.stepTicks);
    const auto step = pattern.rateModifier == 1 ? std::max<std::int64_t>(1, base * 2 / 3)
                    : pattern.rateModifier == 2 ? std::max<std::int64_t>(1, base * 3 / 2)
                    : base;
    const int positions = pattern.direction == Direction::PingPong && count > 1 ? count * 2 - 2 : count;
    if (positions <= 0 || step > std::numeric_limits<std::int64_t>::max() / positions) return 0;
    return step * positions;
}

std::vector<MidiEngine::NoteEvent> LibertyStepSequencer::render(const Pattern& pattern, std::int64_t startTick)
{
    std::vector<MidiEngine::NoteEvent> notes;
    if (!pattern.enabled || pattern.stepTicks <= 0) return notes;
    const int count = std::clamp(pattern.cycleSteps, 1, std::clamp(pattern.stepCount, 1, maxSteps));
    const auto baseStepTicks = std::max<std::int64_t>(1, pattern.stepTicks);
    const auto effectiveStepTicks = pattern.rateModifier == 1 ? std::max<std::int64_t>(1, baseStepTicks * 2 / 3)
                                  : pattern.rateModifier == 2 ? std::max<std::int64_t>(1, baseStepTicks * 3 / 2)
                                  : baseStepTicks;
    const int outputCount = pattern.direction == Direction::PingPong && count > 1 ? count * 2 - 2 : count;
    try { notes.reserve((size_t) outputCount * 4u); } catch (...) { return {}; }
    auto sourceIndexForPosition = [&](int position)
    {
        if (pattern.direction == Direction::Reverse) return count - 1 - (position % count);
        if (pattern.direction == Direction::PingPong && count > 1)
        {
            const int period = count * 2 - 2;
            const int phase = position % period;
            return phase < count ? phase : period - phase;
        }
        if (pattern.direction == Direction::Random)
            return (int)(((unsigned)position * 2654435761u + 1013904223u) % (unsigned)count);
        return position % count;
    };
    for (int i = 0; i < outputCount; ++i)
    {
        const int sourceIndex = sourceIndexForPosition(i);
        const auto& step = pattern.steps[(size_t)sourceIndex];
        const int velocityLane = sourceIndex % std::clamp(pattern.velocityLaneSteps, 1, count);
        const int gateLane = sourceIndex % std::clamp(pattern.gateLaneSteps, 1, count);
        const int probabilityLane = sourceIndex % std::clamp(pattern.probabilityLaneSteps, 1, count);
        const int ratchetLane = sourceIndex % std::clamp(pattern.ratchetLaneSteps, 1, count);
        const auto& velocityStep = pattern.steps[(size_t)velocityLane];
        const auto& gateStep = pattern.steps[(size_t)gateLane];
        const auto& probabilityStep = pattern.steps[(size_t)probabilityLane];
        const auto& ratchetStep = pattern.steps[(size_t)ratchetLane];
        if (!step.enabled || probabilityStep.probability == 0) continue;
        const unsigned hash = (unsigned)(i * 1103515245u + 12345u);
        if ((hash % 100u) >= std::min<unsigned>(100u, probabilityStep.probability)) continue;
        int tiedSteps = 1;
        if (step.tie)
        {
            while (i + tiedSteps < outputCount)
            {
                const auto& previous = pattern.steps[(size_t)sourceIndexForPosition(i + tiedSteps - 1)];
                const auto& next = pattern.steps[(size_t)sourceIndexForPosition(i + tiedSteps)];
                if (!previous.tie || !next.enabled || next.pitch != step.pitch || next.octave != step.octave
                    || next.channel != step.channel || next.chord != step.chord)
                    break;
                ++tiedSteps;
            }
        }
        const bool hasTieContinuation = tiedSteps > 1;
        const int ratchets = hasTieContinuation ? 1 : std::clamp<int>(ratchetStep.ratchet, 1, 8);
        const auto subdivision = std::max<std::int64_t>(1, effectiveStepTicks / ratchets);
        const auto swingOffset = (i & 1) ? (std::int64_t)std::llround((double)effectiveStepTicks * std::clamp((double)pattern.swing, 0.0, 0.75) * 0.5) : 0;
        int pitch = std::clamp((int)step.pitch + step.octave * 12 + std::clamp(pattern.octaveShift, -4, 4) * 12 + std::clamp(pattern.transpose, -12, 12), MidiEngine::minMidiNote, MidiEngine::maxMidiNote);
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
        const auto microOffset = (std::int64_t)std::llround((double)effectiveStepTicks * std::clamp((double)step.microTiming, -0.5, 0.5));
        const auto humanOffset = (std::int64_t)std::llround((((int)(humanHash % 2001u) - 1000) / 1000.0) * human * (double)effectiveStepTicks * 0.10);
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
                note.startTick = std::max<std::int64_t>(startTick, startTick + (std::int64_t)i * effectiveStepTicks + swingOffset + microOffset + humanOffset + (std::int64_t)r * subdivision);
                note.lengthTicks = hasTieContinuation ? effectiveStepTicks * tiedSteps : gateTicks;
                note.pitch = (std::uint8_t)std::clamp(pitch + intervals[chordIndex], MidiEngine::minMidiNote, MidiEngine::maxMidiNote);
                note.velocity = (std::uint8_t)velocity;
                note.channel = (std::uint8_t)std::clamp((int)step.channel, 1, 16);
                notes.push_back(note);
            }
        }
        if (hasTieContinuation)
            i += tiedSteps - 1;
    }
    return notes;
}

void MainComponent::saveStepSequencers(juce::XmlElement& root) const
{
    auto* all = root.createNewChildElement("StepSequencers");
    for (size_t lane = 0; lane < instrumentStepSequencers.size(); ++lane)
    {
        const auto& bank = instrumentStepSequencers[lane];
        auto* track = all->createNewChildElement("Track");
        track->setAttribute("lane", (int) lane);
        track->setAttribute("activePattern", juce::jlimit(0, 7, bank.activePattern));
        for (int pi = 0; pi < 8; ++pi)
        {
            const auto& p = bank.patterns[(size_t) pi];
            auto* pe = track->createNewChildElement("Pattern");
            pe->setAttribute("index", pi); pe->setAttribute("enabled", p.enabled);
            pe->setAttribute("stepCount", p.stepCount); pe->setAttribute("cycleSteps", p.cycleSteps);
            pe->setAttribute("stepTicks", (double) p.stepTicks); pe->setAttribute("rateModifier", (int) p.rateModifier);
            pe->setAttribute("direction", (int) p.direction); pe->setAttribute("swing", (double) p.swing);
            pe->setAttribute("root", (int) p.root); pe->setAttribute("scale", (int) p.scale);
            pe->setAttribute("transpose", p.transpose); pe->setAttribute("octaveShift", p.octaveShift);
            pe->setAttribute("humanize", (double) p.humanize); pe->setAttribute("euclideanPulses", p.euclideanPulses);
            pe->setAttribute("euclideanRotation", p.euclideanRotation); pe->setAttribute("velocityLaneSteps", p.velocityLaneSteps);
            pe->setAttribute("gateLaneSteps", p.gateLaneSteps); pe->setAttribute("probabilityLaneSteps", p.probabilityLaneSteps);
            pe->setAttribute("ratchetLaneSteps", p.ratchetLaneSteps);
            for (int si = 0; si < LibertyStepSequencer::maxSteps; ++si)
            {
                const auto& st = p.steps[(size_t) si];
                auto* se = pe->createNewChildElement("Step"); se->setAttribute("index", si);
                se->setAttribute("enabled", st.enabled); se->setAttribute("pitch", (int) st.pitch);
                se->setAttribute("velocity", (int) st.velocity); se->setAttribute("channel", (int) st.channel);
                se->setAttribute("gate", (double) st.gate); se->setAttribute("probability", (int) st.probability);
                se->setAttribute("ratchet", (int) st.ratchet); se->setAttribute("tie", st.tie);
                se->setAttribute("accent", st.accent); se->setAttribute("octave", st.octave);
                se->setAttribute("microTiming", (double) st.microTiming); se->setAttribute("chord", (int) st.chord);
            }
        }
        for (const auto& clip : bank.timelineClips)
        {
            auto* ce = track->createNewChildElement("TimelineClip");
            ce->setAttribute("name", clip.name);
            // Store musical coordinates alongside legacy seconds. Ticks preserve
            // the exact Pattern placement when a project is reopened at its BPM.
            ce->setAttribute("startSeconds", clip.startSeconds);
            ce->setAttribute("lengthSeconds", clip.lengthSeconds);
            ce->setAttribute("startTick", (double) MidiEngine::secondsToTick(clip.startSeconds, tempoBpm));
            ce->setAttribute("lengthTicks", (double) MidiEngine::secondsToTick(clip.lengthSeconds, tempoBpm));
            for (const auto& note : clip.notes)
            {
                auto* ne = ce->createNewChildElement("Note");
                ne->setAttribute("startTick", (double) note.startTick);
                ne->setAttribute("lengthTicks", (double) note.lengthTicks);
                ne->setAttribute("pitch", (int) note.pitch);
                ne->setAttribute("velocity", (int) note.velocity);
                ne->setAttribute("channel", (int) note.channel);
            }
        }
    }
}

void MainComponent::loadStepSequencers(const juce::XmlElement& root)
{
    const auto* all = root.getChildByName("StepSequencers");
    // Always discard the previous project's arrangement, including legacy projects
    // which do not contain the StepSequencers XML section.
    for (size_t lane = 0; lane < instrumentStepSequencers.size(); ++lane)
    {
        instrumentStepSequencers[lane].timelineClips.clear();
        publishInstrumentArrangementClips((int) lane);
    }
    if (all == nullptr) return;
    for (auto* track = all->getFirstChildElement(); track; track = track->getNextElement())
    {
        if (track->getTagName() != "Track") continue;
        const int lane = track->getIntAttribute("lane", -1);
        if (lane < 0 || lane >= getInstrumentTrackCount() || (size_t) lane >= instrumentStepSequencers.size()) continue;
        auto restored = instrumentStepSequencers[(size_t) lane];
        restored.activePattern = juce::jlimit(0, 7, track->getIntAttribute("activePattern", 0));
        for (auto* pe = track->getFirstChildElement(); pe; pe = pe->getNextElement())
        {
            if (pe->getTagName() != "Pattern") continue;
            const int pi = pe->getIntAttribute("index", -1); if (pi < 0 || pi >= 8) continue;
            auto p = restored.patterns[(size_t) pi];
            p.enabled = pe->getBoolAttribute("enabled", p.enabled);
            p.stepCount = juce::jlimit(1, LibertyStepSequencer::maxSteps, pe->getIntAttribute("stepCount", p.stepCount));
            p.cycleSteps = juce::jlimit(1, p.stepCount, pe->getIntAttribute("cycleSteps", p.cycleSteps));
            const double ticks = pe->getDoubleAttribute("stepTicks", (double) p.stepTicks);
            if (std::isfinite(ticks) && ticks >= 1.0 && ticks <= (double) std::numeric_limits<std::int64_t>::max()) p.stepTicks = (std::int64_t) std::llround(ticks);
            p.rateModifier = (std::uint8_t) juce::jlimit(0, 2, pe->getIntAttribute("rateModifier", p.rateModifier));
            p.direction = (LibertyStepSequencer::Direction) juce::jlimit(0, 3, pe->getIntAttribute("direction", (int)p.direction));
            p.swing = (float) juce::jlimit(0.0, 0.75, pe->getDoubleAttribute("swing", p.swing));
            p.root = (std::uint8_t) juce::jlimit(0, 11, pe->getIntAttribute("root", p.root));
            p.scale = (LibertyStepSequencer::Scale) juce::jlimit(0, 3, pe->getIntAttribute("scale", (int)p.scale));
            p.transpose = juce::jlimit(-12, 12, pe->getIntAttribute("transpose", p.transpose));
            p.octaveShift = juce::jlimit(-4, 4, pe->getIntAttribute("octaveShift", p.octaveShift));
            p.humanize = (float) juce::jlimit(0.0, 1.0, pe->getDoubleAttribute("humanize", p.humanize));
            p.euclideanPulses = juce::jlimit(1, p.stepCount, pe->getIntAttribute("euclideanPulses", p.euclideanPulses));
            p.euclideanRotation = juce::jlimit(0, p.stepCount - 1, pe->getIntAttribute("euclideanRotation", p.euclideanRotation));
            p.velocityLaneSteps = juce::jlimit(1, p.cycleSteps, pe->getIntAttribute("velocityLaneSteps", p.velocityLaneSteps));
            p.gateLaneSteps = juce::jlimit(1, p.cycleSteps, pe->getIntAttribute("gateLaneSteps", p.gateLaneSteps));
            p.probabilityLaneSteps = juce::jlimit(1, p.cycleSteps, pe->getIntAttribute("probabilityLaneSteps", p.probabilityLaneSteps));
            p.ratchetLaneSteps = juce::jlimit(1, p.cycleSteps, pe->getIntAttribute("ratchetLaneSteps", p.ratchetLaneSteps));
            for (auto* se = pe->getFirstChildElement(); se; se = se->getNextElement())
            {
                if (se->getTagName() != "Step") continue;
                const int si = se->getIntAttribute("index", -1); if (si < 0 || si >= LibertyStepSequencer::maxSteps) continue;
                auto st = p.steps[(size_t) si];
                st.enabled = se->getBoolAttribute("enabled", st.enabled);
                st.pitch = (std::uint8_t) juce::jlimit(0, 127, se->getIntAttribute("pitch", st.pitch));
                st.velocity = (std::uint8_t) juce::jlimit(1, 127, se->getIntAttribute("velocity", st.velocity));
                st.channel = (std::uint8_t) juce::jlimit(1, 16, se->getIntAttribute("channel", st.channel));
                st.gate = (float) juce::jlimit(0.01, 1.0, se->getDoubleAttribute("gate", st.gate));
                st.probability = (std::uint8_t) juce::jlimit(0, 100, se->getIntAttribute("probability", st.probability));
                st.ratchet = (std::uint8_t) juce::jlimit(1, 8, se->getIntAttribute("ratchet", st.ratchet));
                st.tie = se->getBoolAttribute("tie", st.tie); st.accent = se->getBoolAttribute("accent", st.accent);
                st.octave = juce::jlimit(-2, 2, se->getIntAttribute("octave", st.octave));
                st.microTiming = (float) juce::jlimit(-0.5, 0.5, se->getDoubleAttribute("microTiming", st.microTiming));
                st.chord = (LibertyStepSequencer::Chord) juce::jlimit(0, 4, se->getIntAttribute("chord", (int)st.chord));
                p.steps[(size_t) si] = st;
            }
            restored.patterns[(size_t) pi] = p;
        }
        restored.timelineClips.clear();
        for (auto* ce = track->getFirstChildElement(); ce; ce = ce->getNextElement())
        {
            if (ce->getTagName() != "TimelineClip") continue;
            if (restored.timelineClips.size() >= 2048) break;
            InstrumentStepSequencerBank::TimelinePatternClip clip;
            clip.name = ce->getStringAttribute("name", "Pattern").trim().substring(0, 64);
            if (clip.name.isEmpty()) clip.name = "Pattern";
            clip.startSeconds = ce->getDoubleAttribute("startSeconds", 0.0);
            clip.lengthSeconds = ce->getDoubleAttribute("lengthSeconds", 0.0);
            // Prefer the musical coordinates for new projects; older projects
            // still load from their original seconds attributes unchanged.
            if (ce->hasAttribute("startTick") && ce->hasAttribute("lengthTicks"))
            {
                const double startTick = ce->getDoubleAttribute("startTick", -1.0);
                const double lengthTicks = ce->getDoubleAttribute("lengthTicks", -1.0);
                // A damaged or partially written tick pair should not erase a
                // valid clip: fall back to the legacy seconds attributes.
                if (std::isfinite(startTick) && std::isfinite(lengthTicks)
                    && startTick >= 0.0 && lengthTicks >= 1.0
                    && startTick <= 1.0e12 && lengthTicks <= 1.0e12)
                {
                    clip.startSeconds = MidiEngine::tickToSeconds((std::int64_t) std::llround(startTick), tempoBpm);
                    clip.lengthSeconds = MidiEngine::tickToSeconds((std::int64_t) std::llround(lengthTicks), tempoBpm);
                }
            }
            if (!std::isfinite(clip.startSeconds) || !std::isfinite(clip.lengthSeconds)
                || clip.startSeconds < 0.0 || clip.lengthSeconds <= 0.0) continue;
            // Reject timeline values that cannot be represented safely as MIDI
            // ticks. This also guards against damaged legacy seconds values.
            constexpr double maxTimelineTicks = 1.0e12;
            const double maxTimelineSeconds =
                MidiEngine::tickToSeconds((std::int64_t) maxTimelineTicks, tempoBpm);
            if (clip.startSeconds > maxTimelineSeconds
                || clip.lengthSeconds > maxTimelineSeconds) continue;
            for (auto* ne = ce->getFirstChildElement(); ne; ne = ne->getNextElement())
            {
                if (ne->getTagName() != "Note") continue;
                if (clip.notes.size() >= 8192) break;
                const double start = ne->getDoubleAttribute("startTick", -1.0);
                const double length = ne->getDoubleAttribute("lengthTicks", -1.0);
                if (!std::isfinite(start) || !std::isfinite(length) || start < 0.0
                    || length < 1.0 || start > 1.0e12 || length > 1.0e12) continue;
                MidiEngine::NoteEvent note;
                note.startTick = (std::int64_t) std::llround(start);
                note.lengthTicks = (std::int64_t) std::llround(length);
                note.pitch = (std::uint8_t) juce::jlimit(0, 127, ne->getIntAttribute("pitch", 60));
                note.velocity = (std::uint8_t) juce::jlimit(1, 127, ne->getIntAttribute("velocity", 100));
                note.channel = (std::uint8_t) juce::jlimit(1, 16, ne->getIntAttribute("channel", 1));
                clip.notes.push_back(note);
            }
            restored.timelineClips.push_back(std::move(clip));
        }
        instrumentStepSequencers[(size_t) lane] = std::move(restored);
        publishInstrumentStepSequencer(lane);
        publishInstrumentArrangementClips(lane);
    }
}
