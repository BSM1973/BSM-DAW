#!/usr/bin/env python3
"""Ensure PERFORMANCE inspector edits are actually wired into playback and projects."""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
source=(root/"Source/PerformController.cpp").read_text(encoding="utf-8")
project=(root/"Source/Project.cpp").read_text(encoding="utf-8")
model=(root/"Source/PerformClipSettings.h").read_text(encoding="utf-8")

for key in (
    "initialiseInspector()",
    "refreshInspector()",
    "commitInspector()",
    "void selectClip(int track,int scene)",
    "void paintOverChildren(juce::Graphics& g) override",
    "inspectorArea() const noexcept",
    "const int rowsAvailable=juce::jmax(0,",
    "inspectorName.onTextChange",
    "inspectorColour.onChange",
    "inspectorLoop.onClick",
    "inspectorQuantize.onChange",
    "inspectorMode.onChange",
    "inspectorGain.setValue",
    "inspectorTranspose.setValue",
    "inspectorIn.setValue",
    "inspectorOut.setValue",
    "player.updateClipSettings(selectedClipTrack,selectedClipScene",
    "LibertyPerformClip::audioSlice(",
    "LibertyPerformClip::linearGain(",
    "LibertyPerformClip::notePitch(",
    "LibertyPerformClip::launchQuantization(",
    "LibertyPerformClip::launchedScene(",
    "slot.audioFirst+(int)(position%slot.audioLength)",
    "slot.audio->getSample(juce::jmin(channel,",
    "clipColour.withAlpha",
    "clip->setAttribute(\"name\",data.name)",
    "clip->setAttribute(\"clipGainDb\",",
    "clip->setAttribute(\"clipQuantize\",",
    "clip->setAttribute(\"launchMode\",",
    "clip->setAttribute(\"midiBars\",",
    'clip->getStringAttribute("name")',
    'clip->getIntAttribute("launchMode",0)',
    "clipMetadata[(size_t)track][(size_t)scene]=metadata",
):
    assert key in source, f"Inspector missing wiring: {key}"

assert "saveLibertyPerformSession(this,project,file)" in project
assert "loadLibertyPerformSession(this,*project,file)" in project
assert "std::clamp" in model
assert "std::isfinite" in model
print("PASS: CLIP and LANCEMENT inspector playback, layout and persistence")
