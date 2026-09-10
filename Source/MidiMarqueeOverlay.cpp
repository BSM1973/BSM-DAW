#include "MidiMarqueeOverlay.h"
#include "MainComponent.h"
#include <memory>

namespace
{
MainComponent* findMainComponent(juce::Component* component) noexcept
{
    if (component == nullptr)
        return nullptr;
    if (auto* main = dynamic_cast<MainComponent*>(component))
        return main;
    for (int i = 0; i < component->getNumChildComponents(); ++i)
        if (auto* main = findMainComponent(component->getChildComponent(i)))
            return main;
    return nullptr;
}

juce::DocumentWindow* findMidiWindow(juce::Component* component) noexcept
{
    while (component != nullptr)
    {
        if (auto* window = dynamic_cast<juce::DocumentWindow*>(component))
            if (window->getName() == "Liberty - MIDI 1")
                return window;
        component = component->getParentComponent();
    }
    return nullptr;
}

class MidiMarqueeManager final : private juce::Timer
{
public:
    MidiMarqueeManager() { startTimerHz(30); }
    ~MidiMarqueeManager() override = default;

private:
    void timerCallback() override
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
        {
            auto* window = findMidiWindow(desktop.getComponent(i));
            if (window == nullptr)
                continue;

            auto* content = window->getContentComponent();
            if (content == nullptr || findOverlay(content) != nullptr)
                continue;

            auto* main = findMainComponent(content);
            if (main == nullptr)
                continue;

            auto overlay = std::make_unique<MidiMarqueeOverlay>(*main);
            auto* overlayPtr = overlay.get();
            content->addAndMakeVisible(overlayPtr);
            overlayPtr->setBounds(content->getLocalBounds());
            overlayPtr->setAlwaysOnTop(true);
            overlayPtr->toFront(false);
            overlay.release();
        }
    }

    static MidiMarqueeOverlay* findOverlay(juce::Component* content) noexcept
    {
        for (int i = 0; i < content->getNumChildComponents(); ++i)
            if (auto* overlay = dynamic_cast<MidiMarqueeOverlay*>(content->getChildComponent(i)))
                return overlay;
        return nullptr;
    }
};

MidiMarqueeManager midiMarqueeManager;
}
