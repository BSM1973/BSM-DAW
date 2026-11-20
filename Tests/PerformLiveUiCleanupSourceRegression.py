#!/usr/bin/env python3
"""Regression: no overlapping PERFORMANCE toolbar text or corrupted French labels."""
from pathlib import Path
import re

root=Path(__file__).resolve().parents[1]
perform=(root/"Source/PerformController.cpp").read_text(encoding="utf-8")
toolbar=(root/"Source/DynamicTrackController.cpp").read_text(encoding="utf-8")
geometry=(root/"Source/NoOverlapLayout.h").read_text(encoding="utf-8")

# Purple: the shared track-creation row may never cover another painted header.
assert 'g.drawText("PERFORM", 22, 10, 150, 28' not in perform
assert 'SESSION   CLIPS   SCÈNES   LIVE' not in perform
assert 'LibertyNoOverlap::creationZoneWidth+16' in perform
assert 'const int titleX=juce::jmax(loopButton.getRight()+18,' in perform
assert 'if(titleX+160<getWidth()-16)' in perform
assert '"PERFORMANCE",titleX,16,160,24' in perform

# Red: do not borrow the competitor's phrase in any user-facing title.
assert 'g.drawText("FOLLOW ACTIONS / MIDI LEARN"' not in perform
assert 'g.drawText(juce::String::fromUTF8("SCÈNES ET MIDI")' in perform

# Green and related controls: JUCE strings must be decoded explicitly as UTF-8.
assert 'juce::String::fromUTF8("NON ASSIGNÉ")' in perform
assert 'juce::String("NON ASSIGNÉ")' not in perform
assert 'juce::String::fromUTF8(track<audioTrackCount()?' in perform
assert 'juce::String::fromUTF8(' in perform

# The four creation buttons already occupy distinct JUCE rectangles: preserve it.
names=('audioButton','midiButton','instrumentButton','busButton','heightIcon')
rect={}
for name in names:
    match=re.search(r'constexpr Rect '+name+r'\{([0-9, ]+)\};',geometry)
    assert match, f"Missing creation rectangle: {name}"
    rect[name]=list(map(int,match.group(1).split(',')))
for i,name in enumerate(names):
    x,y,w,h=rect[name]
    assert w>0 and h>0
    for other in names[i+1:]:
        ox,oy,ow,oh=rect[other]
        assert x+w<=ox or ox+ow<=x or y+h<=oy or oy+oh<=y, (
            f"Overlapping buttons: {name} and {other}")
for code in (
    'put(addAudio,LibertyNoOverlap::audioButton);',
    'put(addMidi,LibertyNoOverlap::midiButton);',
    'put(addInstrument,LibertyNoOverlap::instrumentButton);',
    'put(addBus,LibertyNoOverlap::busButton);',
):
    assert code in toolbar

print("PASS: PERFORMANCE creation buttons, original French title, UTF-8 and no text collision")
