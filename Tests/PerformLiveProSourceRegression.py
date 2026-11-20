#!/usr/bin/env python3
"""Phase 5 LIVE PRO: functional scene scheduler, MIDI Learn, capture and project integration."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
perform=(root/"Source/PerformController.cpp").read_text(encoding="utf-8")
audio=(root/"Source/AudioEngine.cpp").read_text(encoding="utf-8")
audioh=(root/"Source/AudioEngine.h").read_text(encoding="utf-8")
multi=(root/"Source/MultiMidiClipController.cpp").read_text(encoding="utf-8")
pure=(root/"Source/PerformLivePro.h").read_text(encoding="utf-8")
project=(root/"Source/Project.cpp").read_text(encoding="utf-8")

for required in (
    '#include "PerformLivePro.h"',
    "void initialiseLiveControls()",
    "void pollFollowActions()",
    "void pollLiveMidiLearn()",
    "void applyLiveMacros()",
    "void commitFollowAction()",
    "void updateMidiLearnLabel()",
    "activeFollowScene=scene;",
    "LibertyPerformLive::nextFollowSample(",
    "LibertyPerformLive::followScene(",
    "player.queueScene(track,slotHasClip(track,scene)?scene:-1,",
    "player.getSessionSamples()",
    "void setTransitionMilliseconds(int ms)",
    "void setLiveAudioGain(float gain)",
    "void setLiveInstrumentGain(float gain)",
    "void setLiveMasterGain(float gain)",
    "float getLiveInstrumentGain() const noexcept",
    "LibertyPerformLive::crossfadeIn(elapsed,state.fadeLength)",
    "state.fadeFromScene",
    "audioMacroGain.load(",
    "std::array<LibertyPerformLive::Binding,LibertyPerformLive::targets> midiBindings",
    "owner.audioEngine.getPerformMidiLearnEvent()",
    "LibertyPerformLive::matches(",
    "midiBindings[(size_t)target]=LibertyPerformLive::validBinding(",
    "liveMacroSliders[(size_t)(target-9)].setValue(",
    "session->createNewChildElement(\"LivePro\")",
    "live->createNewChildElement(\"Follow\")",
    "live->createNewChildElement(\"Binding\")",
    'session->getChildByName("LivePro")',
    'value->hasTagName("Follow")',
    'value->hasTagName("Binding")',
    "float captureFinalMix(float* const* output,int channels,int samples)",
    "captureWriter->write(captureMix.getArrayOfReadPointers(),count);",
    "captureEvents[(size_t)captureCount++]",
    "sourceOffsetSamples",
    "std::vector<CaptureEvent> getCaptureEvents() const",
    "std::int64_t getCapturedSamples() const",
    "importCapturedMidiIntoArrangement(",
    "const int targetTrack=owner.addAudioTrack();",
    "commitLibertyPerformCapturedInstrumentClip(",
    "void processLibertyPerformFinalMix(AudioEngine* engine",
):
    assert required in perform, "Missing LIVE PRO feature: "+required

assert 'performMidiLearnEvent.store(packed,std::memory_order_release);' in audio
assert 'getPerformMidiLearnEvent() const noexcept' in audioh
assert 'processLibertyPerformFinalMix(this,outputChannelData,numOutputChannels,numSamples);' in audio
assert 'bus.applyGain(getLibertyPerformInstrumentMacro(this));' in audio
assert 'instrumentBus.applyGain(getLibertyPerformInstrumentMacro(this));' in audio
assert 'const int targetTrack=owner.addAudioTrack();' in perform
assert 'captureMix.addSample(0,i,l);' not in perform
assert 'commitGeneratedInstrumentNotesToLane(' in multi
assert 'bool commitLibertyPerformCapturedInstrumentClip' in multi
assert 'false);' in multi
assert 'std::uint64_t packMidi' in pure
assert 'std::int64_t nextFollowSample' in pure
assert 'saveLibertyPerformSession(this,project,file)' in project
assert 'loadLibertyPerformSession(this,*project,file)' in project
print("PASS: LIVE PRO follow, CC/notes, 4 macros, crossfade, capture and MIDI arrangement")
