#define private public
#include "MainComponent.h"
#undef private

#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <map>
#include <memory>

namespace
{
class OldSettingsCover final : public juce::Component
{
public:
    OldSettingsCover()
    {
        setInterceptsMouseClicks(true, true);
        setOpaque(true);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff15181d));
    }
};

class AudioSettingsRelocator final : private juce::Timer
{
public:
    explicit AudioSettingsRelocator(MainComponent& ownerIn) : owner(ownerIn)
    {
        button.setButtonText("AUDIO SETTINGS");
        button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
        button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff303640));
        button.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        button.setMouseClickGrabsKeyboardFocus(false);
        button.onClick = [this] { owner.openAudioSettings(); };

        owner.addAndMakeVisible(cover);
        owner.addAndMakeVisible(button);
        startTimerHz(10);
    }

    ~AudioSettingsRelocator() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        cover.setVisible(false);
        button.setVisible(false);
    }

private:
    void timerCallback() override
    {
        if (stopped.load()) return;

        // PROJECT occupies x=215..305. Keep an 8 px gap and place AUDIO SETTINGS
        // immediately to its right. This region is intentionally reserved.
        button.setBounds(313, 8, 128, 26);
        button.toFront(false);

        // Mask the legacy custom-painted/clickable AUDIO SETTINGS area so that
        // there is one and only one visible/clickable settings control.
        cover.setBounds(920, 6, 132, 32);
        cover.toFront(false);
        button.toFront(false);
    }

    MainComponent& owner;
    OldSettingsCover cover;
    juce::TextButton button;
    std::atomic<bool> stopped { false };
};

std::map<MainComponent*, std::unique_ptr<AudioSettingsRelocator>> controllers;

class Bootstrap final : private juce::Timer
{
public:
    Bootstrap() { startTimerHz(5); }
    ~Bootstrap() override { shutdown(); }

    void shutdown()
    {
        stopTimer();
        for (auto& item : controllers)
            if (item.second) item.second->shutdown();
        controllers.clear();
    }

private:
    void timerCallback() override
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i)))
                if (auto* main = dynamic_cast<MainComponent*>(window->getContentComponent()))
                    if (controllers.find(main) == controllers.end())
                        controllers.emplace(main, std::make_unique<AudioSettingsRelocator>(*main));
    }
};

Bootstrap bootstrap;
}

void shutdownLibertyAudioSettingsRelocator()
{
    bootstrap.shutdown();
}
