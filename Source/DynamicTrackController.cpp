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
        scrollBar.setRangeLimits(0.0, 1.0);
        scrollBar.setCurrentRange(0.0, 1.0);
        scrollBar.addListener(this);
        addAndMakeVisible(scrollBar);
        setInterceptsMouseClicks(false, true);
        owner.addAndMakeVisible(this);
        setBounds(owner.getLocalBounds());
        startTimerHz(4);
    }
    ~DynamicTrackController() override { scrollBar.removeListener(this); stopTimer(); }
    void resized() override
    {
        addAudio.setBounds(8, 78, 54, 24);
        addMidi.setBounds(66, 78, 50, 24);
        addInstrument.setBounds(120, 78, 66, 24);
        countLabel.setVisible(false);
        const int top = 108;
        const int bottom = juce::jmax(top + 24, getHeight() - 210);
        // Keep track scrolling beside the track headers, not at the far-right
        // edge of the arranger/timeline.
        scrollBar.setBounds(194, top, 12, bottom - top);
    }
private:
    void scrollBarMoved(juce::ScrollBar*, double newRangeStart)
    {
        owner.setTrackScrollRows((int)std::round(newRangeStart));
    }
    void updateScrollRange()
    {
        const int rowH = getLibertyTrackRowHeight();
        const int available = juce::jmax(1, owner.getHeight() - 108 - 214);
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
        if (getBounds() != owner.getLocalBounds())
            setBounds(owner.getLocalBounds());
        updateScrollRange();
    }
    MainComponent& owner;
    juce::TextButton addAudio, addMidi, addInstrument;
    juce::Label countLabel;
    juce::ScrollBar scrollBar { true };
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
