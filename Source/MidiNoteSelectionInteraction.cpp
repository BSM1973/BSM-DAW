#include "MainComponent.h"
#include "MidiEngine.h"
#include <cmath>
#include <memory>

namespace
{
constexpr int pianoKeyWidth = 72;
constexpr int rulerHeight = 30;
constexpr int keyHeight = 20;
constexpr int visibleKeys = 40;
constexpr int velocityLaneHeight = 92;
constexpr int lowestKey = 21;

class MidiSelectionOverlay final : public juce::Component,
                                   private juce::Timer
{
public:
    explicit MidiSelectionOverlay(MainComponent& o) : owner(o)
    {
        setInterceptsMouseClicks(true, true);
        setOpaque(false);
        startTimerHz(30);
    }

    bool hitTest(int x, int y) override
    {
        if (x < pianoKeyWidth || y < rulerHeight
            || y >= getHeight() - velocityLaneHeight)
            return false;

        const auto gridWidth = juce::jmax(1, getWidth() - pianoKeyWidth);
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(
            juce::jmax<std::int64_t>(1, ticksPerMeasure),
            MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        const auto pixelsPerTick = static_cast<double>(gridWidth - 2) / static_cast<double>(clipLengthTicks);
        const int pitch = juce::jlimit(0, 127, lowestKey + visibleKeys - 1 - ((y - rulerHeight) / keyHeight));

        for (const auto& note : owner.getMidiEngine().getNotesCopy())
        {
            if (note.pitch != pitch || note.startTick < 0 || note.startTick >= clipLengthTicks)
                continue;
            const auto visibleLengthTicks = juce::jmin(note.lengthTicks, clipLengthTicks - note.startTick);
            const int noteX = pianoKeyWidth + (int) std::llround((double) note.startTick * pixelsPerTick);
            const int noteY = rulerHeight + (visibleKeys - 1 - ((int) note.pitch - lowestKey)) * keyHeight + 2;
            const int noteW = juce::jmax(8, (int) std::llround((double) visibleLengthTicks * pixelsPerTick));
            const auto rect = juce::Rectangle<int>(noteX + 1, noteY, noteW - 2, keyHeight - 4);
            if (rect.contains(x, y))
                return false;
        }

        return true;
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        owner.getMidiEngine().clearNoteSelection();
        owner.repaint();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        if (!owner.getMidiEngine().hasSelectedNote())
            return;

        const auto note = owner.getMidiEngine().getSelectedNote();
        if (note.pitch < lowestKey || note.pitch >= lowestKey + visibleKeys)
            return;

        const auto gridWidth = juce::jmax(1, getWidth() - pianoKeyWidth);
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(
            juce::jmax<std::int64_t>(1, ticksPerMeasure),
            MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        const auto pixelsPerTick = static_cast<double>(gridWidth - 2) / static_cast<double>(clipLengthTicks);

        if (note.startTick < 0 || note.startTick >= clipLengthTicks)
            return;

        const auto visibleLengthTicks = juce::jmin(note.lengthTicks, clipLengthTicks - note.startTick);
        const int x = pianoKeyWidth + (int) std::llround((double) note.startTick * pixelsPerTick);
        const int y = rulerHeight + (visibleKeys - 1 - ((int) note.pitch - lowestKey)) * keyHeight + 2;
        const int w = juce::jmax(8, (int) std::llround((double) visibleLengthTicks * pixelsPerTick));
        auto r = juce::Rectangle<int>(x + 1, y, w - 2, keyHeight - 4);
        r = r.getIntersection(juce::Rectangle<int>(pianoKeyWidth, rulerHeight,
                                                     juce::jmax(1, getWidth() - pianoKeyWidth),
                                                     juce::jmax(1, getHeight() - rulerHeight - velocityLaneHeight)));
        if (r.isEmpty())
            return;

        g.setColour(juce::Colour(0xfff4f7fb));
        g.drawRoundedRectangle(r.toFloat().expanded(1.0f), 3.0f, 2.0f);

        const int handleWidth = juce::jmin(5, r.getWidth());
        const int handleHeight = juce::jmin(5, r.getHeight());
        g.fillRect(r.getX(), r.getY(), handleWidth, handleHeight);
        g.fillRect(r.getRight() - handleWidth, r.getY(), handleWidth, handleHeight);
        g.fillRect(r.getX(), r.getBottom() - handleHeight, handleWidth, handleHeight);
        g.fillRect(r.getRight() - handleWidth, r.getBottom() - handleHeight, handleWidth, handleHeight);
    }

private:
    void timerCallback() override
    {
        if (auto* parent = getParentComponent())
            setBounds(parent->getLocalBounds());
        repaint();
    }

    MainComponent& owner;
};

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
        shutdown();
    }

    void shutdown() noexcept
    {
        detachFromWindows();
        if (registered)
        {
            juce::Desktop::getInstance().removeGlobalMouseListener(this);
            registered = false;
        }
    }

    bool keyPressed(const juce::KeyPress& key, juce::Component*) override
    {
        auto* main = findMainComponent();
        if (main == nullptr)
            return false;

        auto& midi = main->getMidiEngine();
        const auto modifiers = key.getModifiers();
        const bool command = modifiers.isCommandDown();

        if (command && key.getKeyCode() == 'c')
        {
            if (!midi.hasSelectedNote())
                return false;
            clipboardNote = midi.getSelectedNote();
            return true;
        }

        if (command && key.getKeyCode() == 'v')
        {
            if (!clipboardNote.has_value())
                return false;

            auto pasted = *clipboardNote;
            const auto pasteStart = midi.hasSelectedNote()
                ? midi.getSelectedNote().startTick + midi.getSelectedNote().lengthTicks
                : pasted.startTick;
            pasted.startTick = MidiEngine::quantizeTick(pasteStart, MidiEngine::ticksPerQuarterNote / 4);

            if (!midi.addNote(pasted.startTick, pasted.lengthTicks, pasted.pitch, pasted.velocity, pasted.channel))
                return false;

            main->updateMidiClipTiming();
            main->repaint();
            if (selectionOverlay != nullptr)
                selectionOverlay->repaint();
            return true;
        }

        if (command && key.getKeyCode() == 'd')
        {
            if (!midi.hasSelectedNote())
                return false;

            const auto source = midi.getSelectedNote();
            const auto duplicateStart = source.startTick + source.lengthTicks;
            if (!midi.addNote(duplicateStart, source.lengthTicks, source.pitch, source.velocity, source.channel))
                return false;

            main->updateMidiClipTiming();
            main->repaint();
            if (selectionOverlay != nullptr)
                selectionOverlay->repaint();
            return true;
        }

        if (!isDeleteKey(key))
            return false;

        if (!midi.deleteSelectedNote())
            return false;

        main->updateMidiClipTiming();
        main->repaint();
        if (selectionOverlay != nullptr)
            selectionOverlay->repaint();
        return true;
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        attachToMidiWindow(e.getScreenPosition());
        selectNoteFromScreenPosition(e.getScreenPosition());
    }

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

    static juce::DocumentWindow* findMidiWindow(juce::Point<int> screenPosition) noexcept
    {
        auto* component = juce::Desktop::getInstance().findComponentAt(screenPosition);
        while (component != nullptr)
        {
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(component))
                if (window->getName() == "Liberty - MIDI 1")
                    return window;
            component = component->getParentComponent();
        }
        return nullptr;
    }

    void attachToMidiWindow(juce::Point<int> screenPosition)
    {
        auto* window = findMidiWindow(screenPosition);
        if (window == nullptr)
            return;

        if (auto* content = window->getContentComponent())
        {
            if (attachedContent != content)
            {
                detachFromWindows();
                attachedContent = content;
                content->addKeyListener(this);
                if (auto* main = findMainComponent())
                {
                    selectionOverlay = std::make_unique<MidiSelectionOverlay>(*main);
                    content->addAndMakeVisible(selectionOverlay.get());
                    selectionOverlay->setBounds(content->getLocalBounds());
                }
            }
            content->grabKeyboardFocus();
        }
    }

    void selectNoteFromScreenPosition(juce::Point<int> screenPosition)
    {
        auto* window = findMidiWindow(screenPosition);
        if (window == nullptr)
            return;

        auto* content = window->getContentComponent();
        auto* main = findMainComponent();
        if (content == nullptr || main == nullptr)
            return;

        const auto p = screenPosition - content->getScreenPosition();
        if (p.x < pianoKeyWidth || p.y < rulerHeight
            || p.y >= content->getHeight() - velocityLaneHeight)
            return;

        const auto gridWidth = juce::jmax(1, content->getWidth() - pianoKeyWidth);
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(main->getTimeSignatureNumerator(), main->getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(
            juce::jmax<std::int64_t>(1, ticksPerMeasure),
            MidiEngine::secondsToTick(main->getMidiClipLengthSeconds(), main->getTempoBpm()));
        const auto pixelsPerTick = static_cast<double>(gridWidth - 2) / static_cast<double>(clipLengthTicks);
        const int pitch = juce::jlimit(0, 127, lowestKey + visibleKeys - 1 - ((p.y - rulerHeight) / keyHeight));

        for (const auto& note : main->getMidiEngine().getNotesCopy())
        {
            if (note.pitch != pitch || note.startTick < 0 || note.startTick >= clipLengthTicks)
                continue;

            const auto visibleLengthTicks = juce::jmin(note.lengthTicks, clipLengthTicks - note.startTick);
            const int x = pianoKeyWidth + (int) std::llround((double) note.startTick * pixelsPerTick);
            const int y = rulerHeight + (visibleKeys - 1 - ((int) note.pitch - lowestKey)) * keyHeight + 2;
            const int w = juce::jmax(8, (int) std::llround((double) visibleLengthTicks * pixelsPerTick));
            const auto rect = juce::Rectangle<int>(x + 1, y, w - 2, keyHeight - 4);
            if (rect.contains(p.x, p.y))
            {
                main->getMidiEngine().selectNoteAt(note.startTick, note.pitch, note.channel);
                if (selectionOverlay != nullptr)
                    selectionOverlay->repaint();
                return;
            }
        }

        main->getMidiEngine().clearNoteSelection();
        if (selectionOverlay != nullptr)
            selectionOverlay->repaint();
    }

    void detachFromWindows()
    {
        if (attachedContent != nullptr)
            attachedContent->removeKeyListener(this);
        if (selectionOverlay != nullptr && attachedContent != nullptr)
            attachedContent->removeChildComponent(selectionOverlay.get());
        selectionOverlay.reset();
        attachedContent = nullptr;
    }

    bool registered = true;
    juce::Component* attachedContent = nullptr;
    std::unique_ptr<MidiSelectionOverlay> selectionOverlay;
    std::optional<MidiEngine::NoteEvent> clipboardNote;
};

MidiNoteSelectionInteraction midiNoteSelectionInteraction;
}

void shutdownLibertyMidiNoteSelectionInteraction()
{
    midiNoteSelectionInteraction.shutdown();
}
