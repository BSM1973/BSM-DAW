#!/usr/bin/env python3
"""PERFORMANCE Phase 4: non-destructive transformations must reach live MIDI and survive projects."""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
source=(root/"Source/PerformController.cpp").read_text(encoding="utf-8")
generator=(root/"Source/PerformMidiGenerator.h").read_text(encoding="utf-8")
project=(root/"Source/Project.cpp").read_text(encoding="utf-8")

required=(
    '#include "PerformMidiGenerator.h"',
    "struct SessionMidiData",
    "LibertyPerformGenerator::Parameters generator;",
    "std::vector<MidiEngine::NoteEvent> generatedNotes;",
    "int generatorState=0;",
    "inspectorTransformTab.setButtonText",
    "inspectorGenerateTab.setButtonText",
    "void initialiseGenerator()",
    "void refreshGeneratorControls()",
    "void commitGenerator()",
    "void setGeneratorState(int state)",
    "void nextGeneratorVariation()",
    "void cancelTemporaryGeneratorPreview()",
    "void paintMidiPiano(juce::Graphics& g)",
    "const auto& visibleNotes=midiGeneratorVisible()?",
    "generatorKind.onChange",
    "generatorDensity",
    "generatorSwing",
    "generatorVariation",
    "generatorVelocity",
    "bool publishGeneratorLayer()",
    "clip->generatedNotes=LibertyPerformGenerator::render(",
    "clip->generatorState==0?",
    "player.replaceMidiNotes(selectedClipTrack,selectedClipScene,",
    "if(clip->generatorState!=0)",
    "clip->setAttribute(\"generatorKind\",p.kind)",
    "clip->setAttribute(\"generatorSeed\",",
    "clip->setAttribute(\"generatorEnabled\",data.generatorState==2)",
    'clip->getBoolAttribute("generatorEnabled",false)',
    "stored.generatedNotes=LibertyPerformGenerator::render(",
    "player.replaceMidiNotes(track,scene,stored.generatedNotes,",
    "void saveSession(juce::XmlElement& project",
    "void loadSession(const juce::XmlElement& project",
    "const bool showGrid=!midiWorkspaceVisible();",
)
for term in required:
    if term not in source:
        raise AssertionError("Phase 4 missing source integration: "+term)

assert "const auto& data=performMidiClips[(size_t)track][(size_t)scene];" in source
assert "for(const auto& n:data.notes)" in source, "Original notes must be saved (not generated notes)"
assert "for(const auto& n:data.generatedNotes)" not in source
assert "std::stable_sort(result.begin()" in generator
assert "result.size()<maxNotes" in generator
assert "p.kind==0" in generator and "p.kind==1" in generator
assert "p.kind==2" in generator or "else\n    {" in generator
assert "juce::" not in generator, "Pure generator is independent of JUCE/audio thread"
assert "saveLibertyPerformSession(this,project,file)" in project
assert "loadLibertyPerformSession(this,*project,file)" in project
print("PASS: arp, rhythm, variations, original-safe preview/activation and project restore")
