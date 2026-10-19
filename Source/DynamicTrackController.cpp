#include "MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>

int getLibertyTrackHeightPreset() noexcept;
void cycleLibertyTrackHeightPreset();

namespace
{
class TrackHeightIcon final : public juce::Component
{
public:
    TrackHeightIcon() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    void paint(juce::Graphics& g) override
    {
        const int preset = getLibertyTrackHeightPreset();
        g.setColour(juce::Colour(0xff80d8e8));
        const int bars[] = { 3, 6, 10 };
        for (int i = 0; i < 3; ++i)
        {
            const int h = bars[i];
            g.setColour(i == preset ? juce::Colour(0xff54d6dc) : juce::Colour(0xff647c8c));
            g.fillRoundedRectangle(4.0f + i * 11.0f, 18.0f - h, 7.0f, (float)h, 1.5f);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isLeftButtonDown()) { cycleLibertyTrackHeightPreset(); repaint(); }
    }
};

class DynamicTrackController final : public juce::Component
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
        addAndMakeVisible(heightIcon);
        setInterceptsMouseClicks(false, true);
        owner.addAndMakeVisible(this);
        setBounds(owner.getLocalBounds());
    }
    void resized() override
    {
        addAudio.setBounds(8, 78, 54, 24);
        addMidi.setBounds(66, 78, 50, 24);
        addInstrument.setBounds(120, 78, 66, 24);
        countLabel.setVisible(false);
        heightIcon.setBounds(190, 79, 38, 21);
    }
public:
    void resizeToOwner() { setBounds(owner.getLocalBounds()); }
private:
    MainComponent& owner;
    juce::TextButton addAudio, addMidi, addInstrument;
    juce::Label countLabel;
    TrackHeightIcon heightIcon;
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
                    {
                        controllers.emplace(main, std::make_unique<DynamicTrackController>(*main));
                        stopTimer();
                        return;
                    }
    }
};
Bootstrap bootstrap;
}

void resizeLibertyDynamicTrackController(MainComponent* owner)
{
    if (owner == nullptr) return;
    auto it = controllers.find(owner);
    if (it != controllers.end() && it->second)
        it->second->resizeToOwner();
}
