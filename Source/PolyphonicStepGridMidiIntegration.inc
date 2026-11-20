// The Step Sequencer and Piano Roll share the same arrangement-clip owner.
// Transfers are explicit: editing an existing Piano Roll clip never silently
// overwrites a Step Sequencer pattern, and vice versa.
#include "PolyphonicStepGridMidiBridge.h"
#include "PolyphonicStepGridPages.h"
#include "PolyphonicStepGridFrench.h"

bool MainComponent::exportPolyGridToPianoRoll(bool midi, int track, bool openPianoRoll)
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
        ? commitLibertySequencerMidiClip(*this, notes, track, startSeconds, lengthSeconds, openPianoRoll)
        : commitLibertySequencerInstrumentClip(*this, notes, track, startSeconds, lengthSeconds, openPianoRoll);
    if (created)
    {
        // The clip is already inserted into the real ARRANGE timeline.
        // Only VERS PIANO opens the Piano Roll; INSÉRER ARR. keeps the
        // Step Sequencer visible so the user can continue composing.
        if (openPianoRoll)
            openLibertyMidiEditorInDock(*this);
        repaint();
        return true;
    }
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
        "Liberty — Step Sequencer",
        LibertyPolyGridFrench::clipError(),
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


// Stop restores the original instrument bank if a MIDI-track audition
// temporarily fed it a pattern. Neither the song playhead nor arrangement
// clips are changed by audition.
void MainComponent::stopPolyGridPreview()
{
    const int target = polyPreviewTargetTrack;
    const bool wasMidi = polyPreviewMidiSource;
    audioEngine.setStepPreview(-1, false);
    polyPreviewLogicalTrack = -1;
    polyPreviewTargetTrack = -1;
    polyPreviewMidiSource = false;
    if (wasMidi && target >= 0 && target < getInstrumentTrackCount())
        publishInstrumentStepSequencer(target);
    repaint();
}

bool MainComponent::togglePolyGridPreview(bool midi, int track)
{
    const int sourceLogicalTrack = getAudioTrackCount()
        + (midi ? track : getMidiTrackCount() + track);

    // The button is an honest on/off control, not a decorative icon.
    if (audioEngine.isStepPreviewPlaying()
        && polyPreviewLogicalTrack == sourceLogicalTrack)
    {
        stopPolyGridPreview();
        return true;
    }
    if (audioEngine.isStepPreviewPlaying() || polyPreviewLogicalTrack >= 0)
        stopPolyGridPreview();

    const auto* pattern = midi ? getMidiStepSequencer(track)
                               : getInstrumentStepSequencer(track);
    if (pattern == nullptr || !pattern->polyGrid.hasNotes())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, "Liberty - Step Sequencer",
            LibertyPolyGridFrench::emptyPattern(), "OK");
        return false;
    }

    const auto notes = LibertyStepSequencer::render(*pattern);
    const auto ticks = LibertyStepSequencer::getCycleLengthTicks(*pattern);
    const double duration = MidiEngine::tickToSeconds(ticks, tempoBpm);
    if (notes.empty() || ticks <= 0 || !std::isfinite(duration) || duration <= 0.0)
        return false;

    // MIDI tracks produce events, not audio. Route their audition to a loaded
    // instrument on an instrument track, preserving MIDI channel and velocity.
    int target = midi ? juce::jlimit(0, juce::jmax(0, getInstrumentTrackCount()-1),
                                     stepSequencerMidiTarget) : track;
    const auto playable = [this](int lane)
    {
        return lane >= 0 && lane < getInstrumentTrackCount()
            && (LibertyPluginHost::instance().hasInstrumentForTrack(lane)
                || audioEngine.hasNativeDrumSamplerForTrack(lane));
    };
    if (midi && !playable(target))
        for (int lane = 0; lane < getInstrumentTrackCount(); ++lane)
            if (playable(lane)) { target = lane; break; }
    if (!playable(target))
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Liberty - Step Sequencer",
            LibertyPolyGridFrench::noInstrument(), "OK");
        return false;
    }
    if (midi) stepSequencerMidiTarget = target;

    // The audition engine uses its own sample counter. It never moves the
    // ARRANGE playhead and loops one cycle independently from song playback.
    if (audioEngine.isPlaying())
    {
        audioEngine.setPlaying(false);
        isPlaying = false;
    }
    if (midi)
        audioEngine.setInstrumentTrackNotes(target, notes, 0.0, duration,
                                           tempoBpm, true);
    else
        publishInstrumentStepSequencer(track);
    polyPreviewLogicalTrack = sourceLogicalTrack;
    polyPreviewTargetTrack = target;
    polyPreviewMidiSource = midi;
    audioEngine.setStepPreview(target, true);
    repaint();
    return true;
}

bool MainComponent::advancePolyGridPage(bool midi, int track)
{
    auto* pattern = midi ? getMidiStepSequencer(track)
                         : getInstrumentStepSequencer(track);
    if (!pattern) return false;
    const int bank = midi ? midiStepSequencers[(size_t)track].activePattern
                          : instrumentStepSequencers[(size_t)track].activePattern;
    auto& editor = polyGridEditor(midi, track, bank);
    const bool growing = LibertyPolyGridPages::willExtend(pattern->polyGrid,
                                                          stepSequencerPage);
    if (growing) editor.begin(pattern->polyGrid);
    const bool changed = LibertyPolyGridPages::advance(pattern->polyGrid,
                                                       stepSequencerPage);
    if (changed)
    {
        editor.commit(pattern->polyGrid);
        publishPolyGrid(midi, track, *pattern);
        if (audioEngine.isStepPreviewPlaying() && polyPreviewTargetTrack >= 0)
            audioEngine.setStepPreview(polyPreviewTargetTrack, true);
    }
    else repaint();
    return true;
}
