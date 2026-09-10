#include "MainComponent.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
class MidiSelectAllKeyListener final : public juce::KeyListener
{
public:
    MidiSelectAllKeyListener()
    {
        juce::Desktop::getInstance().addGlobalKeyListener(this);
    }

    ~MidiSelectAllKeyListener() override
    {
        juce::Desktop::getInstance().removeGlobalKeyListener(this);
    }

    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override
    {
        if (!(key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
            || juce::CharacterFunctions::toLowerCase(key.getTextCharacter()) != 'a')
            return false;

        auto* main = findMainComponent(originatingComponent);
        if (main == nullptr)
            return false;

        auto selection = main->getMidiEngine().getNotesCopy();
        if (selection.empty())
            return false;

        main->getMidiEngine().setSelectedNotes(selection);
        main->repaint();
        return true;
    }

    bool keyStateChanged(bool, juce::Component*) override { return false; }

private:
    static MainComponent* findMainComponent(juce::Component* component) noexcept
    {
        while (component != nullptr)
        {
            if (auto* main = dynamic_cast<MainComponent*>(component))
                return main;
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(component))
                if (window->getName() == "Liberty - MIDI 1")
                    return findMainInTree(window->getContentComponent());
            component = component->getParentComponent();
        }
        return nullptr;
    }

    static MainComponent* findMainInTree(juce::Component* component) noexcept
    {
        if (component == nullptr)
            return nullptr;
        if (auto* main = dynamic_cast<MainComponent*>(component))
            return main;
        for (int i = 0; i < component->getNumChildComponents(); ++i)
            if (auto* main = findMainInTree(component->getChildComponent(i)))
                return main;
        return nullptr;
    }
};

MidiSelectAllKeyListener midiSelectAllKeyListener;
}
