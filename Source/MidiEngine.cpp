#include "MidiEngine.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
bool checkedAdd(std::int64_t a, std::int64_t b, std::int64_t& result) noexcept
{
    if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
        (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b))
        return false;
    result = a + b;
    return true;
}
}


bool MidiEngine::setControllerEvent(std::int64_t tick, int controller, int value, int channel)
{
    if (tick < 0 || channel < 1 || channel > 16 || controller < 0 || controller > 129
        || value < 0 || value > (controller == 128 ? 16383 : 127))
        return false;
    const auto it = std::find_if(controllerEvents.begin(), controllerEvents.end(),
        [=](const ControllerEvent& e) { return e.tick == tick && e.controller == controller && e.channel == channel; });
    if (it != controllerEvents.end())
    {
        if (it->value == value) return true;
        it->value = value;
        return true;
    }
    if (controllerEvents.size() >= 65536) return false;
    ControllerEvent event;
    event.tick = tick;
    event.channel = static_cast<std::uint8_t>(channel);
    event.controller = controller;
    event.value = value;
    controllerEvents.push_back(event);
    std::sort(controllerEvents.begin(), controllerEvents.end(), [](const ControllerEvent& a, const ControllerEvent& b)
    {
        if (a.tick != b.tick) return a.tick < b.tick;
        if (a.channel != b.channel) return a.channel < b.channel;
        return a.controller < b.controller;
    });
    return true;
}

std::vector<MidiEngine::ControllerEvent> MidiEngine::getControllerEventsCopy() const
{
    return controllerEvents;
}

bool MidiEngine::removeControllerEvent(std::int64_t tick, int controller, int channel)
{
    const auto it = std::find_if(controllerEvents.begin(), controllerEvents.end(),
        [=](const ControllerEvent& event)
        { return event.tick == tick && event.controller == controller && event.channel == channel; });
    if (it == controllerEvents.end()) return false;
    controllerEvents.erase(it);
    return true;
}

void MidiEngine::clearControllerEvents()
{
    controllerEvents.clear();
}

MidiEngine::HistoryState MidiEngine::makeHistoryState() const
{
    return { notes, controllerEvents, selectedNotes };
}

void MidiEngine::beginGroupedEdit() noexcept
{
    groupedEditActive = true;
    groupedEditHasUndo = false;
}

void MidiEngine::endGroupedEdit() noexcept
{
    groupedEditActive = false;
    groupedEditHasUndo = false;
}

void MidiEngine::pushUndoState()
{
    if (groupedEditActive && groupedEditHasUndo) return;
    if (groupedEditActive) groupedEditHasUndo = true;
    ++historyRevision;
    undoHistory.push_back(makeHistoryState());
    redoHistory.clear();
    constexpr std::size_t maxHistory = 100;
    if (undoHistory.size() > maxHistory)
        undoHistory.erase(undoHistory.begin());
}

void MidiEngine::restoreHistoryState(const HistoryState& state)
{
    notes = state.notes;
    controllerEvents = state.controllerEvents;
    selectedNotes = state.selectedNotes;
    if (selectedNotes.empty())
    {
        selectedNoteValid = false;
        selectedNote = {};
        return;
    }
    selectedNote = selectedNotes.back();
    selectedNoteValid = true;
}

bool MidiEngine::undo()
{
    if (undoHistory.empty()) return false;
    ++historyRevision;
    redoHistory.push_back(makeHistoryState());
    auto state = std::move(undoHistory.back());
    undoHistory.pop_back();
    restoreHistoryState(state);
    return true;
}

bool MidiEngine::redo()
{
    if (redoHistory.empty()) return false;
    ++historyRevision;
    undoHistory.push_back(makeHistoryState());
    auto state = std::move(redoHistory.back());
    redoHistory.pop_back();
    restoreHistoryState(state);
    return true;
}

void MidiEngine::clearUndoHistory() noexcept
{
    // Clearing the stacks invalidates any editor-owned undo token, even when
    // the visible MIDI notes have not changed.
    ++historyRevision;
    undoHistory.clear();
    redoHistory.clear();
}

void MidiEngine::clear()
{
    if (notes.empty() && controllerEvents.empty()) return;
    pushUndoState();
    controllerEvents.clear();
    notes.clear();
    clearNoteSelection();
    setPlaybackPositionSeconds(0.0);
    setPlaying(false);
}

bool MidiEngine::addNote(std::int64_t startTick, std::int64_t lengthTicks, int pitch, int velocity, int channel)
{
    std::int64_t noteEnd = 0;
    if (startTick < 0 || lengthTicks <= 0 || !checkedAdd(startTick, lengthTicks, noteEnd) || pitch < minMidiNote || pitch > maxMidiNote || velocity < 1 || velocity > 127 || channel < 1 || channel > 16) return false;
    NoteEvent note;
    note.startTick = startTick; note.lengthTicks = lengthTicks; note.pitch = static_cast<std::uint8_t>(pitch); note.velocity = static_cast<std::uint8_t>(velocity); note.channel = static_cast<std::uint8_t>(channel);
    const auto duplicate = std::find_if(notes.begin(), notes.end(), [note](const NoteEvent& existing)
    { return existing.startTick == note.startTick && existing.pitch == note.pitch && existing.channel == note.channel; });
    if (duplicate != notes.end()) return false;
    pushUndoState();
    const auto insertionPoint = std::lower_bound(notes.begin(), notes.end(), note, [](const NoteEvent& a, const NoteEvent& b)
    { if (a.startTick != b.startTick) return a.startTick < b.startTick; if (a.channel != b.channel) return a.channel < b.channel; return a.pitch < b.pitch; });
    notes.insert(insertionPoint, note);
    selectedNote = note; selectedNoteValid = true;
    selectedNotes.clear(); selectedNotes.push_back(note);
    return true;
}

bool MidiEngine::replaceNotes(const std::vector<NoteEvent>& replacement)
{
    std::vector<NoteEvent> prepared;
    try { prepared = replacement; } catch (...) { return false; }
    for (const auto& note : prepared)
    {
        std::int64_t noteEnd = 0;
        if (note.startTick < 0 || note.lengthTicks <= 0 || !checkedAdd(note.startTick, note.lengthTicks, noteEnd)
            || note.pitch > maxMidiNote || note.velocity < 1 || note.velocity > 127 || note.channel < 1 || note.channel > 16)
            return false;
    }
    std::sort(prepared.begin(), prepared.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (a.startTick != b.startTick) return a.startTick < b.startTick;
        if (a.channel != b.channel) return a.channel < b.channel;
        return a.pitch < b.pitch;
    });
    pushUndoState();
    notes.swap(prepared);
    clearNoteSelection();
    setPlaybackPositionSeconds(0.0);
    setPlaying(false);
    return true;
}

bool MidiEngine::selectNoteAt(std::int64_t startTick, int pitch, int channel) noexcept
{
    const auto it = std::find_if(notes.begin(), notes.end(), [=](const NoteEvent& note)
    { return note.startTick == startTick && note.pitch == static_cast<std::uint8_t>(pitch) && note.channel == static_cast<std::uint8_t>(channel); });
    if (it == notes.end()) return false;
    selectedNote = *it; selectedNoteValid = true; selectedNotes.clear(); selectedNotes.push_back(*it); return true;
}

void MidiEngine::clearNoteSelection() noexcept { selectedNoteValid = false; selectedNote = {}; selectedNotes.clear(); }

bool MidiEngine::toggleNoteSelectionAt(std::int64_t startTick, int pitch, int channel) noexcept
{
    const auto noteIt = std::find_if(notes.begin(), notes.end(), [=](const NoteEvent& note)
    { return note.startTick == startTick && note.pitch == static_cast<std::uint8_t>(pitch) && note.channel == static_cast<std::uint8_t>(channel); });
    if (noteIt == notes.end()) return false;
    const auto selectedIt = std::find_if(selectedNotes.begin(), selectedNotes.end(), [=](const NoteEvent& note)
    { return note.startTick == startTick && note.pitch == static_cast<std::uint8_t>(pitch) && note.channel == static_cast<std::uint8_t>(channel); });
    if (selectedIt != selectedNotes.end()) selectedNotes.erase(selectedIt); else selectedNotes.push_back(*noteIt);
    if (selectedNotes.empty()) { clearNoteSelection(); return true; }
    selectedNote = selectedNotes.back(); selectedNoteValid = true; return true;
}

bool MidiEngine::isNoteSelected(const NoteEvent& note) const noexcept
{
    return std::find_if(selectedNotes.begin(), selectedNotes.end(), [&note](const NoteEvent& selected)
    { return selected.startTick == note.startTick && selected.pitch == note.pitch && selected.channel == note.channel; }) != selectedNotes.end();
}

std::vector<MidiEngine::NoteEvent> MidiEngine::getSelectedNotesCopy() const { return selectedNotes; }

void MidiEngine::setSelectedNotes(const std::vector<NoteEvent>& selection) noexcept
{
    selectedNotes.clear();
    for (const auto& candidate : selection)
    {
        const auto it = std::find_if(notes.begin(), notes.end(), [&candidate](const NoteEvent& note)
        { return note.startTick == candidate.startTick && note.pitch == candidate.pitch && note.channel == candidate.channel; });
        if (it != notes.end()) selectedNotes.push_back(*it);
    }
    if (selectedNotes.empty()) { clearNoteSelection(); return; }
    selectedNote = selectedNotes.back(); selectedNoteValid = true;
}

bool MidiEngine::moveSelectedNotesBy(std::int64_t deltaTicks, int deltaPitch)
{
    if (selectedNotes.empty()) return false;
    // The editor applies the active global SNAP step; the engine must also
    // accept finer tick deltas (including unsnapped movements).
    std::vector<NoteEvent> source = selectedNotes;
    for (const auto& n : source)
    {
        std::int64_t newStart = 0;
        if (!checkedAdd(n.startTick, deltaTicks, newStart)) return false;
        const int newPitch = static_cast<int>(n.pitch) + deltaPitch;
        if (newStart < 0 || newPitch < minMidiNote || newPitch > maxMidiNote) return false;
        const bool collision = std::any_of(notes.begin(), notes.end(), [&n, newStart, newPitch, this](const NoteEvent& other)
        { return !isNoteSelected(other) && other.startTick == newStart && other.pitch == static_cast<std::uint8_t>(newPitch) && other.channel == n.channel; });
        if (collision) return false;
    }
    // Reject target collisions within the moving group as well. Checking only
    // against unselected events would miss two selected notes converging on
    // the same tick, pitch and MIDI channel.
    for (std::size_t i = 0; i < source.size(); ++i)
    {
        std::int64_t targetTick = 0;
        if (!checkedAdd(source[i].startTick, deltaTicks, targetTick))
            return false;
        const int targetPitch = static_cast<int>(source[i].pitch) + deltaPitch;
        for (std::size_t j = i + 1; j < source.size(); ++j)
        {
            std::int64_t otherTick = 0;
            if (!checkedAdd(source[j].startTick, deltaTicks, otherTick))
                return false;
            if (targetTick == otherTick
                && targetPitch == static_cast<int>(source[j].pitch) + deltaPitch
                && source[i].channel == source[j].channel)
                return false;
        }
    }
    // Resolve every selected event before mutating any note. A stale selection
    // must never produce a partially moved chord.
    std::vector<std::size_t> indices;
    indices.reserve(source.size());
    for (const auto& n : source)
    {
        const auto it = std::find_if(notes.begin(), notes.end(), [&n](const NoteEvent& other)
        { return other.startTick == n.startTick && other.pitch == n.pitch && other.channel == n.channel; });
        if (it == notes.end())
            return false;
        const auto index = static_cast<std::size_t>(std::distance(notes.begin(), it));
        // A duplicated selection entry must not mutate the same note twice.
        if (std::find(indices.begin(), indices.end(), index) != indices.end())
            return false;
        indices.push_back(index);
    }
    pushUndoState();
    for (const auto index : indices)
    {
        auto& note = notes[index];
        note.startTick += deltaTicks;
        note.pitch = static_cast<std::uint8_t>(static_cast<int>(note.pitch) + deltaPitch);
    }
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    { if (a.startTick != b.startTick) return a.startTick < b.startTick; if (a.channel != b.channel) return a.channel < b.channel; return a.pitch < b.pitch; });
    auto result = source;
    for (auto& n : result) { n.startTick += deltaTicks; n.pitch = static_cast<std::uint8_t>(static_cast<int>(n.pitch) + deltaPitch); }
    setSelectedNotes(result);
    return true;
}

bool MidiEngine::strumSelectedNotes(std::int64_t spreadTicks, bool lowToHigh, int velocityVariation, int timingVariation)
{
    if (spreadTicks <= 0 || selectedNotes.size() < 2 || velocityVariation < 0 || velocityVariation > 50 || timingVariation < 0 || timingVariation > 50) return false;
    auto updated = notes;
    auto selection = selectedNotes;
    std::sort(selection.begin(), selection.end(), [lowToHigh](const NoteEvent& a, const NoteEvent& b)
    {
        if (a.startTick != b.startTick) return a.startTick < b.startTick;
        return lowToHigh ? a.pitch < b.pitch : a.pitch > b.pitch;
    });
    bool changed = false;
    std::vector<NoteEvent> shifted;
    for (std::size_t begin = 0; begin < selection.size();)
    {
        std::size_t end = begin + 1;
        while (end < selection.size() && selection[end].startTick == selection[begin].startTick) ++end;
        for (std::size_t i = begin; i < end; ++i)
        {
            const auto offsetIndex = i - begin;
            if (offsetIndex > (std::size_t)(std::numeric_limits<std::int64_t>::max() / spreadTicks)) return false;
            auto offset = (std::int64_t)offsetIndex * spreadTicks;
            if (timingVariation > 0 && offsetIndex > 0)
            {
                // Deterministic variation within a bounded percentage of the
                // interval. The first note remains anchored to the grid.
                const auto jitterLimit = (spreadTicks / 100) * timingVariation
                    + (spreadTicks % 100) * timingVariation / 100;
                const auto variation = (std::int64_t)((offsetIndex * 13 + (std::size_t)selection[i].pitch * 3) % 101) - 50;
                const auto jitter = jitterLimit * variation / 50;
                if (jitter < 0 && offset < -jitter) offset = 0;
                else offset += jitter;
            }
            auto moved = selection[i];
            if (!checkedAdd(moved.startTick, offset, moved.startTick)) return false;
            const auto original = std::find_if(updated.begin(), updated.end(), [&](const NoteEvent& n)
            { return n.startTick == selection[i].startTick && n.pitch == selection[i].pitch && n.channel == selection[i].channel; });
            if (original == updated.end()) return false;
            original->startTick = moved.startTick;
            if (velocityVariation > 0)
            {
                const int deviation = (int)((offsetIndex * 17 + (std::size_t)moved.pitch * 7) % (std::size_t)(2 * velocityVariation + 1)) - velocityVariation;
                moved.velocity = (std::uint8_t)std::clamp((int)moved.velocity + deviation, 1, 127);
                original->velocity = moved.velocity;
            }
            shifted.push_back(moved);
            changed = changed || offset != 0;
        }
        begin = end;
    }
    if (!changed) return false;
    std::sort(updated.begin(), updated.end(), [](const NoteEvent& a, const NoteEvent& b)
    { if (a.startTick != b.startTick) return a.startTick < b.startTick;
      if (a.channel != b.channel) return a.channel < b.channel;
      return a.pitch < b.pitch; });
    for (std::size_t i = 1; i < updated.size(); ++i)
        if (updated[i].startTick == updated[i - 1].startTick
            && updated[i].pitch == updated[i - 1].pitch
            && updated[i].channel == updated[i - 1].channel) return false;
    pushUndoState();
    notes.swap(updated);
    setSelectedNotes(shifted);
    return true;
}

bool MidiEngine::splitSelectedNotesToGrid(std::int64_t gridTicks)
{
    if (gridTicks <= 0 || selectedNotes.empty()) return false;
    auto replacement = notes;
    std::vector<NoteEvent> newSelection;
    bool changed = false;
    for (const auto& selected : selectedNotes)
    {
        const auto it = std::find_if(replacement.begin(), replacement.end(), [&](const NoteEvent& n)
        { return n.startTick == selected.startTick && n.pitch == selected.pitch && n.channel == selected.channel; });
        if (it == replacement.end() || it->lengthTicks <= gridTicks) continue;
        std::int64_t end = 0;
        if (!checkedAdd(it->startTick, it->lengthTicks, end)) return false;
        std::vector<NoteEvent> pieces;
        auto cursor = it->startTick;
        while (cursor < end)
        {
            const auto remaining = end - cursor;
            const auto length = std::min(gridTicks, remaining);
            auto piece = *it;
            piece.startTick = cursor;
            piece.lengthTicks = length;
            pieces.push_back(piece);
            cursor += length;
            if (pieces.size() > 4096) return false;
        }
        replacement.erase(it);
        replacement.insert(replacement.end(), pieces.begin(), pieces.end());
        newSelection.insert(newSelection.end(), pieces.begin(), pieces.end());
        changed = true;
    }
    if (!changed) return false;
    std::sort(replacement.begin(), replacement.end(), [](const NoteEvent& a, const NoteEvent& b)
    { if (a.startTick != b.startTick) return a.startTick < b.startTick;
      if (a.channel != b.channel) return a.channel < b.channel;
      return a.pitch < b.pitch; });
    // Refuse duplicate note identities rather than overwriting existing notes.
    for (std::size_t i = 1; i < replacement.size(); ++i)
        if (replacement[i - 1].startTick == replacement[i].startTick
            && replacement[i - 1].pitch == replacement[i].pitch
            && replacement[i - 1].channel == replacement[i].channel) return false;
    pushUndoState();
    notes.swap(replacement);
    setSelectedNotes(newSelection);
    return true;
}

bool MidiEngine::resizeSelectedNotesBy(std::int64_t deltaTicks, bool fromLeftEdge)
{
    if (selectedNotes.empty() || deltaTicks == 0)
        return false;

    constexpr std::int64_t minimumLength = ticksPerQuarterNote / 4;
    const auto source = selectedNotes;

    for (const auto& n : source)
    {
        if (fromLeftEdge)
        {
            std::int64_t newStart = 0, newLength = 0;
            if (!checkedAdd(n.startTick, deltaTicks, newStart) || !checkedAdd(n.lengthTicks, -deltaTicks, newLength)) return false;
            if (newStart < 0 || newLength < minimumLength)
                return false;
        }
        else
        {
            std::int64_t newLength = 0;
            if (!checkedAdd(n.lengthTicks, deltaTicks, newLength) || newLength < minimumLength)
                return false;
        }
    }

    // Resizing from the left changes note onset. Reject duplicate target
    // onsets on the same pitch/channel before changing any live events.
    if (fromLeftEdge)
    {
        for (const auto& n : source)
        {
            std::int64_t targetTick = 0;
            if (!checkedAdd(n.startTick, deltaTicks, targetTick))
                return false;
            const bool collision = std::any_of(notes.begin(), notes.end(),
                [this, &n, targetTick](const NoteEvent& other)
                {
                    return !isNoteSelected(other) && other.startTick == targetTick
                        && other.pitch == n.pitch && other.channel == n.channel;
                });
            if (collision)
                return false;
        }
    }

    // Validate every selected note against the live event collection before
    // changing anything, so a stale selection cannot cause a partial resize.
    std::vector<std::size_t> indices;
    indices.reserve(source.size());
    for (const auto& n : source)
    {
        const auto it = std::find_if(notes.begin(), notes.end(), [&n](const NoteEvent& other)
        {
            return other.startTick == n.startTick && other.pitch == n.pitch && other.channel == n.channel;
        });
        if (it == notes.end())
            return false;
        const auto index = static_cast<std::size_t>(std::distance(notes.begin(), it));
        // A duplicated selection entry must not mutate the same note twice.
        if (std::find(indices.begin(), indices.end(), index) != indices.end())
            return false;
        indices.push_back(index);
    }

    pushUndoState();
    std::vector<NoteEvent> result;
    result.reserve(source.size());
    for (const auto index : indices)
    {
        auto& note = notes[index];
        if (fromLeftEdge)
        {
            note.startTick += deltaTicks;
            note.lengthTicks -= deltaTicks;
        }
        else
        {
            note.lengthTicks += deltaTicks;
        }
        result.push_back(note);
    }

    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    {
        if (a.startTick != b.startTick) return a.startTick < b.startTick;
        if (a.channel != b.channel) return a.channel < b.channel;
        return a.pitch < b.pitch;
    });
    setSelectedNotes(result);
    return true;
}

bool MidiEngine::duplicateSelectedNotes(std::int64_t deltaTicks)
{
    if (selectedNotes.empty() || deltaTicks == 0) return false;
    const auto source = selectedNotes;
    // Ensure the selection still refers to actual events. A stale selection
    // must not create new notes from events that no longer exist.
    for (const auto& n : source)
    {
        const bool exists = std::any_of(notes.begin(), notes.end(), [&n](const NoteEvent& other)
        {
            return other.startTick == n.startTick && other.pitch == n.pitch
                && other.channel == n.channel;
        });
        if (!exists)
            return false;
    }
    std::vector<NoteEvent> copies;
    for (const auto& n : source)
    {
        std::int64_t newStart = 0;
        if (!checkedAdd(n.startTick, deltaTicks, newStart) || newStart < 0) return false;
        if (std::any_of(notes.begin(), notes.end(), [&n, newStart](const NoteEvent& other)
            { return other.startTick == newStart && other.pitch == n.pitch && other.channel == n.channel; })) return false;
        // Also check copies produced earlier in this operation. The live notes
        // collection alone cannot detect duplicates inside the new batch.
        if (std::any_of(copies.begin(), copies.end(), [&n, newStart](const NoteEvent& other)
            { return other.startTick == newStart && other.pitch == n.pitch && other.channel == n.channel; }))
            return false;
        auto copy = n; copy.startTick = newStart; copies.push_back(copy);
    }
    pushUndoState();
    for (const auto& copy : copies) notes.push_back(copy);
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    { if (a.startTick != b.startTick) return a.startTick < b.startTick; if (a.channel != b.channel) return a.channel < b.channel; return a.pitch < b.pitch; });
    setSelectedNotes(copies);
    return true;
}

bool MidiEngine::deleteSelectedNotes()
{
    if (selectedNotes.empty()) return false;
    const auto selected = selectedNotes;
    pushUndoState();
    notes.erase(std::remove_if(notes.begin(), notes.end(), [&selected](const NoteEvent& note)
    { return std::find_if(selected.begin(), selected.end(), [&note](const NoteEvent& s) { return s.startTick == note.startTick && s.pitch == note.pitch && s.channel == note.channel; }) != selected.end(); }), notes.end());
    clearNoteSelection(); return true;
}

bool MidiEngine::removeNoteAt(std::int64_t startTick, int pitch, int channel) { return selectNoteAt(startTick, pitch, channel); }

bool MidiEngine::deleteSelectedNote()
{
    if (!selectedNoteValid) return false;
    const auto it = std::find_if(notes.begin(), notes.end(), [this](const NoteEvent& note)
    { return note.startTick == selectedNote.startTick && note.pitch == selectedNote.pitch && note.channel == selectedNote.channel; });
    if (it == notes.end()) { clearNoteSelection(); return false; }
    pushUndoState();
    notes.erase(it); clearNoteSelection(); return true;
}

bool MidiEngine::moveNote(std::int64_t oldStartTick, int oldPitch, int channel, std::int64_t newStartTick, int newPitch)
{
    if (newStartTick < 0 || newPitch < minMidiNote || newPitch > maxMidiNote || channel < 1 || channel > 16) return false;
    const auto it = std::find_if(notes.begin(), notes.end(), [=](const NoteEvent& note)
    { return note.startTick == oldStartTick && note.pitch == static_cast<std::uint8_t>(oldPitch) && note.channel == static_cast<std::uint8_t>(channel); });
    if (it == notes.end()) return false;
    std::int64_t movedEnd = 0;
    if (!checkedAdd(newStartTick, it->lengthTicks, movedEnd)) return false;
    const auto duplicate = std::find_if(notes.begin(), notes.end(), [=](const NoteEvent& note)
    { return &note != &(*it) && note.startTick == newStartTick && note.pitch == static_cast<std::uint8_t>(newPitch) && note.channel == static_cast<std::uint8_t>(channel); });
    if (duplicate != notes.end()) return false;
    pushUndoState();
    const auto length = it->lengthTicks; const auto velocity = it->velocity;
    it->startTick = newStartTick; it->pitch = static_cast<std::uint8_t>(newPitch);
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    { if (a.startTick != b.startTick) return a.startTick < b.startTick; if (a.channel != b.channel) return a.channel < b.channel; return a.pitch < b.pitch; });
    selectedNote = { newStartTick, length, static_cast<std::uint8_t>(newPitch), velocity, static_cast<std::uint8_t>(channel) };
    selectedNoteValid = true; selectedNotes.clear(); selectedNotes.push_back(selectedNote);
    return true;
}

bool MidiEngine::setNoteLength(std::int64_t startTick, int pitch, int channel, std::int64_t newLengthTicks)
{
    std::int64_t resizedEnd = 0;
    if (startTick < 0 || pitch < minMidiNote || pitch > maxMidiNote || channel < 1 || channel > 16 || newLengthTicks <= 0 || !checkedAdd(startTick, newLengthTicks, resizedEnd)) return false;
    const auto it = std::find_if(notes.begin(), notes.end(), [=](const NoteEvent& note)
    { return note.startTick == startTick && note.pitch == static_cast<std::uint8_t>(pitch) && note.channel == static_cast<std::uint8_t>(channel); });
    if (it == notes.end() || it->lengthTicks == newLengthTicks) return false;
    pushUndoState();
    it->lengthTicks = newLengthTicks; selectNoteAt(startTick, pitch, channel); selectedNote.lengthTicks = newLengthTicks; return true;
}

bool MidiEngine::resizeAdjacentNoteBoundary(std::int64_t startTick, int pitch, int channel,
                                            std::int64_t newLengthTicks)
{
    if (newLengthTicks < ticksPerQuarterNote / 4) return false;
    const auto first = std::find_if(notes.begin(), notes.end(), [=](const NoteEvent& n)
    { return n.startTick == startTick && n.pitch == pitch && n.channel == channel; });
    if (first == notes.end()) return false;
    std::int64_t oldBoundary = 0, newBoundary = 0, followingEnd = 0;
    if (!checkedAdd(startTick, first->lengthTicks, oldBoundary)
        || !checkedAdd(startTick, newLengthTicks, newBoundary)) return false;
    const auto following = std::find_if(notes.begin(), notes.end(), [=](const NoteEvent& n)
    { return n.startTick == oldBoundary && n.pitch == pitch && n.channel == channel; });
    if (following == notes.end()
        || !checkedAdd(following->startTick, following->lengthTicks, followingEnd)
        || newBoundary >= followingEnd - ticksPerQuarterNote / 4
        || newBoundary <= startTick
        || newBoundary == oldBoundary) return false;
    pushUndoState();
    first->lengthTicks = newLengthTicks;
    following->startTick = newBoundary;
    following->lengthTicks = followingEnd - newBoundary;
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    { if (a.startTick != b.startTick) return a.startTick < b.startTick;
      if (a.channel != b.channel) return a.channel < b.channel;
      return a.pitch < b.pitch; });
    selectNoteAt(startTick, pitch, channel);
    return true;
}

bool MidiEngine::setNoteVelocity(std::int64_t startTick, int pitch, int channel, int newVelocity)
{
    if (startTick < 0 || pitch < minMidiNote || pitch > maxMidiNote || channel < 1 || channel > 16 || newVelocity < 1 || newVelocity > 127) return false;
    const auto it = std::find_if(notes.begin(), notes.end(), [=](const NoteEvent& note)
    { return note.startTick == startTick && note.pitch == static_cast<std::uint8_t>(pitch) && note.channel == static_cast<std::uint8_t>(channel); });
    if (it == notes.end() || it->velocity == static_cast<std::uint8_t>(newVelocity)) return false;
    pushUndoState();
    it->velocity = static_cast<std::uint8_t>(newVelocity); selectNoteAt(startTick, pitch, channel); selectedNote.velocity = static_cast<std::uint8_t>(newVelocity); return true;
}

std::vector<MidiEngine::NoteEvent> MidiEngine::getNotesCopy() const { return notes; }
std::int64_t MidiEngine::getLengthTicks() const noexcept { std::int64_t length = 0; for (const auto& note : notes) { std::int64_t end = 0; if (!checkedAdd(note.startTick, note.lengthTicks, end)) return std::numeric_limits<std::int64_t>::max(); length = std::max(length, end); } return length; }
double MidiEngine::tickToSeconds(std::int64_t tick, double tempoBpm) noexcept { if (tick <= 0 || !std::isfinite(tempoBpm) || tempoBpm <= 0.0) return 0.0; return (static_cast<double>(tick) / static_cast<double>(ticksPerQuarterNote)) * (60.0 / tempoBpm); }
std::int64_t MidiEngine::secondsToTick(double seconds, double tempoBpm) noexcept { if (!std::isfinite(seconds) || !std::isfinite(tempoBpm) || seconds <= 0.0 || tempoBpm <= 0.0) return 0; const double ticks=seconds*tempoBpm/60.0*static_cast<double>(ticksPerQuarterNote); const double maxTick=static_cast<double>(std::numeric_limits<std::int64_t>::max()); if (!std::isfinite(ticks) || ticks >= maxTick) return std::numeric_limits<std::int64_t>::max(); return static_cast<std::int64_t>(std::llround(ticks)); }
std::int64_t MidiEngine::quantizeTick(std::int64_t tick, std::int64_t gridTicks) noexcept { if (tick <= 0 || gridTicks <= 0) return std::max<std::int64_t>(0, tick); const double rounded=std::round(static_cast<double>(tick)/static_cast<double>(gridTicks)); const double maxMultiple=static_cast<double>(std::numeric_limits<std::int64_t>::max()/gridTicks); if (!std::isfinite(rounded) || rounded>=maxMultiple) return std::numeric_limits<std::int64_t>::max(); return static_cast<std::int64_t>(rounded)*gridTicks; }
std::int64_t MidiEngine::ticksPerMeasure(int numerator, int denominator) noexcept { if (numerator <= 0 || denominator <= 0) return 0; return static_cast<std::int64_t>(numerator) * ticksPerQuarterNote * 4 / denominator; }
void MidiEngine::setPlaybackPositionSeconds(double seconds) noexcept { playbackPositionSeconds.store(std::max(0.0, seconds), std::memory_order_relaxed); }
double MidiEngine::getPlaybackPositionSeconds() const noexcept { return playbackPositionSeconds.load(std::memory_order_relaxed); }
void MidiEngine::setPlaying(bool shouldPlay) noexcept { playing.store(shouldPlay, std::memory_order_relaxed); }
bool MidiEngine::isPlaying() const noexcept { return playing.load(std::memory_order_relaxed); }
