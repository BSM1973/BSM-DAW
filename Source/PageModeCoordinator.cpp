#define private public
#include "MainComponent.h"
#undef private

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <memory>

bool isLibertyMixConsoleVisible(MainComponent* owner);
bool isLibertyPerformVisible(MainComponent* owner);

namespace
{
juce::TextButton* findButtonRecursive(juce::Component* root, const juce::String& text)
{
    if (root == nullptr) return nullptr;
    if (auto* button = dynamic_cast<juce::TextButton*>(root))
        if (button->getButtonText() == text)
            return button;

    for (int i = 0; i < root->getNumChildComponents(); ++i)
        if (auto* found = findButtonRecursive(root->getChildComponent(i), text))
            return found;
    return nullptr;
}

class PageModeCoordinator final : private juce::Timer
{
public:
    explicit PageModeCoordinator(MainComponent& ownerIn) : owner(ownerIn)
    {
        startTimerHz(30);
    }

    ~PageModeCoordinator() override { stopTimer(); }

private:
    void timerCallback() override
    {
        const bool perform = isLibertyPerformVisible(&owner);
        const bool mix = !perform && isLibertyMixConsoleVisible(&owner);
        const bool arrange = !perform && !mix;

        auto* arrangeButton = findButtonRecursive(&owner, "ARRANGE");
        auto* mixButton = findButtonRecursive(&owner, "MIXCONSOLE");
        auto* performButton = findButtonRecursive(&owner, "PERFORM");

        const auto active = juce::Colour(0xff315f7a);
        const auto inactive = juce::Colour(0xff252a31);

        if (arrangeButton != nullptr)
        {
            arrangeButton->setColour(juce::TextButton::buttonColourId, arrange ? active : inactive);
            arrangeButton->toFront(false);
        }
        if (mixButton != nullptr)
        {
            mixButton->setColour(juce::TextButton::buttonColourId, mix ? active : inactive);
            mixButton->toFront(false);
        }
        if (performButton != nullptr)
        {
            performButton->setColour(juce::TextButton::buttonColourId, perform ? active : inactive);
            performButton->toFront(false);
        }

        if (perform)
        {
            // PERFORM is an exclusive full-page mode. Keep its always-on-top view
            // above every legacy ARRANGE overlay which may still repaint itself.
            for (int i = 0; i < owner.getNumChildComponents(); ++i)
            {
                auto* child = owner.getChildComponent(i);
                if (child != nullptr && child->isVisible() && child->isAlwaysOnTop())
                    child->toFront(false);
            }

            // Put page-navigation buttons back above the full-page view.
            if (arrangeButton != nullptr) arrangeButton->toFront(false);
            if (mixButton != nullptr) mixButton->toFront(false);
            if (performButton != nullptr) performButton->toFront(false);
        }
    }

    MainComponent& owner;
};

std::map<MainComponent*, std::unique_ptr<PageModeCoordinator>> coordinators;

class Bootstrap final : private juce::Timer
{
public:
    Bootstrap() { startTimerHz(10); }
    ~Bootstrap() override { shutdown(); }

    void shutdown()
    {
        stopTimer();
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

// Compatibility function used by PERFORM. The original MIXCONSOLE controller
// exposes its state but not a setter, so this routes the request through the
// existing page buttons rather than duplicating console ownership.
void setLibertyMixConsoleVisible(MainComponent* owner, bool shouldShow)
{
    if (owner == nullptr) return;
    const bool current = isLibertyMixConsoleVisible(owner);
    if (current == shouldShow) return;

    if (shouldShow)
    {
        if (auto* button = findButtonRecursive(owner, "MIXCONSOLE"))
            button->triggerClick();
    }
    else
    {
        if (auto* button = findButtonRecursive(owner, "ARRANGE"))
            button->triggerClick();
    }
}

void shutdownLibertyPageModeCoordinator()
{
    bootstrap.shutdown();
}
