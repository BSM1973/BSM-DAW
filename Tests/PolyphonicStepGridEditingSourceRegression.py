#!/usr/bin/env python3
"""Phase 3: all six edit modes must be wired to MIDI and project persistence."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
model=(root/"Source/PolyphonicStepGrid.h").read_text(encoding="utf-8")
history=(root/"Source/PolyphonicStepGridEditing.h").read_text(encoding="utf-8")
gesture=(root/"Source/PolyphonicStepGridGestures.inc").read_text(encoding="utf-8")
source=(root/"Source/MainComponent.cpp").read_text(encoding="utf-8")
editing=(root/"Source/Editing.cpp").read_text(encoding="utf-8")
xml=(root/"Source/StepSequencer.cpp").read_text(encoding="utf-8")
ui=(root/"Source/PolyphonicStepGridUI.inc").read_text(encoding="utf-8")
for literal in ('offsetTicks','repeats','pitchOffset','note.probability','polyGrid.active'):
    assert literal in model or literal in xml, literal
for literal in ('Velocity, Offset, Length, Repeat, Pitch, Chance',
                'void begin(', 'bool commit(', 'bool undo(', 'bool redo(',
                'moveFrom(', 'resizeFrom(', 'applyDelta(', 'removeSelected('):
    assert literal in history, literal
for literal in ('beginPolyGridPointer(', 'dragPolyGridPointer(', 'endPolyGridPointer(',
                'polyGridKeyAction(', 'beginFloatingPolyGrid(',
                'selectRectangle(', 'juce::KeyPress::deleteKey',
                'editor.commit(grid)', 'editor.undo(grid)', 'editor.redo(grid)'):
    assert literal in gesture, literal
for literal in ('"offsetTicks"','"repeats"','"pitchOffset"'):
    assert xml.count(literal)>=2, literal
assert '#include "PolyphonicStepGridGestures.inc"' in source
assert 'dragPolyGridPointer(' in source and 'endPolyGridPointer(' in source
assert 'polyGridKeyAction(key, false)' in editing
assert 'polyGridKeyAction(key, true)' in source
assert 'editor->contains({row, note.id})' in ui
assert '"ANNULER"' in ui and 'LibertyPolyGridFrench::redo()' in ui
assert 'Édition avancée prévue en phase 3' not in ui
print("Liberty Step Sequencer: six properties + pointer/keyboard history OK")
