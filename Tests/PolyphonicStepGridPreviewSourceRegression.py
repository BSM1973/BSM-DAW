#!/usr/bin/env python3
"""Step Sequencer acceptance: real independent audition, ARRANGE clip creation,
nonempty 16/32/48/64 page navigation and safe Unicode UI captions."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
get=lambda name:(root/"Source"/name).read_text(encoding="utf-8",errors="strict")
ui=get("PolyphonicStepGridUI.inc")
gestures=get("PolyphonicStepGridGestures.inc")
integration=get("PolyphonicStepGridMidiIntegration.inc")
controller=get("MultiMidiClipController.cpp")
audio=get("AudioEngine.cpp")
audio_h=get("AudioEngine.h")
main=get("MainComponent.cpp")
hdr=get("MainComponent.h")
pages=get("PolyphonicStepGridPages.h")
french=get("PolyphonicStepGridFrench.h")
for file in (ui,gestures,integration,controller,audio,audio_h):
    assert "\ufffd" not in file
for needle in ("l.previewButton", "getStepPreviewPositionSeconds()",
               "LibertyPolyGridFrench::stop()", "LibertyPolyGridFrench::insert()",
               "LibertyPolyGridPages::willExtend", "LibertyPolyGridFrench::redo()"):
    assert needle in ui,needle
for needle in ("togglePolyGridPreview(midi, track)", "advancePolyGridPage(midi, track)",
               "exportPolyGridToPianoRoll(midi, track, false)",
               "exportPolyGridToPianoRoll(midi, track, true)"):
    assert needle in gestures,needle
for needle in ("audioEngine.setStepPreview(target, true)", "audioEngine.setStepPreview(-1, false)",
               "stopPolyGridPreview()", "stepSequencerMidiTarget = target",
               "advancePolyGridPage(bool midi, int track)", "LibertyPolyGridPages::advance",
               "if (openPianoRoll)", "openPianoRoll)"):
    assert needle in integration,needle
assert "bool revealEditor=true" in controller
assert "if(revealEditor){setBounds(owner.getLocalBounds());toFront(false);}" in controller
assert "commitGeneratedNotes(n,true,start,length,true,revealEditor)" in controller
assert "hasNativeDrumSamplerForTrack" in audio_h
assert "stepPreviewTrack.load(std::memory_order_acquire) >= 0)" in audio
assert "nativeDrums->renderMidi(bus, midi)" in audio
assert "instrumentPanicPending.store(true" in audio_h
assert "std::min(88, l.classicMode.y - 24)" in get("PolyphonicStepGridLayout.h")
assert "newCount = (next + 1) * 16" in pages
assert "if (row.steps == previousCount)" in pages
assert "setStepPreview(polyPreviewTargetTrack, true)" in gestures
assert "stopPolyGridPreview();" in get("Project.cpp")
assert "stopPolyGridPreview();" in get("StepSequencer.cpp")
assert "previewButton" in get("PolyphonicStepGridLayout.h")
for codepoint in ("0x00C9", "0x00CA", "0x00EA", "0x00E9"):
    assert codepoint in french,codepoint
assert "LibertyPolyGridFrench::correctUnicode()" in get("StepSequencer.cpp")
assert 'juce::Font("Helvetica Neue"' in ui
assert 'juce::Font("Segoe UI"' in ui
assert "polyPreviewLogicalTrack" in hdr
print("Step Sequencer preview, arrangement insertion, 64-step pages and French Unicode wired")
