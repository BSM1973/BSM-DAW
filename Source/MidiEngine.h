#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cstdint>
#include <vector>

class MidiEngine final
{
public:
    static constexpr std::int64_t ticksPerQuarterNote = 960;
    static constexpr int minMidiNote = 0;
    static constexpr int maxMidiNote = 127;

    struct NoteEvent
    {
        std::int64_t startTick = 0;
        std::int64_t lengthTicks = ticksPerQuarterNote;
        std::uint8_t pitch = 60;
        std::uint8_t velocity = 100;
        std::uint8_t channel = 1;
    };

    MidiEngine() = default;
    ~MidiEngine() = default;
    MidiEngine(const MidiEngine&) = delete;
    MidiEngine& operator=(const MidiEngine&) = delete;

    void clear();
    bool addNote(std::int64_t startTick, std::int64_t lengthTicks, int pitch,
                 int velocity = 100, int channel = 1);
    bool removeNoteAt(std::int64_t startTick, int pitch, int channel = 1);
    bool deleteSelectedNote();
    bool selectNoteAt(std::int64_t startTick, int pitch, int channel = 1) noexcept;
    void clearNoteSelection() noexcept;
    bool hasSelectedNote() const noexcept { return selectedNoteValid; }
    NoteEvent getSelectedNote() const noexcept { return selectedNote; }

    bool toggleNoteSelectionAt(std::int64_t startTick, int pitch, int channel = 1) noexcept;
    bool isNoteSelected(const NoteEvent& note) const noexcept;
    std::vector<NoteEvent> getSelectedNotesCopy() const;
    void setSelectedNotes(const std::vector<NoteEvent>& selection) noexcept;
    std::size_t getNumSelectedNotes() const noexcept { return selectedNotes.size(); }
    bool moveSelectedNotesBy(std::int64_t deltaTicks, int deltaPitch);
    bool duplicateSelectedNotes(std::int64_t deltaTicks = ticksPerQuarterNote);
    bool deleteSelectedNotes();

    bool undo();
    bool redo();
    bool canUndo() const noexcept { return !undoHistory.empty(); }
    bool canRedo() const noexcept { return !redoHistory.empty(); }
    void clearUndoHistory() noexcept;

    bool moveNote(std::int64_t oldStartTick, int oldPitch, int channel,
                  std::int64_t newStartTick, int newPitch);
    bool setNoteLength(std::int64_t startTick, int pitch, int channel,
                       std::int64_t newLengthTicks);
    bool setNoteVelocity(std::int64_t startTick, int pitch, int channel,
                         int newVelocity);
    std::size_t getNumNotes() const noexcept { return notes.size(); }
    std::vector<NoteEvent> getNotesCopy() const;
    std::int64_t getLengthTicks() const noexcept;

    static double tickToSeconds(std::int64_t tick, double tempoBpm) noexcept;
    static std::int64_t secondsToTick(double seconds, double tempoBpm) noexcept;
    static std::int64_t quantizeTick(std::int64_t tick, std::int64_t gridTicks) noexcept;
    static std::int64_t ticksPerMeasure(int numerator, int denominator) noexcept;

    void setPlaybackPositionSeconds(double seconds) noexcept;
    double getPlaybackPositionSeconds() const noexcept;
    void setPlaying(bool shouldPlay) noexcept;
    bool isPlaying() const noexcept;

private:
    struct HistoryState
    {
        std::vector<NoteEvent> notes;
        std::vector<NoteEvent> selectedNotes;
    };

    HistoryState makeHistoryState() const;
    void restoreHistoryState(const HistoryState& state);
    void pushUndoState();

    std::vector<NoteEvent> notes;
    NoteEvent selectedNote;
    bool selectedNoteValid = false;
    std::vector<NoteEvent> selectedNotes;
    std::vector<HistoryState> undoHistory;
    std::vector<HistoryState> redoHistory;
    std::atomic<double> playbackPositionSeconds { 0.0 };
    std::atomic<bool> playing { false };
};
