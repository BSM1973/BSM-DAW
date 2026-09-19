#include "MidiEngine.h"
#include <algorithm>
#include <cmath>

MidiEngine::HistoryState MidiEngine::makeHistoryState() const
{
    return { notes, selectedNotes };
}

void MidiEngine::pushUndoState()
{
    undoHistory.push_back(makeHistoryState());
    redoHistory.clear();
    constexpr std::size_t maxHistory = 100;
    if (undoHistory.size() > maxHistory)
        undoHistory.erase(undoHistory.begin());
}

void MidiEngine::restoreHistoryState(const HistoryState& state)
{
    notes = state.notes;
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
    redoHistory.push_back(makeHistoryState());
    auto state = std::move(undoHistory.back());
    undoHistory.pop_back();
    restoreHistoryState(state);
    return true;
}

bool MidiEngine::redo()
{
    if (redoHistory.empty()) return false;
    undoHistory.push_back(makeHistoryState());
    auto state = std::move(redoHistory.back());
    redoHistory.pop_back();
    restoreHistoryState(state);
    return true;
}

void MidiEngine::clearUndoHistory() noexcept
{
    undoHistory.clear();
    redoHistory.clear();
}

void MidiEngine::clear()
{
    if (notes.empty()) return;
    pushUndoState();
    notes.clear();
    clearNoteSelection();
    setPlaybackPositionSeconds(0.0);
    setPlaying(false);
}

bool MidiEngine::addNote(std::int64_t startTick, std::int64_t lengthTicks, int pitch, int velocity, int channel)
{
    if (startTick < 0 || lengthTicks <= 0 || pitch < minMidiNote || pitch > maxMidiNote || velocity < 1 || velocity > 127 || channel < 1 || channel > 16) return false;
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
    if (deltaTicks % (ticksPerQuarterNote / 4) != 0) return false;
    std::vector<NoteEvent> source = selectedNotes;
    for (const auto& n : source)
    {
        const auto newStart = n.startTick + deltaTicks;
        const int newPitch = static_cast<int>(n.pitch) + deltaPitch;
        if (newStart < 0 || newPitch < minMidiNote || newPitch > maxMidiNote) return false;
        const bool collision = std::any_of(notes.begin(), notes.end(), [&n, newStart, newPitch, this](const NoteEvent& other)
        { return !isNoteSelected(other) && other.startTick == newStart && other.pitch == static_cast<std::uint8_t>(newPitch) && other.channel == n.channel; });
        if (collision) return false;
    }
    pushUndoState();
    for (const auto& n : source)
    {
        const auto it = std::find_if(notes.begin(), notes.end(), [&n](const NoteEvent& other)
        { return other.startTick == n.startTick && other.pitch == n.pitch && other.channel == n.channel; });
        if (it != notes.end()) { it->startTick += deltaTicks; it->pitch = static_cast<std::uint8_t>(static_cast<int>(it->pitch) + deltaPitch); }
    }
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    { if (a.startTick != b.startTick) return a.startTick < b.startTick; if (a.channel != b.channel) return a.channel < b.channel; return a.pitch < b.pitch; });
    auto result = source;
    for (auto& n : result) { n.startTick += deltaTicks; n.pitch = static_cast<std::uint8_t>(static_cast<int>(n.pitch) + deltaPitch); }
    setSelectedNotes(result);
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
            const auto newStart = n.startTick + deltaTicks;
            const auto newLength = n.lengthTicks - deltaTicks;
            if (newStart < 0 || newLength < minimumLength)
                return false;
        }
        else
        {
            if (n.lengthTicks + deltaTicks < minimumLength)
                return false;
        }
    }

    pushUndoState();
    std::vector<NoteEvent> result;
    result.reserve(source.size());

    for (const auto& n : source)
    {
        const auto it = std::find_if(notes.begin(), notes.end(), [&n](const NoteEvent& other)
        {
            return other.startTick == n.startTick && other.pitch == n.pitch && other.channel == n.channel;
        });
        if (it == notes.end())
            continue;

        if (fromLeftEdge)
        {
            it->startTick += deltaTicks;
            it->lengthTicks -= deltaTicks;
        }
        else
        {
            it->lengthTicks += deltaTicks;
        }
        result.push_back(*it);
    }

    if (result.size() != source.size())
        return false;

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
    std::vector<NoteEvent> copies;
    for (const auto& n : source)
    {
        const auto newStart = n.startTick + deltaTicks;
        if (newStart < 0) return false;
        if (std::any_of(notes.begin(), notes.end(), [&n, newStart](const NoteEvent& other)
            { return other.startTick == newStart && other.pitch == n.pitch && other.channel == n.channel; })) return false;
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
    if (startTick < 0 || pitch < minMidiNote || pitch > maxMidiNote || channel < 1 || channel > 16 || newLengthTicks <= 0) return false;
    const auto it = std::find_if(notes.begin(), notes.end(), [=](const NoteEvent& note)
    { return note.startTick == startTick && note.pitch == static_cast<std::uint8_t>(pitch) && note.channel == static_cast<std::uint8_t>(channel); });
    if (it == notes.end() || it->lengthTicks == newLengthTicks) return false;
    pushUndoState();
    it->lengthTicks = newLengthTicks; selectNoteAt(startTick, pitch, channel); selectedNote.lengthTicks = newLengthTicks; return true;
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
std::int64_t MidiEngine::getLengthTicks() const noexcept { std::int64_t length = 0; for (const auto& note : notes) length = std::max(length, note.startTick + note.lengthTicks); return length; }
double MidiEngine::tickToSeconds(std::int64_t tick, double tempoBpm) noexcept { if (tick <= 0 || tempoBpm <= 0.0) return 0.0; return (static_cast<double>(tick) / static_cast<double>(ticksPerQuarterNote)) * (60.0 / tempoBpm); }
std::int64_t MidiEngine::secondsToTick(double seconds, double tempoBpm) noexcept { if (seconds <= 0.0 || tempoBpm <= 0.0) return 0; return static_cast<std::int64_t>(std::llround(seconds * tempoBpm / 60.0 * static_cast<double>(ticksPerQuarterNote))); }
std::int64_t MidiEngine::quantizeTick(std::int64_t tick, std::int64_t gridTicks) noexcept { if (tick <= 0 || gridTicks <= 0) return std::max<std::int64_t>(0, tick); return static_cast<std::int64_t>(std::llround(static_cast<double>(tick) / static_cast<double>(gridTicks))) * gridTicks; }
std::int64_t MidiEngine::ticksPerMeasure(int numerator, int denominator) noexcept { if (numerator <= 0 || denominator <= 0) return 0; return static_cast<std::int64_t>(numerator) * ticksPerQuarterNote * 4 / denominator; }
void MidiEngine::setPlaybackPositionSeconds(double seconds) noexcept { playbackPositionSeconds.store(std::max(0.0, seconds), std::memory_order_relaxed); }
double MidiEngine::getPlaybackPositionSeconds() const noexcept { return playbackPositionSeconds.load(std::memory_order_relaxed); }
void MidiEngine::setPlaying(bool shouldPlay) noexcept { playing.store(shouldPlay, std::memory_order_relaxed); }
bool MidiEngine::isPlaying() const noexcept { return playing.load(std::memory_order_relaxed); }
