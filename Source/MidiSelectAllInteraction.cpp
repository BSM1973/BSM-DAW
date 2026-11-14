#include "MainComponent.h"
#include "MidiEngine.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
class MidiSelectAllKeyListener final : private juce::Timer, public juce::KeyListener
{
public:
    MidiSelectAllKeyListener() { startTimerHz(10); }
    ~MidiSelectAllKeyListener() override { stopTimer(); detachFromContent(); }

    bool keyPressed(const juce::KeyPress& key, juce::Component*) override
    {
        auto* main = findMainComponent();
        if (main == nullptr) return false;

        const auto modifiers = key.getModifiers();
        const bool commandOrCtrl = modifiers.isCommandDown() || modifiers.isCtrlDown();
        const int keyCode = key.getKeyCode();

        if (commandOrCtrl && keyCode == 'A')
        {
            auto notes = main->getMidiEngine().getNotesCopy();
            if (notes.empty()) return false;
            main->getMidiEngine().setSelectedNotes(notes);
            main->repaint();
            return true;
        }

        if (keyCode == juce::KeyPress::escapeKey)
        {
            if (main->getMidiEngine().getNumSelectedNotes() == 0) return false;
            main->getMidiEngine().clearNoteSelection();
            main->repaint();
            return true;
        }

        if (main->getMidiEngine().getNumSelectedNotes() == 0) return false;

        constexpr std::int64_t grid = MidiEngine::ticksPerQuarterNote / 4;
        std::int64_t deltaTicks = 0;
        int deltaPitch = 0;

        if (keyCode == juce::KeyPress::leftKey) deltaTicks = -grid;
        else if (keyCode == juce::KeyPress::rightKey) deltaTicks = grid;
        else if (keyCode == juce::KeyPress::upKey) deltaPitch = 1;
        else if (keyCode == juce::KeyPress::downKey) deltaPitch = -1;
        else return false;

        if (main->getMidiEngine().moveSelectedNotesBy(deltaTicks, deltaPitch))
        {
            main->updateMidiClipTiming();
            main->repaint();
            return true;
        }
        return false;
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

MidiSelectAllKeyListener midiSelectAllKeyListener;
}
