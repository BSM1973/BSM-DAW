#include "MidiEngine.h"
#include <algorithm>
#include <cmath>

void MidiEngine::clear()
{
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
    for (const auto& n : source)
    {
        const auto it = std::find_if(notes.begin(), notes.end(), [&n](const NoteEvent& other)
        { return other.startTick == n.startTick && other.pitch == n.pitch && other.channel == n.channel; });
        if (it != notes.end()) { it->startTick += deltaTicks; it->pitch = static_cast<std::uint8_t>(static_cast<int>(it->pitch) + deltaPitch); }
    }
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b)
    { if (a.startTick != b.startTick) return a.startTick < b.startTick; if (a.channel != b.channel) return a.channel < b.channel; return a.pitch < b.pitch; });
    setSelectedNotes([&source, deltaTicks, deltaPitch]() { auto result = source; for (auto& n : result) { n.startTick += deltaTicks; n.pitch = static_cast<std::uint8_t>(static_cast<int>(n.pitch) + deltaPitch); } return result; }());
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
    const auto length = it->lengthTicks; const auto velocity = it->velocity; notes.erase(it); return addNote(newStartTick, length, newPitch, velocity, channel);
}

bool MidiEngine::setNoteLength(std::int64_t startTick, int pitch, int channel, std::int64_t newLengthTicks)
{
    if (startTick < 0 || pitch < minMidiNote || pitch > maxMidiNote || channel < 1 || channel > 16 || newLengthTicks <= 0) return false;
    const auto it = std::find_if(notes.begin(), notes.end(), [=](const NoteEvent& note)
    { return note.startTick == startTick && note.pitch == static_cast<std::uint8_t>(pitch) && note.channel == static_cast<std::uint8_t>(channel); });
    if (it == notes.end()) return false; it->lengthTicks = newLengthTicks; selectNoteAt(startTick, pitch, channel); selectedNote.lengthTicks = newLengthTicks; return true;
}

bool MidiEngine::setNoteVelocity(std::int64_t startTick, int pitch, int channel, int newVelocity)
{
    if (startTick < 0 || pitch < minMidiNote || pitch > maxMidiNote || channel < 1 || channel > 16 || newVelocity < 1 || newVelocity > 127) return false;
    const auto it = std::find_if(notes.begin(), notes.end(), [=](const NoteEvent& note)
    { return note.startTick == startTick && note.pitch == static_cast<std::uint8_t>(pitch) && note.channel == static_cast<std::uint8_t>(channel); });
    if (it == notes.end()) return false; it->velocity = static_cast<std::uint8_t>(newVelocity); selectNoteAt(startTick, pitch, channel); selectedNote.velocity = static_cast<std::uint8_t>(newVelocity); return true;
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
