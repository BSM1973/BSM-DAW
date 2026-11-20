#pragma once
#include "MidiEngine.h"
#include <array>
#include <cstdint>
#include <vector>

class LibertyStepSequencer final
{
public:
    static constexpr int maxSteps = 64;
    enum class Chord : std::uint8_t { Off = 0, Major, Minor, Power, Seventh };
    struct Step
    {
        bool enabled = false;
        std::uint8_t pitch = 60;
        std::uint8_t velocity = 100;
        std::uint8_t channel = 1;
        float gate = 0.80f;
        std::uint8_t probability = 100;
        std::uint8_t ratchet = 1;
        bool tie = false;
        bool accent = false;
        int octave = 0;
        float microTiming = 0.0f;
        Chord chord = Chord::Off;
    };
    enum class Scale : std::uint8_t { Off = 0, Major, Minor, Pentatonic };
    enum class Direction : std::uint8_t { Forward = 0, Reverse, PingPong, Random };
    struct Pattern
    {
        bool enabled = false;
        int stepCount = 16;
        int cycleSteps = 16;
        std::int64_t stepTicks = MidiEngine::ticksPerQuarterNote / 4;
        std::uint8_t rateModifier = 0; // 0 straight, 1 triplet, 2 dotted
        Direction direction = Direction::Forward;
        float swing = 0.0f;
        std::uint8_t root = 0;
        Scale scale = Scale::Off;
        int transpose = 0;
        int octaveShift = 0;
        float humanize = 0.0f;
        int euclideanPulses = 4;
        int euclideanRotation = 0;
        int velocityLaneSteps = 16;
        int gateLaneSteps = 16;
        int probabilityLaneSteps = 16;
        int ratchetLaneSteps = 16;
        std::array<Step, maxSteps> steps {};
    };

    static std::int64_t getCycleLengthTicks(const Pattern&) noexcept;
    static std::vector<MidiEngine::NoteEvent> render(const Pattern&, std::int64_t startTick = 0);
};
