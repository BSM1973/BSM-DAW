#include "MidiEngine.h"
#include <algorithm>

bool MidiEngine::quantizeSelectedNotes(std::int64_t gridTicks)
{
    if (selectedNotes.empty() || gridTicks <= 0)
        return false;

    const auto source = selectedNotes;
    std::vector<NoteEvent> result;
    result.reserve(source.size());

    for (const auto& sourceNote : source)
    {
        const auto it = std::find_if(notes.begin(), notes.end(), [&sourceNote](const NoteEvent& note)
        {
            return note.startTick == sourceNote.startTick
                && note.pitch == sourceNote.pitch
                && note.channel == sourceNote.channel;
        });
        if (it == notes.end())
            return false;

        auto quantized = *it;
        quantized.startTick = quantizeTick(sourceNote.startTick, gridTicks);
        result.push_back(quantized);
    }

    // A quantized note may not collide with an unselected note.
    for (const auto& quantized : result)
    {
        const bool collision = std::any_of(notes.begin(), notes.end(), [&quantized, this](const NoteEvent& other)
        {
            return !isNoteSelected(other)
                && other.startTick == quantized.startTick
                && other.pitch == quantized.pitch
                && other.channel == quantized.channel;
        });
        if (collision)
            return false;
    }

    // Selected notes must also remain unique after quantization.
    for (std::size_t i = 0; i < result.size(); ++i)
        for (std::size_t j = i + 1; j < result.size(); ++j)
            if (result[i].startTick == result[j].startTick
                && result[i].pitch == result[j].pitch
                && result[i].channel == result[j].channel)
                return false;

    bool changed = false;
    for (std::size_t i = 0; i < source.size(); ++i)
        changed = changed || (source[i].startTick != result[i].startTick);

    if (!changed)
        return false;

    // One complete quantization action = one undo step.
    pushUndoState();

    for (const auto& sourceNote : source)
    {
        const auto it = std::find_if(notes.begin(), notes.end(), [&sourceNote](const NoteEvent& note)
        {
            return note.startTick == sourceNote.startTick
                && note.pitch == sourceNote.pitch
                && note.channel == sourceNote.channel;
        });
        if (it != notes.end())
        {
            const auto resultIt = std::find_if(result.begin(), result.end(), [&sourceNote](const NoteEvent& note)
            {
                return note.pitch == sourceNote.pitch
                    && note.channel == sourceNote.channel
                    && note.lengthTicks == sourceNote.lengthTicks
                    && note.velocity == sourceNote.velocity;
            });
            if (resultIt != result.end())
                it->startTick = resultIt->startTick;
        }
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
