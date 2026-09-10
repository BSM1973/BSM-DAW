#include "MainComponent.h"
#include "MidiEngine.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
class MidiQuantizeKeyListener final : private juce::Timer, public juce::KeyListener
{
public:
    MidiQuantizeKeyListener() { startTimerHz(10); }
    ~MidiQuantizeKeyListener() override { stopTimer(); detachFromContent(); }

    bool keyPressed(const juce::KeyPress& key, juce::Component*) override
    {
        auto* main = findMainComponent();
        if (main == nullptr)
            return false;

        const auto modifiers = key.getModifiers();
        const bool commandOrCtrl = modifiers.isCommandDown() || modifiers.isCtrlDown();
        const int keyCode = key.getKeyCode();

        if (commandOrCtrl && (keyCode == 'Z' || keyCode == 'z'))
        {
            if (!main->getMidiEngine().undo()) return false;
            main->updateMidiClipTiming();
            main->repaint();
            return true;
        }

        if (commandOrCtrl && (keyCode == 'Y' || keyCode == 'y'))
        {
            if (!main->getMidiEngine().redo()) return false;
            main->updateMidiClipTiming();
            main->repaint();
            return true;
        }

        if (commandOrCtrl || main->getMidiEngine().getNumSelectedNotes() == 0)
            return false;

        std::int64_t gridTicks = 0;
        if (keyCode == 'Q' || keyCode == 'q') gridTicks = MidiEngine::ticksPerQuarterNote / 4;      // 1/16
        else if (keyCode == 'W' || keyCode == 'w') gridTicks = MidiEngine::ticksPerQuarterNote / 2; // 1/8
        else if (keyCode == 'E' || keyCode == 'e') gridTicks = MidiEngine::ticksPerQuarterNote;     // 1/4
        else return false;

        if (!main->getMidiEngine().quantizeSelectedNotes(gridTicks))
            return false;

        main->updateMidiClipTiming();
        main->repaint();
        return true;
    }

    bool keyStateChanged(bool, juce::Component*) override { return false; }

private:
    static MainComponent* findMainInTree(juce::Component* component) noexcept
    {
        if (component == nullptr) return nullptr;
        if (auto* main = dynamic_cast<MainComponent*>(component)) return main;
        for (int i = 0; i < component->getNumChildComponents(); ++i)
            if (auto* main = findMainInTree(component->getChildComponent(i))) return main;
        return nullptr;
    }

    static MainComponent* findMainComponent() noexcept
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* main = findMainInTree(desktop.getComponent(i))) return main;
        return nullptr;
    }

    static juce::DocumentWindow* findMidiWindow() noexcept
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i)))
                if (window->getName() == "Liberty - MIDI 1") return window;
        return nullptr;
    }

    void timerCallback() override
    {
        auto* window = findMidiWindow();
        auto* content = window != nullptr ? window->getContentComponent() : nullptr;
        if (content == attachedContent) return;
        detachFromContent();
        if (content != nullptr)
        {
            attachedContent = content;
            attachedContent->addKeyListener(this);
        }
    }

    void detachFromContent() noexcept
    {
        if (attachedContent != nullptr)
        {
            attachedContent->removeKeyListener(this);
            attachedContent = nullptr;
        }
    }

    juce::Component* attachedContent = nullptr;
};

MidiQuantizeKeyListener midiQuantizeKeyListener;
}
