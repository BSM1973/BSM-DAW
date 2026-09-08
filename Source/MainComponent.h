#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawTransport(juce::Graphics& g, juce::Rectangle<int> area);
    void drawTrackArea(juce::Graphics& g, juce::Rectangle<int> area);
    void drawMixer(juce::Graphics& g, juce::Rectangle<int> area);

    bool isPlaying = false;
    double playheadSeconds = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
