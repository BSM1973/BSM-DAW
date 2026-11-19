#pragma once
// Phase 3: JUCE-free transactional editing of the polyphonic Step Sequencer.
// One mouse gesture == one Undo entry; no allocations happen on the audio thread.
#include "PolyphonicStepGrid.h"
#include <algorithm>
#include <cstdint>
#include <vector>

class LibertyPolyGridEditing final
{
public:
    struct Key
    {
        int row = -1;
        std::uint32_t id = 0;
        bool operator==(const Key& b) const noexcept { return row == b.row && id == b.id; }
    };
    enum class Property { Velocity, Offset, Length, Repeat, Pitch, Chance };
    static constexpr size_t maxHistory = 64;

    const std::vector<Key>& selection() const noexcept { return selection_; }
    bool contains(Key key) const noexcept
    {
        return std::find(selection_.begin(), selection_.end(), key) != selection_.end();
    }
    void clearSelection() { selection_.clear(); }
    void select(Key key, bool additive = false)
    {
        if (!additive) selection_.clear();
        if (key.id && !contains(key)) selection_.push_back(key);
    }
    void toggle(Key key)
    {
        auto found = std::find(selection_.begin(), selection_.end(), key);
        if (found != selection_.end()) selection_.erase(found);
        else if (key.id) selection_.push_back(key);
    }
    void selectRectangle(const LibertyPolyphonicGrid& grid, int row0, int step0,
                         int row1, int step1, bool additive = false)
    {
        if (!additive) selection_.clear();
        const int loRow = std::max(0, std::min(row0, row1));
        const int hiRow = std::min(LibertyPolyphonicGrid::laneCount - 1, std::max(row0, row1));
        const int loStep = std::min(step0, step1);
        const int hiStep = std::max(step0, step1);
        for (int row = loRow; row <= hiRow; ++row)
            for (const auto& e : grid.lanes[(size_t)row].events)
            {
                const int endStep = e.startStep + (int)((e.lengthTicks - 1) / grid.stepTicks);
                if (e.startStep <= hiStep && endStep >= loStep)
                    select({row, e.id}, true);
            }
    }
    void pruneSelection(const LibertyPolyphonicGrid& grid)
    {
        selection_.erase(std::remove_if(selection_.begin(), selection_.end(),
            [&](const Key& k) { return find(grid, k) == nullptr; }), selection_.end());
    }

    static const LibertyPolyphonicGrid::Event* find(const LibertyPolyphonicGrid& grid, Key k) noexcept
    {
        const auto* row = grid.laneAt(k.row);
        if (!row) return nullptr;
        for (const auto& e : row->events) if (e.id == k.id) return &e;
        return nullptr;
    }
    static LibertyPolyphonicGrid::Event* find(LibertyPolyphonicGrid& grid, Key k) noexcept
    {
        auto* row = grid.laneAt(k.row);
        if (!row) return nullptr;
        for (auto& e : row->events) if (e.id == k.id) return &e;
        return nullptr;
    }

    static bool same(const LibertyPolyphonicGrid& a, const LibertyPolyphonicGrid& b) noexcept
    {
        if (a.active != b.active || a.stepCount != b.stepCount ||
            a.stepTicks != b.stepTicks) return false;
        for (int row = 0; row < LibertyPolyphonicGrid::laneCount; ++row)
        {
            const auto& x = a.lanes[(size_t)row];
            const auto& y = b.lanes[(size_t)row];
            if (x.pitch != y.pitch || x.enabled != y.enabled || x.muted != y.muted ||
                x.solo != y.solo || x.steps != y.steps || x.rate != y.rate ||
                x.reverse != y.reverse || x.swing != y.swing ||
                x.events.size() != y.events.size()) return false;
            for (size_t i = 0; i < x.events.size(); ++i)
            {
                const auto& e = x.events[i];
                const auto& f = y.events[i];
                if (e.id != f.id || e.startStep != f.startStep || e.lengthTicks != f.lengthTicks ||
                    e.velocity != f.velocity || e.channel != f.channel ||
                    e.probability != f.probability || e.muted != f.muted ||
                    e.offsetTicks != f.offsetTicks || e.repeats != f.repeats ||
                    e.pitchOffset != f.pitchOffset) return false;
            }
        }
        return true;
    }
    void begin(const LibertyPolyphonicGrid& grid)
    {
        if (!transactionActive_)
        {
            before_ = grid;
            transactionActive_ = true;
        }
    }
    bool commit(const LibertyPolyphonicGrid& grid)
    {
        if (!transactionActive_) return false;
        transactionActive_ = false;
        if (same(before_, grid)) return false;
        push(undo_, before_);
        redo_.clear();
        return true;
    }
    void cancel(LibertyPolyphonicGrid& grid)
    {
        if (transactionActive_) grid = before_;
        transactionActive_ = false;
        pruneSelection(grid);
    }
    bool undo(LibertyPolyphonicGrid& grid)
    {
        if (transactionActive_) cancel(grid);
        if (undo_.empty()) return false;
        push(redo_, grid);
        grid = undo_.back();
        undo_.pop_back();
        pruneSelection(grid);
        return true;
    }
    bool redo(LibertyPolyphonicGrid& grid)
    {
        if (transactionActive_) cancel(grid);
        if (redo_.empty()) return false;
        push(undo_, grid);
        grid = redo_.back();
        redo_.pop_back();
        pruneSelection(grid);
        return true;
    }
    bool canUndo() const noexcept { return !undo_.empty(); }
    bool canRedo() const noexcept { return !redo_.empty(); }
    void clearHistory()
    {
        transactionActive_ = false;
        selection_.clear();
        undo_.clear();
        redo_.clear();
    }

    // Every move is derived from the gesture's original snapshot: a back-and-
    // forth drag never accumulates rounding errors or loses event identity.
    bool moveFrom(const LibertyPolyphonicGrid& original,
                  LibertyPolyphonicGrid& current, int deltaRows, int deltaSteps) const
    {
        if (selection_.empty()) return false;
        auto candidate = original;
        for (const auto& k : selection_)
            if (!find(original, k) || !candidate.removeNote(k.row, k.id)) return false;
        for (const auto& k : selection_)
        {
            const auto* source = find(original, k);
            if (!source) return false;
            const int row = k.row + deltaRows;
            const int start = source->startStep + deltaSteps;
            if (row < 0 || row >= LibertyPolyphonicGrid::laneCount ||
                start < 0 || start >= original.stepCount) return false;
            const auto remaining = candidate.cycleTicks() - (std::int64_t)start * candidate.stepTicks;
            if (candidate.noteAt(row, start) || remaining < 1) return false;
            auto moved = *source;
            moved.startStep = start;
            moved.lengthTicks = std::min(moved.lengthTicks, remaining);
            if (!candidate.importNote(row, moved)) return false;
        }
        current = std::move(candidate);
        return true;
    }

    bool resizeFrom(const LibertyPolyphonicGrid& original,
                    LibertyPolyphonicGrid& current, int stepDelta) const
    {
        if (selection_.empty()) return false;
        auto candidate = original;
        for (const auto& key : selection_)
        {
            auto* e = find(candidate, key);
            if (!e) return false;
            const auto limit = candidate.cycleTicks() - (std::int64_t)e->startStep * candidate.stepTicks;
            e->lengthTicks = std::clamp(e->lengthTicks + stepDelta * candidate.stepTicks,
                                        (std::int64_t)1, limit);
        }
        current = std::move(candidate);
        return true;
    }

    bool applyDelta(LibertyPolyphonicGrid& grid, Property property, int delta) const
    {
        bool changed = false;
        for (const auto& key : selection_)
        {
            auto* e = find(grid, key);
            if (!e) continue;
            const auto old = *e;
            switch (property)
            {
                case Property::Velocity:
                    e->velocity = (std::uint8_t)std::clamp((int)e->velocity + delta, 1, 127); break;
                case Property::Offset:
                    e->offsetTicks = std::clamp<std::int64_t>(e->offsetTicks + delta,
                        -grid.stepTicks / 2, grid.stepTicks / 2); break;
                case Property::Length:
                    e->lengthTicks = std::clamp<std::int64_t>(e->lengthTicks + delta,
                        1, grid.cycleTicks() - (std::int64_t)e->startStep * grid.stepTicks); break;
                case Property::Repeat:
                    e->repeats = (std::uint8_t)std::clamp((int)e->repeats + delta, 1, 8); break;
                case Property::Pitch:
                    e->pitchOffset = (std::int8_t)std::clamp((int)e->pitchOffset + delta, -24, 24); break;
                case Property::Chance:
                    e->probability = (std::uint8_t)std::clamp((int)e->probability + delta, 0, 100); break;
            }
            changed = changed || !equalEvent(old, *e);
        }
        return changed;
    }
    bool removeSelected(LibertyPolyphonicGrid& grid)
    {
        bool removed = false;
        for (const auto& k : selection_) removed = grid.removeNote(k.row, k.id) || removed;
        if (removed) selection_.clear();
        return removed;
    }

private:
    static bool equalEvent(const LibertyPolyphonicGrid::Event& e,
                           const LibertyPolyphonicGrid::Event& f) noexcept
    {
        return e.id == f.id && e.startStep == f.startStep && e.lengthTicks == f.lengthTicks &&
            e.velocity == f.velocity && e.channel == f.channel && e.probability == f.probability &&
            e.muted == f.muted && e.offsetTicks == f.offsetTicks &&
            e.repeats == f.repeats && e.pitchOffset == f.pitchOffset;
    }
    static void push(std::vector<LibertyPolyphonicGrid>& stack, const LibertyPolyphonicGrid& grid)
    {
        if (stack.size() >= maxHistory) stack.erase(stack.begin());
        stack.push_back(grid);
    }
    std::vector<Key> selection_;
    std::vector<LibertyPolyphonicGrid> undo_, redo_;
    LibertyPolyphonicGrid before_;
    bool transactionActive_ = false;
};
