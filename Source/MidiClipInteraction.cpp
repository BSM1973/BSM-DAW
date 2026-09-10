#include "MainComponent.h"
#include "MidiEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>

namespace
{
class MidiClipInteraction final : private juce::Timer, private juce::MouseListener
{
public:
    MidiClipInteraction() { startTimerHz(30); }
    ~MidiClipInteraction() override { detach(); }

private:
    void timerCallback() override
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
        {
            auto* window = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i));
            if (window == nullptr) continue;
            auto* main = dynamic_cast<MainComponent*>(window->getContentComponent());
            if (main == nullptr) continue;
            attach(main);
        }
    }

    void attach(juce::Component* component)
    {
        if (component == nullptr) return;
        if (attached.insert(component).second)
            component->addMouseListener(this, true);
    }

    void detach()
    {
        for (auto* component : attached)
            if (component != nullptr)
                component->removeMouseListener(this);
        attached.clear();
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        auto* component = event.eventComponent;
        if (component == nullptr) return;

        auto* main = dynamic_cast<MainComponent*>(component->getParentComponent());
        if (main == nullptr) return;

        // The MIDI clip is the dedicated 70px row immediately after the four
        // audio tracks. Only react to the timeline clip component, never to
        // transport, mixer, or Piano Roll controls.
        constexpr int midiRowY = 76 + 32 + (4 * 70);
        const auto bounds = component->getBounds();
        if (bounds.getX() != 210 || bounds.getY() != midiRowY || bounds.getHeight() != 70)
            return;

        const auto local = event.getEventRelativeTo(component).position;
        if (local.x < 0.0f || local.x > static_cast<float>(component->getWidth())
            || local.y < 0.0f || local.y > static_cast<float>(component->getHeight()))
            return;

        openLibertyMidiEditor(*main);
    }

    std::set<juce::Component*> attached;
};

MidiClipInteraction globalMidiClipInteraction;
}
