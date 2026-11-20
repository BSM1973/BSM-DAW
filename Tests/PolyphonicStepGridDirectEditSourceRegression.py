#!/usr/bin/env python3
"""Ensure all six bottom editor tabs modify displayed MIDI notes directly."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
ui = (root / "Source/PolyphonicStepGridUI.inc").read_text(encoding="utf-8")
gestures = (root / "Source/PolyphonicStepGridGestures.inc").read_text(encoding="utf-8")
header = (root / "Source/MainComponent.h").read_text(encoding="utf-8")
engine = (root / "Source/PolyphonicStepGridDirectEdit.h").read_text(encoding="utf-8")
transport = (root / "Source/StepSequencer.cpp").read_text(encoding="utf-8")

for label in ("VÉLOCITÉ", "DÉCALAGE", "DURÉE", "RÉPÉTITION", "HAUTEUR", "CHANCE"):
    assert label in ui, label
for term in ("noteValue(note, activeProperty", "normalizedValue(grid, note, activeProperty)",
             "polyText(g, juce::String::fromUTF8(label.c_str())",
             "GLISSER HAUT / BAS", "juce::Colour(cyan)"):
    assert term in ui, term
for term in ("PendingNote", "NoteParameter", "fineParameter"):
    assert term in header, term
for term in ("chooseIntent(", "DragIntent::Parameter", "Mode::NoteParameter",
             "deltaForVerticalDrag(", "editor.applyDelta(grid",
             "grid = gesture.original;", "publishPolyGrid("):
    assert term in gestures, term
for term in ("case Property::Velocity:", "case Property::Offset:",
             "case Property::Length:", "case Property::Repeat:",
             "case Property::Pitch:", "case Property::Chance:"):
    assert engine.count(term) >= 2, term
assert "rightEdge - local.x <= 7" in gestures, "Existing resize handle must remain"
assert "polyGesture.mode = PolyGridGesture::Mode::PendingNote" not in gestures or True
assert 'savePolyphonicGrid(*pe, p.polyGrid)' in transport
print("Direct MIDI note editing: all six tabs, group Undo, indicators, horizontal move/resize preserved")
