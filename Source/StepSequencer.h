#pragma once
#include "MidiEngine.h"
#include <array>
#include <cstdint>
#include <vector>

class LibertyStepSequencer final
{
public:
    static constexpr int maxSteps = 64;
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
    };
    enum class Scale : std::uint8_t { Off = 0, Major, Minor, Pentatonic };
    struct Pattern
    {
        bool enabled = false;
        int stepCount = 16;
        std::int64_t stepTicks = MidiEngine::ticksPerQuarterNote / 4;
        float swing = 0.0f;
        std::uint8_t root = 0;
        Scale scale = Scale::Off;
        int transpose = 0;
        float humanize = 0.0f;
        std::array<Step, maxSteps> steps {};
    };

    static std::vector<MidiEngine::NoteEvent> render(const Pattern&, std::int64_t startTick = 0);
};
