#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class MainComponent final : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
};
