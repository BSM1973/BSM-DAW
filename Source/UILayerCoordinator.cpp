#define private public
#include "MainComponent.h"
#undef private

#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <map>
#include <memory>

namespace
{
juce::Component* findChildWithButtons(MainComponent& owner,
                                      const juce::String& first,
                                      const juce::String& second)
{
    for (int i = 0; i < owner.getNumChildComponents(); ++i)
    {
        auto* child = owner.getChildComponent(i);
        if (child == nullptr) continue;
        bool a = false, b = false;
        for (int j = 0; j < child->getNumChildComponents(); ++j)
        {
            if (auto* button = dynamic_cast<juce::TextButton*>(child->getChildComponent(j)))
            {
                a = a || button->getButtonText() == first;
                b = b || button->getButtonText() == second;
            }
        }
        if (a && b) return child;
    }
    return nullptr;
}

class UILayerCoordinator final : private juce::Timer
{
public:
    explicit UILayerCoordinator(MainComponent& ownerIn) : owner(ownerIn)
    {
        startTimerHz(4);
    }

    ~UILayerCoordinator() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
    }

private:
    void timerCallback() override
    {
        if (stopped.load()) return;

        auto* browser = findChildWithButtons(owner, "FILES", "PLUGINS");
        auto* perform = findChildWithButtons(owner, "SCENE 1", "STOP ALL");

        if (perform != nullptr && perform->isAlwaysOnTop())
            perform->setAlwaysOnTop(false);

        if (browser != nullptr && !browser->isAlwaysOnTop())
            browser->setAlwaysOnTop(true);
    }

    MainComponent& owner;
    std::atomic<bool> stopped { false };
};

std::map<MainComponent*, std::unique_ptr<UILayerCoordinator>> coordinators;

class Bootstrap final : private juce::Timer
{
public:
    Bootstrap() { startTimerHz(8); }
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
                        coordinators.emplace(main, std::make_unique<UILayerCoordinator>(*main));
    }
};

Bootstrap bootstrap;
}

void shutdownLibertyUILayerCoordinator()
{
    bootstrap.shutdown();
}
