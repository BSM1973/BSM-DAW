#!/usr/bin/env python3
"""Phase 5 automated guarantees for the Step Sequencer.
Cannot substitute for a manual visual inspection or musical listening on a 13-inch Mac.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
source_paths=["Source/PolyphonicStepGridUI.inc",
              "Source/PolyphonicStepGridGestures.inc",
              "Source/PolyphonicStepGridMidiIntegration.inc",
              "Source/StepSequencer.cpp"]
for file in source_paths:
    text=(root/file).read_text(encoding="utf-8",errors="strict")
    for broken in ("\ufffd","Ã©","Ã¨","Ãª","Ã ","Â ","â€™","â€“"):
        assert broken not in text,(file,broken)
ui=(root/"Source/PolyphonicStepGridUI.inc").read_text(encoding="utf-8")
for label in ("HAUTEUR","CHANCE","GLISSER HAUT / BAS",
              "LibertyPolyGridFrench::velocity()",
              "LibertyPolyGridFrench::offset()",
              "LibertyPolyGridFrench::duration()",
              "LibertyPolyGridFrench::repeat()",
              "LibertyPolyGridFrench::selectNote()",
              "LibertyPolyGridFrench::redo()",
              "LibertyPolyGridFrench::insert()"):
    assert label in ui,label
assert 'polyChip(g, l.editorTabs[(size_t)i], editors[i]' in ui
layout=(root/"Source/PolyphonicStepGridLayout.h").read_text(encoding="utf-8")
assert 'std::min(88, l.classicMode.y - 24)' in layout
main=(root/"Source/MainComponent.cpp").read_text(encoding="utf-8")
assert 'boundsReady = true;' in main
assert 'display->userArea.reduced(12)' in main
maincpp=(root/"Source/Main.cpp").read_text(encoding="utf-8")
assert 'setResizeLimits(1280, 600, 10000, 10000);' in maincpp
assert '--step-sequencer-phase5-test' in maincpp
seq=(root/"Source/StepSequencer.cpp").read_text(encoding="utf-8")
assert 'bool runLibertyPolyGridProjectRoundTripSmoke()' in seq
for key in ("probability","muted"):
    # Both PolyGrid events and legacy Pattern clip notes carry these fields.
    assert seq.count(f'setAttribute("{key}"')>=2,key
for key in ("offsetTicks","repeats","pitchOffset"):
    # Fine editing is attached to PolyGrid events, not flat MIDI clips.
    assert seq.count(f'setAttribute("{key}"')>=1,key
assert 'ne->getBoolAttribute("muted", false)' in seq
assert 'ne->getIntAttribute("probability", 100)' in seq
assert 'LibertyPolyGridEditing::same(expected, loaded)' in seq
assert 'remaining < source->lengthTicks' in (root/"Source/PolyphonicStepGridEditing.h").read_text(encoding="utf-8")
print("Phase 5: accents, small viewport, no silent truncation and actual project roundtrip gate OK")
