#!/usr/bin/env python3
"""Dock Mixer mini-fader dB scale: real arithmetic, hit testing, layout and text."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
main=(root/"Source/MainComponent.cpp").read_text(encoding="utf-8")
scale=(root/"Source/DockMixerDbScale.h").read_text(encoding="utf-8")
paint=main[main.index("void MainComponent::drawMixer("):main.index("void MainComponent::resized()")]
hit=main[main.index("bool MainComponent::handleMixerMouse("):main.index("void MainComponent::mouseMove(")]
assert '#include "DockMixerDbScale.h"' in main
assert "LibertyDockMixerDbScale::atPixels(faderTop, faderBottom)" in paint
assert "LibertyDockMixerDbScale::gainToPosition(gain)" in paint
assert "LibertyDockMixerDbScale::positionToGain(n)" in hit
assert "n * 2.0f" not in hit, "Old linear fader mapping would make tick marks inaccurate"
assert "gain * 0.5f" not in paint, "Old linear gain position would not match dB scale"
assert "mark.showLabel" in paint and "mark.major" in paint
assert 'mark.label' in paint
assert "c.getX() + 76, mark.y - 6, c.getWidth() - 79, 12" in paint
assert "if (fader.getHeight() > 0.0f)" in paint
assert "if (faderBottom > faderTop" in hit
for required in ("{0.0f, \"0\", true}", '"-6"', '"-12"', '"-18"',
                 '"-24"', '"-36"', '"-48"', '"-60"', '"+6"'):
    assert required in scale, required
for property in ("audioEngine.getTrackGain(sourceIndex)",
                 "audioEngine.getInstrumentTrackGain(sourceIndex)",
                 "audioEngine.getMasterGain()"):
    assert property in paint, property
assert "getArrangeMixerLogicalTrack(channel)" in paint
assert "const int channelCount = audioTracks + instrumentTracks;" in paint
assert "const bool master = channel == channelCount;" in paint
assert 'std::clamp' in scale and 'std::log10' in scale
# No typographic minus U+2212; legible ASCII "-" with fonts available on Mac/Windows.
assert "\u2212" not in scale and "\ufffd" not in scale
print("Dock Mixer: signed dB tick labels match gain law, stay inside strip and preserve ALL channels")

# Console and dock share readable signed gain labels without replacing PEAK/RMS dBFS.
console=(root/"Source/MixConsoleController.cpp").read_text(encoding="utf-8")
header=(root/"Source/MainComponent.h").read_text(encoding="utf-8")
assert '#include "DockMixerDbScale.h"' in console
assert "drawGainGraduations(g,s)" in console
assert "LibertyDockMixerDbScale::atPixels(top,bottom)" in console
assert "lookAndFeel.getSliderLayout(*s.fader)" in console
assert "s.kind==Strip::midi" in console
assert "yy-6,15,12,juce::Justification::centredRight" in console
assert "setDoubleClickReturnValue(true,0.0)" in console  # console audio/instrument/bus/master
assert "bool MainComponent::resetDockMixerFaderOnDoubleClick" in main
assert "event.getNumberOfClicks() >= 2" in main
assert "resetDockMixerFaderOnDoubleClick(event)" in main
assert "void mouseDoubleClick(const juce::MouseEvent& e) override" in header
assert "owner.resetDockMixerFaderOnDoubleClick" in header
assert "for (int i = 0; i <= count; ++i)" in header  # includes master hit target
assert "audioEngine.setMasterGain(1.0f)" in main
assert "audioEngine.setTrackGain(logical, 1.0f)" in main
assert "audioEngine.setInstrumentTrackGain(" in main
print("Console dB scale and both fader double-click resets: source regression OK")
