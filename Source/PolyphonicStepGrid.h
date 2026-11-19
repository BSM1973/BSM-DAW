#pragma once
// Polyphonic step-grid data model. No JUCE/audio-thread dependency.
// Version 1: eight note lanes, independent event lengths and simultaneous starts.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

class LibertyPolyphonicGrid final
{
public:
    static constexpr int laneCount = 8;
    static constexpr int maxSteps = 64;
    static constexpr int maxNotesPerLane = 64;
    static constexpr std::int64_t maxStepTicks = 1000000000LL;
    static constexpr std::uint32_t maxId = 0x7fffffffU;

    struct Event
    {
        std::uint32_t id = 0; // Stable across editing and project serialization.
        int startStep = 0;
        std::int64_t lengthTicks = 240; // Musical duration, independent of other events.
        std::uint8_t velocity = 100;
        std::uint8_t channel = 1;
        std::uint8_t probability = 100;
        bool muted = false;
    };
    struct Lane
    {
        std::uint8_t pitch = 60;
        bool enabled = true;
        bool muted = false;
        bool solo = false;
        std::vector<Event> events;
    };
    struct RenderedNote
    {
        std::uint32_t id = 0;
        std::int64_t startTick = 0;
        std::int64_t lengthTicks = 1;
        std::uint8_t pitch = 60, velocity = 100, channel = 1, probability = 100;
    };

    // The legacy single-note Pattern remains active unless explicitly switched.
    bool active = false;
    int stepCount = 16;
    std::int64_t stepTicks = 240; // 1/16 note at 960 PPQ.
    // C4, B3, A3, G3, F3, E3, D3, C3 as in the supplied video.
    std::array<Lane, laneCount> lanes {{
        Lane {72, true, false, false, {}}, Lane {71, true, false, false, {}},
        Lane {69, true, false, false, {}}, Lane {67, true, false, false, {}},
        Lane {65, true, false, false, {}}, Lane {64, true, false, false, {}},
        Lane {62, true, false, false, {}}, Lane {60, true, false, false, {}}
    }};

    std::int64_t cycleTicks() const noexcept
    {
        if (stepCount < 1 || stepCount > maxSteps ||
            stepTicks < 1 || stepTicks > maxStepTicks) return 0;
        return (std::int64_t)stepCount * stepTicks;
    }

    bool hasNotes() const noexcept
    {
        for (const auto& lane : lanes)
            if (!lane.events.empty()) return true;
        return false;
    }

    const Lane* laneAt(int row) const noexcept
    {
        return row >= 0 && row < laneCount ? &lanes[(size_t)row] : nullptr;
    }
    Lane* laneAt(int row) noexcept
    {
        return row >= 0 && row < laneCount ? &lanes[(size_t)row] : nullptr;
    }

    const Event* noteAt(int row, int step) const noexcept
    {
        const auto* lane = laneAt(row);
        if (!lane) return nullptr;
        for (const auto& e : lane->events)
            if (e.startStep == step) return &e;
        return nullptr;
    }

    std::uint32_t addNote(int row, int step, std::int64_t length,
                          int velocity = 100, int channel = 1, int chance = 100)
    {
        auto* lane = laneAt(row);
        const auto cycle = cycleTicks();
        if (!lane || cycle == 0 || step < 0 || step >= stepCount ||
            length <= 0 || velocity < 1 || velocity > 127 ||
            channel < 1 || channel > 16 || chance < 0 || chance > 100 ||
            (int)lane->events.size() >= maxNotesPerLane || noteAt(row, step))
            return 0;
        const auto id = nextFreeId();
        if (id == 0) return 0;
        const auto lastTick = cycle - (std::int64_t)step * stepTicks;
        lane->events.push_back({ id, step, std::min(length, lastTick),
                                 (std::uint8_t)velocity, (std::uint8_t)channel,
                                 (std::uint8_t)chance, false });
        return id;
    }

    bool changeLength(int row, std::uint32_t id, std::int64_t length) noexcept
    {
        auto* lane = laneAt(row);
        const auto cycle = cycleTicks();
        if (!lane || cycle == 0 || length < 1) return false;
        for (auto& e : lane->events)
            if (e.id == id)
            {
                e.lengthTicks = std::min(length, cycle - (std::int64_t)e.startStep * stepTicks);
                return true;
            }
        return false;
    }

    bool removeNote(int row, std::uint32_t id)
    {
        auto* lane = laneAt(row);
        if (!lane || id == 0) return false;
        const auto oldSize = lane->events.size();
        lane->events.erase(std::remove_if(lane->events.begin(), lane->events.end(),
            [id](const Event& e) { return e.id == id; }), lane->events.end());
        return lane->events.size() != oldSize;
    }

    // Validated import used by the project reader. An unknown/malformed note
    // is rejected individually and never corrupts an existing lane or ID.
    bool importNote(int row, const Event& e)
    {
        auto* lane = laneAt(row);
        const auto cycle = cycleTicks();
        if (!lane || cycle == 0 || e.id == 0 || e.id > maxId ||
            e.startStep < 0 || e.startStep >= stepCount ||
            e.lengthTicks < 1 || e.lengthTicks > cycle - (std::int64_t)e.startStep * stepTicks ||
            e.velocity < 1 || e.channel < 1 || e.channel > 16 ||
            e.probability > 100 || (int)lane->events.size() >= maxNotesPerLane ||
            noteAt(row, e.startStep) != nullptr || containsId(e.id))
            return false;
        lane->events.push_back(e);
        if (nextId_ <= e.id) nextId_ = e.id == maxId ? 1 : e.id + 1;
        return true;
    }

    std::vector<RenderedNote> render(std::int64_t startTick = 0) const
    {
        std::vector<RenderedNote> result;
        const auto cycle = cycleTicks();
        if (!active || cycle == 0 || startTick < 0 ||
            startTick > std::numeric_limits<std::int64_t>::max() - cycle)
            return result;
        bool anySolo = false;
        for (const auto& lane : lanes)
            anySolo = anySolo || (lane.enabled && !lane.muted && lane.solo);
        result.reserve((size_t)laneCount * maxNotesPerLane);
        for (const auto& lane : lanes)
        {
            if (!lane.enabled || lane.muted || (anySolo && !lane.solo)) continue;
            for (const auto& e : lane.events)
            {
                if (e.muted || e.probability == 0 || e.id == 0 ||
                    e.startStep < 0 || e.startStep >= stepCount || e.lengthTicks < 1)
                    continue;
                const auto relativeStart = (std::int64_t)e.startStep * stepTicks;
                const auto remaining = cycle - relativeStart;
                if (remaining < 1) continue;
                result.push_back({ e.id, startTick + relativeStart,
                                   std::min(e.lengthTicks, remaining), lane.pitch,
                                   e.velocity, e.channel, e.probability });
            }
        }
        std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b)
        {
            if (a.startTick != b.startTick) return a.startTick < b.startTick;
            if (a.pitch != b.pitch) return a.pitch < b.pitch;
            return a.id < b.id;
        });
        return result;
    }

private:
    std::uint32_t nextId_ = 1;
    bool containsId(std::uint32_t id) const noexcept
    {
        for (const auto& lane : lanes)
            for (const auto& note : lane.events)
                if (note.id == id) return true;
        return false;
    }
    std::uint32_t nextFreeId() noexcept
    {
        for (int attempts = 0; attempts < laneCount * maxNotesPerLane + 1; ++attempts)
        {
            const auto candidate = nextId_;
            nextId_ = candidate >= maxId ? 1 : candidate + 1;
            if (candidate != 0 && !containsId(candidate)) return candidate;
        }
        return 0;
    }
};
