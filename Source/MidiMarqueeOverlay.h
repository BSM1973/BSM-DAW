#pragma once

#include <JuceHeader.h>
#include <vector>
#include "MidiEngine.h"

class MainComponent;

class MidiMarqueeOverlay final : public juce::Component
{
public:
    explicit MidiMarqueeOverlay(MainComponent& owner);
    void paint(juce::Graphics& g) override;
    bool hitTest(int x, int y) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
private:
    enum class ResizeSide { none, left, right };

    bool isInPianoRollGrid(juce::Point<int> p) const noexcept;
    bool pointHitsAnyNote(juce::Point<int> p) const;
    bool findSelectedResizeSide(juce::Point<int> p, ResizeSide& side) const;
    std::int64_t tickFromX(int x) const noexcept;
    juce::Rectangle<int> noteRectangle(juce::Point<int> p, std::int64_t startTick, std::int64_t lengthTicks, int pitch) const;
    void applySelection();
    MainComponent& owner;
    bool dragging = false;
    bool marqueeActive = false;
    bool resizeActive = false;
    ResizeSide resizeSide = ResizeSide::none;
    std::int64_t lastResizeTick = 0;
    juce::Point<int> dragStart;
    juce::Point<int> dragCurrent;
    std::vector<MidiEngine::NoteEvent> baseSelection;
};
