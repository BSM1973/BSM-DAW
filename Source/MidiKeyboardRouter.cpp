#include "MainComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>

bool handleLibertyMidiNoteSelectionKeyPress(const juce::KeyPress& key);
bool handleLibertyMidiQuantizeKeyPress(const juce::KeyPress& key);
bool handleLibertyMidiUndoRedoKeyPress(const juce::KeyPress& key);

class MidiKeyboardRouter final : private juce::Timer, private juce::KeyListener
{
public:
    MidiKeyboardRouter() { startTimerHz(30); }
    ~MidiKeyboardRouter() override { detach(); }

private:
    void timerCallback() override
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i)))
                if (window->getName() == "Liberty - MIDI 1")
                    attachRecursive(window->getContentComponent());
    }

    void attachRecursive(juce::Component* component)
    {
        if (component == nullptr) return;
        if (attached.insert(component).second)
            component->addKeyListener(this);
        for (int i = 0; i < component->getNumChildComponents(); ++i)
            attachRecursive(component->getChildComponent(i));
    }

    void detach()
    {
        for (auto* component : attached)
            if (component != nullptr) component->removeKeyListener(this);
        attached.clear();
    }

    bool keyPressed(const juce::KeyPress& key, juce::Component*) override
    {
        if (handleLibertyMidiUndoRedoKeyPress(key)) return true;
        if (handleLibertyMidiNoteSelectionKeyPress(key)) return true;
        if (handleLibertyMidiQuantizeKeyPress(key)) return true;
        return false;
    }

    std::set<juce::Component*> attached;
};

MidiKeyboardRouter globalMidiKeyboardRouter;
