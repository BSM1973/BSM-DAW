#!/usr/bin/env python3
"""Read-only CI contract for 8-lane Step Sequencer view and hit paths."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
ui = (root / "Source/PolyphonicStepGridUI.inc").read_text(encoding="utf-8")
main = (root / "Source/MainComponent.cpp").read_text(encoding="utf-8")
header = (root / "Source/MainComponent.h").read_text(encoding="utf-8")
for text in ('"CLIP MIDI"', '"CRÉER CLIP"', '"VÉLOCITÉ"', '"DÉCALAGE"',
             '"DURÉE"', '"RÉPÉTITION"', '"HAUTEUR"', '"CHANCE"',
             '"PAS", "RATE", "DIR", "SWING"', 'constexpr juce::uint32 orange = 0xffed963c',
             'juce::Colour(cyan)', 'polyChip(g, l.muteButtons',
             'polyChip(g, l.soloButtons', 'clickPolyphonicStepSequencer'):
    assert text in ui, text
assert 'LibertyPolyGridUI::Layout::make(' in ui
assert 'polyShowNewGrid(*pattern)' in main
assert '#include "PolyphonicStepGridUI.inc"' in main
assert 'drawPolyphonicStepSequencer(g, area, *pattern' in main
assert 'clickPolyphonicStepSequencer(point, area, *pattern' in main
assert 'clickPolyphonicStepSequencer(p, area, *pattern' in main
assert 'polyGridSelectedEvent' in header
# Phase 4 routes new clips into the shared Piano Roll arrangement instead
# of creating a second, uneditable instrument Pattern clip.
assert 'exportPolyGridToPianoRoll(isMidi, trackIndex)' in ui
assert 'importPianoRollToPolyGrid(isMidi, trackIndex)' in ui
integration = (root / "Source/PolyphonicStepGridMidiIntegration.inc").read_text(encoding="utf-8")
assert 'commitLibertySequencerInstrumentClip(*this, notes, track' in integration
assert 'commitLibertySequencerMidiClip(*this, notes, track' in integration
print("Liberty Step Sequencer: 8-line interactive UI / multi-bank wiring OK")
