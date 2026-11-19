#!/usr/bin/env python3
"""JUCE-free source checks for the versioned polyphonic Step Sequencer integration."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
step = (ROOT / "Source/StepSequencer.cpp").read_text(encoding="utf-8")
model = (ROOT / "Source/StepSequencer.h").read_text(encoding="utf-8")
main = (ROOT / "Source/MainComponent.h").read_text(encoding="utf-8")
assert "LibertyPolyphonicGrid polyGrid" in model
assert 'createNewChildElement("PolyGrid")' in step
assert 'root->getIntAttribute("version", -1) != 1' in step
assert step.count("savePolyphonicGrid(*pe, p.polyGrid)") == 2
assert step.count("loadPolyphonicGrid(*pe, p.polyGrid)") == 2
assert "pattern.polyGrid.render(startTick)" in step
assert "if (pattern.polyGrid.active) return pattern.polyGrid.cycleTicks();" in step
assert "pattern->polyGrid.active" in main
assert 'root.getChildByName("StepSequencers")' in step
assert 'root.getChildByName("MidiStepSequencers")' in step
print("Polyphonic Step Grid: legacy/dual-bank XML and render integration OK")
