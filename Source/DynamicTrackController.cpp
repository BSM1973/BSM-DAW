#include "MainComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>

namespace
{
class DynamicTrackController final : public juce::Component, private juce::Timer
{
public:
    explicit DynamicTrackController(MainComponent& o) : owner(o)
    {
        addAudio.setButtonText("+ AUDIO");
        addAudio.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff245b70));
        addAudio.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        addAudio.setMouseClickGrabsKeyboardFocus(false);
        addAudio.onClick = [this]
        {
            owner.addAudioTrack();
            countLabel.setVisible(false);
        };
        addMidi.setButtonText("+ MIDI");
        addInstrument.setButtonText("+ INSTRUMENT");
        for (auto* b : { &addAudio, &addMidi, &addInstrument })
        {
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff245b70));
            b->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            b->setMouseClickGrabsKeyboardFocus(false);
            addAndMakeVisible(*b);
        }
        addMidi.onClick = [this] { owner.addMidiTrack(); };
        addInstrument.onClick = [this] { owner.addInstrumentTrack(); };
        countLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9fc7e8));
        countLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(countLabel);
        setInterceptsMouseClicks(false, true);
        owner.addAndMakeVisible(this);
        setBounds(owner.getLocalBounds());
        startTimerHz(4);
    }
    ~DynamicTrackController() override { stopTimer(); }
    void resized() override
    {
        addAudio.setBounds(8, 78, 54, 24);
        addMidi.setBounds(66, 78, 50, 24);
        addInstrument.setBounds(120, 78, 66, 24);
        countLabel.setVisible(false);
    }
private:
    void timerCallback() override
    {
        if (getBounds() != owner.getLocalBounds())
            setBounds(owner.getLocalBounds());
    }
    MainComponent& owner;
    juce::TextButton addAudio, addMidi, addInstrument;
    juce::Label countLabel;
};

std::map<MainComponent*, std::unique_ptr<DynamicTrackController>> controllers;

class Bootstrap final : private juce::Timer
{
public:
    Bootstrap() { startTimerHz(4); }
    ~Bootstrap() override { stopTimer(); controllers.clear(); }
private:
    void timerCallback() override
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i)))
                if (auto* main = dynamic_cast<MainComponent*>(window->getContentComponent()))
                    if (!controllers.count(main))
                        controllers.emplace(main, std::make_unique<DynamicTrackController>(*main));
    }
};
Bootstrap bootstrap;
}
