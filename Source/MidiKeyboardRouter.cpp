#include <juce_gui_basics/juce_gui_basics.h>

bool handleLibertyMidiNoteSelectionKeyPress(const juce::KeyPress& key);
bool handleLibertyMidiQuantizeKeyPress(const juce::KeyPress& key);

namespace
{
class MidiKeyboardRouter final : public juce::KeyListener, private juce::Timer
{
public:
    MidiKeyboardRouter()
    {
        startTimerHz(10);
    }

    ~MidiKeyboardRouter() override
    {
        detach();
    }

    bool keyPressed(const juce::KeyPress& key, juce::Component*) override
    {
        if (handleLibertyMidiNoteSelectionKeyPress(key))
            return true;
        if (handleLibertyMidiQuantizeKeyPress(key))
            return true;
        return false;
    }

private:
    static juce::DocumentWindow* findMidiWindow() noexcept
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
        {
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i)))
                if (window->getName() == "Liberty - MIDI 1")
                    return window;
        }
        return nullptr;
    }

    void attach(juce::DocumentWindow* window)
    {
        if (window == attachedWindow)
        {
            if (window != nullptr && window->isActiveWindow())
                if (auto* content = window->getContentComponent())
                    content->grabKeyboardFocus();
            return;
        }

        detach();
        if (window == nullptr)
            return;

        attachedWindow = window;
        attachedWindow->addKeyListener(this);
        attachedContent = attachedWindow->getContentComponent();
        if (attachedContent != nullptr)
        {
            attachedContent->addKeyListener(this);
            attachedContent->setWantsKeyboardFocus(true);
            if (attachedWindow->isActiveWindow())
                attachedContent->grabKeyboardFocus();
        }
    }

    void detach()
    {
        if (attachedContent != nullptr)
            attachedContent->removeKeyListener(this);
        if (attachedWindow != nullptr)
            attachedWindow->removeKeyListener(this);
        attachedContent = nullptr;
        attachedWindow = nullptr;
    }

    void timerCallback() override
    {
        attach(findMidiWindow());
    }

    juce::DocumentWindow* attachedWindow = nullptr;
    juce::Component* attachedContent = nullptr;
};

MidiKeyboardRouter midiKeyboardRouter;
}
