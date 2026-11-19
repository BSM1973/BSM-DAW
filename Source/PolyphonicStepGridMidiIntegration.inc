// The Step Sequencer and Piano Roll share the same arrangement-clip owner.
// Transfers are explicit: editing an existing Piano Roll clip never silently
// overwrites a Step Sequencer pattern, and vice versa.
#include "PolyphonicStepGridMidiBridge.h"

bool MainComponent::exportPolyGridToPianoRoll(bool midi, int track)
{
    auto* pattern = midi ? getMidiStepSequencer(track)
                         : getInstrumentStepSequencer(track);
    if (!pattern || !pattern->polyGrid.active) return false;
    const auto cycle = LibertyStepSequencer::getCycleLengthTicks(*pattern);
    const auto notes = LibertyStepSequencer::render(*pattern);
    if (cycle <= 0 || notes.empty() || notes.size() > 8192) return false;
    // Place clips on the same MIDI-tick grid used by the existing arrangement.
    constexpr auto snapTicks = MidiEngine::ticksPerQuarterNote / 4;
    const auto playTick = juce::jmax<std::int64_t>(0,
        MidiEngine::secondsToTick(juce::jmax(0.0, playheadSeconds), tempoBpm));
    const auto snappedTick = playTick / snapTicks * snapTicks;
    const auto startSeconds = MidiEngine::tickToSeconds(snappedTick, tempoBpm);
    const auto lengthSeconds = MidiEngine::tickToSeconds(cycle, tempoBpm);
    if (!(lengthSeconds > 0.0) || !std::isfinite(lengthSeconds)) return false;
    const bool created = midi
        ? commitLibertySequencerMidiClip(*this, notes, track, startSeconds, lengthSeconds)
        : commitLibertySequencerInstrumentClip(*this, notes, track, startSeconds, lengthSeconds);
    if (created)
    {
        // commitGeneratedNotes loads the new authoritative clip into midiEngine.
        // Opening the existing Piano Roll never creates a second copy.
        openLibertyMidiEditorInDock(*this);
        repaint();
        return true;
    }
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
        "Liberty — Step Sequencer",
        juce::String::fromUTF8("Impossible de créer le clip MIDI ici. Vérifiez qu'aucun autre clip n'occupe cette position."),
        "OK");
    return false;
}

bool MainComponent::importPianoRollToPolyGrid(bool midi, int track)
{
    auto* pattern = midi ? getMidiStepSequencer(track)
                         : getInstrumentStepSequencer(track);
    if (!pattern) return false;
    std::vector<MidiEngine::NoteEvent> source;
    std::int64_t clipTicks = 0;
    if (!copyLibertyActiveMidiClipNotes(*this, !midi, track, source, clipTicks))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
            "Liberty — Step Sequencer",
            juce::String::fromUTF8("Ouvrez d'abord un clip de cette piste dans le Piano Roll, puis réessayez l'import."),
            "OK");
        return false;
    }
    // Keep the current note resolution: no automatic quantization, dropped
    // pitches or shortening of notes without the user's consent.
    const auto stepTicks = pattern->polyGrid.stepTicks;
    if (stepTicks < 1 || clipTicks < 1 || clipTicks % stepTicks != 0 ||
        clipTicks / stepTicks > LibertyPolyphonicGrid::maxSteps)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Liberty — Import MIDI",
            juce::String::fromUTF8("La durée du clip ne correspond pas à une grille de 1 à 64 pas. Le pattern reste inchangé."),
            "OK");
        return false;
    }
    LibertyPolyphonicGrid restored;
    if (!LibertyPolyGridMidi::importPianoRoll(source, restored,
                                            (int)(clipTicks / stepTicks), stepTicks))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Liberty — Import MIDI",
            juce::String::fromUTF8("Ce clip comporte plus de huit hauteurs, des positions non alignées ou des notes superposées impossibles à représenter dans la grille. Aucun événement n'a été perdu."),
            "OK");
        return false;
    }
    const int bank = midi ? midiStepSequencers[(size_t)track].activePattern
                          : instrumentStepSequencers[(size_t)track].activePattern;
    auto& editor = polyGridEditor(midi, track, bank);
    editor.begin(pattern->polyGrid);
    pattern->polyGrid = std::move(restored);
    pattern->enabled = true;
    editor.clearSelection();
    editor.commit(pattern->polyGrid);
    polyGridSelectedLane = -1;
    polyGridSelectedEvent = 0;
    stepSequencerPage = 0;
    publishPolyGrid(midi, track, *pattern);
    return true;
}
