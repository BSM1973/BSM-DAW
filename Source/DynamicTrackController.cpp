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
            countLabel.setText(juce::String(owner.getAudioTrackCount()) + " AUDIO", juce::dontSendNotification);
        };
        addAndMakeVisible(addAudio);
        countLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9fc7e8));
        countLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(countLabel);
        owner.addAndMakeVisible(this);
        setBounds(8, 78, 190, 28);
        startTimerHz(4);
    }
    ~DynamicTrackController() override { stopTimer(); }
    void resized() override
    {
        addAudio.setBounds(0, 0, 82, 26);
        countLabel.setBounds(88, 0, 98, 26);
    }
private:
    void timerCallback() override
    {
        countLabel.setText(juce::String(owner.getAudioTrackCount()) + " AUDIO", juce::dontSendNotification);
        toFront(false);
    }
    MainComponent& owner;
    juce::TextButton addAudio;
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
