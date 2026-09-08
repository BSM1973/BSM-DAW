#include "MainComponent.h"

MainComponent::MainComponent()
{
    setSize(1280, 720);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff111111));

    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(32.0f, juce::Font::bold));
    g.drawText("BSM DAW", getLocalBounds().reduced(40),
               juce::Justification::centred, false);

    g.setFont(juce::Font(16.0f));
    g.setColour(juce::Colours::lightgrey);
    g.drawText("Project Foundation  •  Version 0.1.0",
               getLocalBounds().reduced(40).withTrimmedTop(90),
               juce::Justification::centred, false);
}

void MainComponent::resized()
{
}
