#include "MainComponent.h"
#include "MidiEngine.h"

namespace
{
class MidiNoteSelectionInteraction final : public juce::KeyListener,
                                           public juce::MouseListener
{
public:
    MidiNoteSelectionInteraction()
    {
        juce::Desktop::getInstance().addGlobalMouseListener(this);
    }

    ~MidiNoteSelectionInteraction() override
    {
        detachFromWindows();
        juce::Desktop::getInstance().removeGlobalMouseListener(this);
    }

    bool keyPressed(const juce::KeyPress& key, juce::Component*) override
    {
        if (!isDeleteKey(key))
            return false;

        auto* main = findMainComponent();
        if (main == nullptr)
            return false;

        auto& midi = main->getMidiEngine();
        if (!midi.deleteSelectedNote())
            return false;

        main->updateMidiClipTiming();
        main->repaint();
        return true;
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        attachToMidiWindow(e.getScreenPosition());
    }

    void mouseUp(const juce::MouseEvent&) override {}

private:
    static bool isDeleteKey(const juce::KeyPress& key) noexcept
    {
        return key.getKeyCode() == juce::KeyPress::deleteKey
            || key.getKeyCode() == juce::KeyPress::backspaceKey;
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

    static MainComponent* findMainComponent() noexcept
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* main = findMainInTree(desktop.getComponent(i)))
                return main;
        return nullptr;
    }

    void attachToMidiWindow(juce::Point<int> screenPosition)
    {
        auto* component = juce::Desktop::getInstance().findComponentAt(screenPosition);
        while (component != nullptr)
        {
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(component))
            {
                if (window->getName() == "Liberty - MIDI 1")
                {
                    if (auto* content = window->getContentComponent())
                    {
                        if (attachedContent != content)
                        {
                            detachFromWindows();
                            attachedContent = content;
                            content->addKeyListener(this);
                        }
                        content->grabKeyboardFocus();
                    }
                    return;
                }
            }
            component = component->getParentComponent();
        }
    }

    void detachFromWindows()
    {
        if (attachedContent != nullptr)
            attachedContent->removeKeyListener(this);
        attachedContent = nullptr;
    }

    juce::Component* attachedContent = nullptr;
};

MidiNoteSelectionInteraction midiNoteSelectionInteraction;
}
