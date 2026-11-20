#!/usr/bin/env python3
"""Check the integration points for the PERFORMANCE session engine."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
perform=(root/"Source/PerformController.cpp").read_text(encoding="utf-8")
audio=(root/"Source/AudioEngine.cpp").read_text(encoding="utf-8")
project=(root/"Source/Project.cpp").read_text(encoding="utf-8")
timing=(root/"Source/PerformSessionTiming.h").read_text(encoding="utf-8")
assert "prepareAudioClip" in perform and "prepareMidiClip" in perform
assert "parseMidiFile" in perform and "createMidiFromTrack" in perform
assert "LibertyStepSequencer::render(*pattern)" in perform
assert "LibertyPerformTiming::nextBoundary" in perform
assert "LibertyPerformTiming::ticksToSamples" in perform
assert "appendLibertyPerformMidi(this,instrumentTrack,midi,numSamples)" in audio
assert "host.renderInstrumentForTrack(instrumentTrack,bus,numSamples,midi," in audio
assert "nativeDrums->renderMidi(bus,midi)" in audio
assert "saveLibertyPerformSession(this,project,file)" in project
assert "loadLibertyPerformSession(this,*project,file)" in project
assert "clearLibertyPerformSession(this)" in project
assert 'quantizeSelector.addItem("1 MESURE",3)' in perform
assert 'loopButton.setButtonText("BOUCLE : ON")' in perform
assert "instrumentForLogicalTrack" in timing
print("PERFORMANCE Phase 1 source wiring: OK")
