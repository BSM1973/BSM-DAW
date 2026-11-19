#!/usr/bin/env python3
"""Phase 4: transport MIDI, shared arrangement clips and project persistence."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
audio=(root/"Source/AudioEngine.cpp").read_text(encoding="utf-8")
audio_h=(root/"Source/AudioEngine.h").read_text(encoding="utf-8")
grid=(root/"Source/PolyphonicStepGridMidiIntegration.inc").read_text(encoding="utf-8")
midi=(root/"Source/MultiMidiClipController.cpp").read_text(encoding="utf-8")
step=(root/"Source/StepSequencer.cpp").read_text(encoding="utf-8")
ui=(root/"Source/PolyphonicStepGridUI.inc").read_text(encoding="utf-8")
gestures=(root/"Source/PolyphonicStepGridGestures.inc").read_text(encoding="utf-8")
layout=(root/"Source/PolyphonicStepGridLayout.h").read_text(encoding="utf-8")
transport=(root/"Source/TransportSettings.cpp").read_text(encoding="utf-8")
project=(root/"Source/Project.cpp").read_text(encoding="utf-8")
main=(root/"Source/MainComponent.cpp").read_text(encoding="utf-8")
header=(root/"Source/MainComponent.h").read_text(encoding="utf-8")
assert 'LibertyStepTransport::eachLoopOccurrence' in audio
assert 'repeatStepPattern.load' in audio and 'repeatStepPattern.store' in audio
assert 'std::atomic<bool> repeatStepPattern' in audio_h
assert 'publishInstrumentStepSequencer(lane)' in transport
assert 'pattern->polyGrid.active' in header
for marker in ('commitLibertySequencerInstrumentClip(', 'copyLibertyActiveMidiClipNotes(',
               'openLibertyMidiEditorInDock(*this)', 'LibertyPolyGridMidi::importPianoRoll('):
    assert marker in grid,marker
for marker in ('commitGeneratedInstrumentNotesToLane(', 'copyActiveClipNotes(',
               'bool commitLibertySequencerInstrumentClip(', 'bool copyLibertyActiveMidiClipNotes(',
               'legacy.notes', 'owner.midiEngine.replaceNotes(a.notes)'):
    assert marker in midi,marker
for marker in ('pianoExport', 'pianoImport'):
    assert marker in layout and marker in ui and marker in gestures, marker
for marker in ('"PolyGrid"', 'savePolyphonicGrid(*pe, p.polyGrid)',
               'loadPolyphonicGrid(*pe, p.polyGrid)'):
    assert marker in step,marker
assert 'saveStepSequencers(project);' in project
assert 'loadStepSequencers(*project);' in project
assert '#include "PolyphonicStepGridMidiIntegration.inc"' in main
print("Step Sequencer Phase 4: transport/clip/Piano Roll/project wiring OK")
