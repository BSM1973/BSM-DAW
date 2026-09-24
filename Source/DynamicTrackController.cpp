#include "MainComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>

namespace
{
class DynamicTrackController final : public juce::Component, private juce::Timer, private juce::ScrollBar::Listener
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
            countLabel.setText(juce::String(owner.getAudioTrackCount()) + " AUDIO  |  " +
                           juce::String(owner.getMidiTrackCount()) + " MIDI  |  " +
                           juce::String(owner.getInstrumentTrackCount()) + " INST", juce::dontSendNotification);
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
        scrollBar.setRangeLimits(0.0, 1.0);
        scrollBar.setCurrentRange(0.0, 1.0);
        scrollBar.addListener(this);
        addAndMakeVisible(scrollBar);
        owner.addAndMakeVisible(this);
        setBounds(8, 78, 178, 86);
        startTimerHz(4);
    }
    ~DynamicTrackController() override { scrollBar.removeListener(this); stopTimer(); }
    void resized() override
    {
        addAudio.setBounds(0, 0, 54, 24);
        addMidi.setBounds(58, 0, 50, 24);
        addInstrument.setBounds(112, 0, 66, 24);
        countLabel.setBounds(0, 28, 178, 24);
        scrollBar.setBounds(0, 58, 178, 16);
    }
private:
    void scrollBarMoved(juce::ScrollBar*, double newRangeStart)
    {
        owner.setTrackScrollRows((int)std::round(newRangeStart));
    }
    void updateScrollRange()
    {
        const int rowH = getLibertyTrackRowHeight();
        const int available = juce::jmax(1, owner.getHeight() - 76 - 32 - 210);
        const int visible = juce::jmax(1, available / rowH);
        const int total = owner.getTotalArrangeTrackCount();
        const int maxStart = juce::jmax(0, total - visible);
        const int current = juce::jlimit(0, maxStart, owner.getTrackScrollRows());
        if (current != owner.getTrackScrollRows()) owner.setTrackScrollRows(current);
        scrollBar.setRangeLimits(0.0, (double)juce::jmax(1, maxStart + visible));
        scrollBar.setCurrentRange((double)current, (double)visible, juce::dontSendNotification);
        scrollBar.setVisible(maxStart > 0);
    }
    void timerCallback() override
    {
        countLabel.setText(juce::String(owner.getAudioTrackCount()) + " AUDIO", juce::dontSendNotification);
        updateScrollRange();
        toFront(false);
    }
    MainComponent& owner;
    juce::TextButton addAudio, addMidi, addInstrument;
    juce::Label countLabel;
    juce::ScrollBar scrollBar { false };
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
