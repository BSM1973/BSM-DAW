#include "MainComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <map>
#include <memory>

bool isLibertyMixConsoleVisible(MainComponent* owner);
bool isLibertyPerformVisible(MainComponent* owner);
void setLibertyPerformVisible(MainComponent* owner, bool shouldShow);

namespace
{
juce::TextButton* findPageButton(MainComponent* owner, const juce::String& text)
{
    if (owner == nullptr) return nullptr;
    for (int i = 0; i < owner->getNumChildComponents(); ++i)
        if (auto* button = dynamic_cast<juce::TextButton*>(owner->getChildComponent(i)))
            if (button->getButtonText() == text)
                return button;
    return nullptr;
}

MainComponent* findMainComponent(juce::Component* component)
{
    for (auto* c = component; c != nullptr; c = c->getParentComponent())
        if (auto* main = dynamic_cast<MainComponent*>(c))
            return main;
    return nullptr;
}

class NavigationBridge final : private juce::MouseListener, private juce::Timer
{
public:
    NavigationBridge()
    {
        juce::Desktop::getInstance().addGlobalMouseListener(this);
        startTimerHz(12);
    }

    ~NavigationBridge() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        juce::Desktop::getInstance().removeGlobalMouseListener(this);
    }

private:
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (stopped.load()) return;
        auto* button = dynamic_cast<juce::TextButton*>(event.eventComponent);
        if (button == nullptr) return;

        const auto text = button->getButtonText();
        if (text != "ARRANGE" && text != "MIXCONSOLE") return;

        if (auto* main = findMainComponent(button))
            if (isLibertyPerformVisible(main))
                setLibertyPerformVisible(main, false);
    }

    void timerCallback() override
    {
        if (stopped.load()) return;
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
        {
            auto* window = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i));
            if (window == nullptr) continue;
            auto* main = dynamic_cast<MainComponent*>(window->getContentComponent());
            if (main == nullptr) continue;

            // Legacy ARRANGE overlay controllers periodically bring themselves to front.
            // Reassert PERFORM z-order while that page is active so nothing leaks over it.
            if (isLibertyPerformVisible(main))
                setLibertyPerformVisible(main, true);
        }
    }

    std::atomic<bool> stopped { false };
};

NavigationBridge bridge;
}

void setLibertyMixConsoleVisible(MainComponent* owner, bool shouldShow)
{
    if (owner == nullptr) return;
    if (auto* button = findPageButton(owner, shouldShow ? "MIXCONSOLE" : "ARRANGE"))
        button->triggerClick();
}

void shutdownLibertyPageNavigationBridge()
{
    bridge.shutdown();
}
