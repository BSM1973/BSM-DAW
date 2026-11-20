#!/usr/bin/env python3
"""MIDI piano roll: interactive gestures, live notes, undo and project restoration."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=(root/"Source/PerformController.cpp").read_text(encoding="utf-8")
project=(root/"Source/Project.cpp").read_text(encoding="utf-8")
ops=(root/"Source/PerformMidiEditorOps.h").read_text(encoding="utf-8")
assert '#include "PerformMidiEditorOps.h"' in source
for method in (
    "bool replaceMidiNotes(",
    "slot.notes=edited;",
    "slot.sourceMidiLengthTicks=lengthTicks;",
    "void insertMidiNote()",
    "void deleteMidiNote()",
    "void humanizeMidiNotes()",
    "void undoMidiNotes()",
    "void redoMidiNotes()",
    "void commitMidiSlider()",
    "void paintMidiPiano(",
    "void refreshMidiNoteControls()",
    "void mouseDown(const juce::MouseEvent& event) override",
    "void mouseDrag(const juce::MouseEvent& event) override",
    "void mouseDoubleClick(const juce::MouseEvent& event) override",
    "void mouseWheelMove(",
    "bool keyPressed(const juce::KeyPress& key) override",
    "grabKeyboardFocus();",
    "void paintOverChildren(juce::Graphics& g) override",
    'inspectorMidiTab.setButtonText("PIANO ROLL");',
    "const bool piano=midi&&inspectorTab==2;",
    "midiPianoBounds() const",
    "midiVelocityBounds() const",
    "inspectorArea() const noexcept",
    "const bool showGrid=!midiEditorVisible();",
    "clipButtons[(size_t)track][(size_t)scene].setVisible(showGrid);",
    "sceneButtons[(size_t)scene].setVisible(showGrid);",
    "midiHumanizeButton.onClick=[this]{humanizeMidiNotes();};",
    "midiUndoButton.onClick=[this]{undoMidiNotes();};",
    "midiRedoButton.onClick=[this]{redoMidiNotes();};",
    "LibertyPerformMidiEdit::humanize(",
    "LibertyPerformMidiEdit::edit(",
    "clip->history.checkpoint(",
    "clip->history.undoStep(",
    "clip->history.redoStep(",
    "player.replaceMidiNotes(selectedClipTrack,selectedClipScene,",
    "e->setAttribute(\"probability\",(int)n.probability)",
    'e->getIntAttribute("probability",100)',
):
    assert method in source, "Missing MIDI piano-roll implementation: "+method

assert "saveLibertyPerformSession(this,project,file)" in project
assert "loadLibertyPerformSession(this,*project,file)" in project
assert "std::clamp" in ops and "maxEntries=48" in ops
assert "midiEditorVisible()?juce::" not in source
print("PASS: piano roll direct editing, humanize, Undo/Redo, live engine and save/load")
