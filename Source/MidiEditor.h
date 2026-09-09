#pragma once

class MainComponent;

// Opens the MIDI editor for Liberty's first MIDI track.
// The editor owns no project state; it edits MainComponent::midiEngine directly.
void openLibertyMidiEditor(MainComponent& owner);
