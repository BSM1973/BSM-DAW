#!/usr/bin/env python3
"""Prevent ARRANGEMENT's golden playhead from painting over PERFORMANCE."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "Source" / "MainComponent.cpp").read_text(encoding="utf-8")
perform = (root / "Source" / "PerformController.cpp").read_text(encoding="utf-8")

start = main.index("void MainComponent::paintOverChildren(")
end = main.index("\nvoid MainComponent::paint(", start)
overlay = main[start:end]
guard = "if (isLibertyMixConsoleVisible(this) || isLibertyPerformVisible(this)) return;"
assert guard in overlay, "The over-children overlay must be hidden in MIXCONSOLE and PERFORMANCE"
assert overlay.index(guard) < overlay.index("const float x ="), "Guard must run before cursor coordinates"
assert overlay.index(guard) < overlay.index("g.drawLine(x,"), "Never draw the cursor over PERFORMANCE"
assert "playheadSeconds * getLibertyTimelinePixelsPerSecond()" in overlay
assert 'juce::Colour(0xfff8b84d)' in overlay, "Retain the orange playhead in ARRANGEMENT"

# PERFORMANCE intentionally owns its independent live audio engine and opaque grid;
# it must not receive the ARRANGEMENT overlay after its own children are drawn.
assert "bool isLibertyPerformVisible(MainComponent* owner)" in perform
assert "owner.audioEngine.setPlaying(false);" in perform
assert "owner.isPlaying = false;" in perform
assert "g.fillAll(LibertyUI::Colour::canvasDeep);" in perform

print("PASS: golden playhead remains in ARRANGEMENT; neither PERFORMANCE nor MIXCONSOLE paints it")
