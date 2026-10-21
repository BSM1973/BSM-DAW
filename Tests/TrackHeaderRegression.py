"""Guard previously validated Liberty track-header interactions.

Run: python3 Tests/TrackHeaderRegression.py
These source-level checks catch accidental removal of validated wiring.
They complement (but do not replace) interactive macOS UI tests.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
header = (root / "Source/TrackHeaderMixControls.cpp").read_text()
zoom = (root / "Source/ZoomController.cpp").read_text()
dynamic = (root / "Source/DynamicTrackController.cpp").read_text()

checks = {
    "Instrument MIDI input control is constructed": "c.midiInput=std::make_unique<juce::ComboBox>()" in header,
    "Instrument MIDI input is populated": 'box->addItem(juce::String::fromUTF8("Entrées"),1)' in header,
    "Instrument MIDI input changes audio routing": "setInstrumentMidiInput(t," in header,
    "Instrument MIDI input remains visible by row geometry": "owner.getArrangeTrackAtY(selectorY+12)==logical" in header,
    "Instrument MIDI input is brought to front": "c.midiInput->toFront(false)" in header,
    "Track height icon cycles 3 presets": "cycleLibertyTrackHeightPreset(); repaint();" in dynamic,
    "Preset applies actual track heights": "main->applyArrangeTrackHeightPreset()" in zoom,
    "Timeline zoom does not overlap height icon": "p.x >= 202 && p.x <= 218" not in zoom,
}
for name, ok in checks.items():
    print(("PASS" if ok else "FAIL") + ": " + name)
if not all(checks.values()):
    raise SystemExit("Track-header regression guard failed")
