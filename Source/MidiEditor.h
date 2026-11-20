#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class MainComponent;

void openLibertyMidiEditor(MainComponent& owner);
void openLibertyMidiEditorInDock(MainComponent& owner);
void shutdownLibertyMidiEditor();
void shutdownLibertyMidiNoteSelectionInteraction();

// Embed the existing editable Piano Roll in the ARRANGE dock.
void layoutLibertyPianoRollDock(MainComponent& owner, juce::Rectangle<int> bounds, bool visible);
