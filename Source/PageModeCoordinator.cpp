#define private public
#include "MainComponent.h"
#undef private

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>

bool isLibertyMixConsoleVisible(MainComponent* owner);
bool isLibertyPerformVisible(MainComponent* owner);
void setLibertyPerformVisible(MainComponent* owner, bool shouldShow);

namespace
{
class PageModeCoordinator;
std::map<MainComponent*, std::unique_ptr<PageModeCoordinator>> coordinators;

class PageModeCoordinator final : private juce::Timer
{
public:
    explicit PageModeCoordinator(MainComponent& ownerIn) : owner(ownerIn)
    {
        configureButton(arrangeButton, "ARRANGE");
        configureButton(mixButton, "MIXCONSOLE");
        configureButton(performButton, "PERFORM");

        captureLegacyButtons();

        arrangeButton.onClick = [this]
        {
            captureLegacyButtons();
            setLibertyPerformVisible(&owner, false);
            if (legacyArrange != nullptr) legacyArrange->triggerClick();
            refresh();
        };

        mixButton.onClick = [this]
        {
            captureLegacyButtons();
            setLibertyPerformVisible(&owner, false);
            if (legacyMix != nullptr) legacyMix->triggerClick();
            refresh();
        };

        performButton.onClick = [this]
        {
            captureLegacyButtons();
            if (isLibertyMixConsoleVisible(&owner) && legacyArrange != nullptr)
                legacyArrange->triggerClick();
            setLibertyPerformVisible(&owner, true);
            refresh();
        };

        owner.addAndMakeVisible(arrangeButton);
        owner.addAndMakeVisible(mixButton);
        owner.addAndMakeVisible(performButton);
        startTimerHz(30);
        refresh();
    }

    ~PageModeCoordinator() override { shutdown(); }

    void shutdown()
    {
        stopTimer();
        arrangeButton.setVisible(false);
        mixButton.setVisible(false);
        performButton.setVisible(false);
    }

    void setMixVisible(bool shouldShow)
    {
        captureLegacyButtons();
        if (shouldShow)
        {
            setLibertyPerformVisible(&owner, false);
            if (legacyMix != nullptr) legacyMix->triggerClick();
        }
        else if (isLibertyMixConsoleVisible(&owner))
        {
            if (legacyArrange != nullptr) legacyArrange->triggerClick();
        }
        refresh();
    }

private:
    void configureButton(juce::TextButton& button, const juce::String& text)
    {
        button.setButtonText(text);
        button.setClickingTogglesState(false);
        button.setMouseClickGrabsKeyboardFocus(false);
        button.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    }

    void captureLegacyButtons()
    {
        for (int i = 0; i < owner.getNumChildComponents(); ++i)
        {
            auto* button = dynamic_cast<juce::TextButton*>(owner.getChildComponent(i));
            if (button == nullptr) continue;
            if (button == &arrangeButton || button == &mixButton || button == &performButton) continue;

            const auto text = button->getButtonText();
            if (text == "ARRANGE" && legacyArrange == nullptr) legacyArrange = button;
            else if (text == "MIXCONSOLE" && legacyMix == nullptr) legacyMix = button;
            else if (text == "PERFORM" && legacyPerform == nullptr) legacyPerform = button;
        }
    }

    void hideEveryLegacyPageButton()
    {
        for (int i = 0; i < owner.getNumChildComponents(); ++i)
        {
            auto* button = dynamic_cast<juce::TextButton*>(owner.getChildComponent(i));
            if (button == nullptr) continue;
            if (button == &arrangeButton || button == &mixButton || button == &performButton) continue;
            const auto text = button->getButtonText();
            if (text == "ARRANGE" || text == "MIXCONSOLE" || text == "PERFORM")
                button->setVisible(false);
        }
    }

    void refresh()
    {
        captureLegacyButtons();

        const bool perform = isLibertyPerformVisible(&owner);
        const bool mix = !perform && isLibertyMixConsoleVisible(&owner);
        const bool arrange = !perform && !mix;

        const auto active = juce::Colour(0xff315f7a);
        const auto inactive = juce::Colour(0xff252a31);
        arrangeButton.setColour(juce::TextButton::buttonColourId, arrange ? active : inactive);
        mixButton.setColour(juce::TextButton::buttonColourId, mix ? active : inactive);
        performButton.setColour(juce::TextButton::buttonColourId, perform ? active : inactive);

        arrangeButton.setBounds(1055, 8, 88, 26);
        mixButton.setBounds(1147, 8, 112, 26);
        performButton.setBounds(1263, 8, 94, 26);

        hideEveryLegacyPageButton();
        arrangeButton.setVisible(true);
        mixButton.setVisible(true);
        performButton.setVisible(true);
        arrangeButton.toFront(false);
        mixButton.toFront(false);
        performButton.toFront(false);

        if (perform)
        {
            for (int i = 0; i < owner.getNumChildComponents(); ++i)
            {
                auto* child = owner.getChildComponent(i);
                if (child != nullptr && child->isVisible() && child->isAlwaysOnTop())
                    child->toFront(false);
            }
            arrangeButton.toFront(false);
            mixButton.toFront(false);
            performButton.toFront(false);
        }
    }

    void timerCallback() override { refresh(); }

    MainComponent& owner;
    juce::TextButton arrangeButton, mixButton, performButton;
    juce::TextButton* legacyArrange = nullptr;
    juce::TextButton* legacyMix = nullptr;
    juce::TextButton* legacyPerform = nullptr;
};

class Bootstrap final : private juce::Timer
{
public:
    Bootstrap() { startTimerHz(10); }
    ~Bootstrap() override { shutdown(); }

    void shutdown()
    {
        stopTimer();
        for (auto& entry : coordinators)
            if (entry.second) entry.second->shutdown();
        coordinators.clear();
    }

private:
    void timerCallback() override
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i)))
                if (auto* main = dynamic_cast<MainComponent*>(window->getContentComponent()))
                    if (coordinators.find(main) == coordinators.end())
                        coordinators.emplace(main, std::make_unique<PageModeCoordinator>(*main));
    }
};

Bootstrap bootstrap;
}

void setLibertyMixConsoleVisible(MainComponent* owner, bool shouldShow)
{
    if (owner == nullptr) return;
    const auto it = coordinators.find(owner);
    if (it == coordinators.end() || !it->second) return;
    it->second->setMixVisible(shouldShow);
}

void shutdownLibertyPageModeCoordinator()
{
    bootstrap.shutdown();
}
