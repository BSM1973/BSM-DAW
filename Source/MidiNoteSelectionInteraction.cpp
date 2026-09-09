#include "MainComponent.h"
#include "MidiEngine.h"
#include <cmath>
#include <memory>
#include <optional>
#include <vector>

namespace
{
constexpr int pianoKeyWidth = 72;
constexpr int rulerHeight = 30;
constexpr int keyHeight = 20;
constexpr int visibleKeys = 40;
constexpr int velocityLaneHeight = 92;
constexpr int lowestKey = 21;
constexpr std::int64_t gridTicks = MidiEngine::ticksPerQuarterNote / 4;

class MidiSelectionOverlay final : public juce::Component,
                                   private juce::Timer
{
public:
    explicit MidiSelectionOverlay(MainComponent& o) : owner(o)
    {
        setInterceptsMouseClicks(true, false);
        setOpaque(false);
        startTimerHz(30);
    }

    void paint(juce::Graphics& g) override
    {
        const auto selectedNotes = owner.getMidiEngine().getSelectedNotesCopy();
        if (selectedNotes.empty())
            return;

        const auto gridWidth = juce::jmax(1, getWidth() - pianoKeyWidth);
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(
            juce::jmax<std::int64_t>(1, ticksPerMeasure),
            MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        const auto pixelsPerTick = static_cast<double>(gridWidth - 2) / static_cast<double>(clipLengthTicks);

        for (const auto& note : selectedNotes)
        {
            if (note.pitch < lowestKey || note.pitch >= lowestKey + visibleKeys
                || note.startTick < 0 || note.startTick >= clipLengthTicks)
                continue;

            const auto visibleLengthTicks = juce::jmin(note.lengthTicks, clipLengthTicks - note.startTick);
            const int x = pianoKeyWidth + (int) std::llround((double) note.startTick * pixelsPerTick);
            const int y = rulerHeight + (visibleKeys - 1 - ((int) note.pitch - lowestKey)) * keyHeight + 2;
            const int w = juce::jmax(8, (int) std::llround((double) visibleLengthTicks * pixelsPerTick));
            auto r = juce::Rectangle<int>(x + 1, y, w - 2, keyHeight - 4);
            r = r.getIntersection(juce::Rectangle<int>(pianoKeyWidth, rulerHeight,
                                                        juce::jmax(1, getWidth() - pianoKeyWidth),
                                                        juce::jmax(1, getHeight() - rulerHeight - velocityLaneHeight)));
            if (r.isEmpty())
                continue;

            g.setColour(juce::Colour(0xfff4f7fb));
            g.drawRoundedRectangle(r.toFloat().expanded(1.0f), 3.0f, 2.0f);
            const int handleWidth = juce::jmin(5, r.getWidth());
            const int handleHeight = juce::jmin(5, r.getHeight());
            g.fillRect(r.getX(), r.getY(), handleWidth, handleHeight);
            g.fillRect(r.getRight() - handleWidth, r.getY(), handleWidth, handleHeight);
            g.fillRect(r.getX(), r.getBottom() - handleHeight, handleWidth, handleHeight);
            g.fillRect(r.getRight() - handleWidth, r.getBottom() - handleHeight, handleWidth, handleHeight);
        }
    }

    bool hitTest(int x, int y) override
    {
        if (x < pianoKeyWidth || y < rulerHeight || y >= getHeight() - velocityLaneHeight)
            return false;
        return !pointHitsExistingNote(x, y);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!e.mods.isLeftButtonDown())
            return;

        if (auto* parent = getParentComponent())
            parent->grabKeyboardFocus();

        owner.getMidiEngine().clearNoteSelection();
        repaint();
        owner.repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (!e.mods.isLeftButtonDown())
            return;

        const int pitch = pitchFromY(e.y);
        const auto tick = tickFromX(e.x);
        if (owner.getMidiEngine().addNote(tick, MidiEngine::ticksPerQuarterNote, pitch, 100, 1))
        {
            owner.updateMidiClipTiming();
            repaint();
            owner.repaint();
        }
    }

private:
    bool pointHitsExistingNote(int x, int y) const
    {
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
            if (juce::Rectangle<int>(noteX + 1, noteY, noteW - 2, keyHeight - 4).contains(x, y))
                return true;
        }
        return false;
    }

    int pitchFromY(int y) const noexcept
    {
        const int row = juce::jlimit(0, visibleKeys - 1, (y - rulerHeight) / keyHeight);
        return lowestKey + visibleKeys - 1 - row;
    }

    std::int64_t tickFromX(int x) const noexcept
    {
        const auto gridWidth = juce::jmax(1, getWidth() - pianoKeyWidth);
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(
            juce::jmax<std::int64_t>(1, ticksPerMeasure),
            MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        const auto pixelsPerTick = static_cast<double>(gridWidth - 2) / static_cast<double>(clipLengthTicks);
        const auto raw = (std::int64_t)std::llround((x - pianoKeyWidth) / pixelsPerTick);
        return MidiEngine::quantizeTick(juce::jmax<std::int64_t>(0, raw), gridTicks);
    }

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
    MidiNoteSelectionInteraction() { juce::Desktop::getInstance().addGlobalMouseListener(this); }
    ~MidiNoteSelectionInteraction() override { shutdown(); }

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
        return handleKeyPress(key);
    }

    bool handleKeyPress(const juce::KeyPress& key)
    {
        auto* main = findMainComponent();
        if (main == nullptr || attachedContent == nullptr)
            return false;

        const auto modifiers = key.getModifiers();
        const bool command = modifiers.isCommandDown();
        const int keyCode = key.getKeyCode();
        const auto isKey = [keyCode](int lower, int upper) noexcept { return keyCode == lower || keyCode == upper; };

        if (command && isKey('c', 'C'))
        {
            if (!midiHasSelectedNote(*main)) return false;
            clipboardNote = main->getMidiEngine().getSelectedNote();
            return true;
        }
        if (command && isKey('v', 'V'))
        {
            if (!clipboardNote.has_value()) return false;
            auto& midi = main->getMidiEngine(); auto pasted = *clipboardNote;
            const auto pasteStart = midi.hasSelectedNote() ? midi.getSelectedNote().startTick + midi.getSelectedNote().lengthTicks : pasted.startTick;
            pasted.startTick = MidiEngine::quantizeTick(pasteStart, gridTicks);
            if (!midi.addNote(pasted.startTick, pasted.lengthTicks, pasted.pitch, pasted.velocity, pasted.channel)) return false;
            main->updateMidiClipTiming(); main->repaint(); if (selectionOverlay != nullptr) selectionOverlay->repaint(); return true;
        }
        if (command && isKey('d', 'D'))
        {
            auto& midi = main->getMidiEngine(); if (!midi.hasSelectedNote()) return false; const auto source = midi.getSelectedNote();
            if (!midi.addNote(source.startTick + source.lengthTicks, source.lengthTicks, source.pitch, source.velocity, source.channel)) return false;
            main->updateMidiClipTiming(); main->repaint(); if (selectionOverlay != nullptr) selectionOverlay->repaint(); return true;
        }
        if (isDeleteKey(key))
        {
            auto& midi = main->getMidiEngine(); if (!midi.deleteSelectedNote()) return false;
            main->updateMidiClipTiming(); main->repaint(); if (selectionOverlay != nullptr) selectionOverlay->repaint(); return true;
        }
        return false;
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        attachToMidiWindow(e.getScreenPosition());
        pendingAdditiveSelection = false;
        if (e.mods.isCommandDown())
        {
            pendingAdditiveSelection = selectNoteFromScreenPosition(e.getScreenPosition(), true);
        }
        else
        {
            selectNoteFromScreenPosition(e.getScreenPosition(), false);
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (!pendingAdditiveSelection)
            return;
        if (auto* main = findMainComponent())
        {
            main->getMidiEngine().setSelectedNotes(pendingSelection);
            main->repaint();
            if (selectionOverlay != nullptr) selectionOverlay->repaint();
        }
        pendingAdditiveSelection = false;
        pendingSelection.clear();
    }

private:
    static bool midiHasSelectedNote(MainComponent& main) noexcept { return main.getMidiEngine().hasSelectedNote(); }
    static bool isDeleteKey(const juce::KeyPress& key) noexcept { return key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey; }

    static MainComponent* findMainInTree(juce::Component* component) noexcept
    {
        if (component == nullptr) return nullptr;
        if (auto* main = dynamic_cast<MainComponent*>(component)) return main;
        for (int i = 0; i < component->getNumChildComponents(); ++i) if (auto* main = findMainInTree(component->getChildComponent(i))) return main;
        return nullptr;
    }

    static MainComponent* findMainComponent() noexcept
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i) if (auto* main = findMainInTree(desktop.getComponent(i))) return main;
        return nullptr;
    }

    static juce::DocumentWindow* findMidiWindow(juce::Point<int> screenPosition) noexcept
    {
        auto* component = juce::Desktop::getInstance().findComponentAt(screenPosition);
        while (component != nullptr)
        {
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(component)) if (window->getName() == "Liberty - MIDI 1") return window;
            component = component->getParentComponent();
        }
        return nullptr;
    }

    void attachToMidiWindow(juce::Point<int> screenPosition)
    {
        auto* window = findMidiWindow(screenPosition); if (window == nullptr) return;
        if (auto* content = window->getContentComponent())
        {
            if (attachedContent != content)
            {
                detachFromWindows(); attachedContent = content; content->addKeyListener(this);
                if (auto* main = findMainComponent()) { selectionOverlay = std::make_unique<MidiSelectionOverlay>(*main); content->addAndMakeVisible(selectionOverlay.get()); selectionOverlay->setBounds(content->getLocalBounds()); }
            }
            content->grabKeyboardFocus();
        }
    }

    bool selectNoteFromScreenPosition(juce::Point<int> screenPosition, bool additive)
    {
        auto* window = findMidiWindow(screenPosition); if (window == nullptr) return false;
        auto* content = window->getContentComponent(); auto* main = findMainComponent(); if (content == nullptr || main == nullptr) return false;
        const auto p = screenPosition - content->getScreenPosition();
        if (p.x < pianoKeyWidth || p.y < rulerHeight || p.y >= content->getHeight() - velocityLaneHeight) return false;
        const auto gridWidth = juce::jmax(1, content->getWidth() - pianoKeyWidth);
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(main->getTimeSignatureNumerator(), main->getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(juce::jmax<std::int64_t>(1, ticksPerMeasure), MidiEngine::secondsToTick(main->getMidiClipLengthSeconds(), main->getTempoBpm()));
        const auto pixelsPerTick = static_cast<double>(gridWidth - 2) / static_cast<double>(clipLengthTicks);
        const int pitch = juce::jlimit(0, 127, lowestKey + visibleKeys - 1 - ((p.y - rulerHeight) / keyHeight));

        for (const auto& note : main->getMidiEngine().getNotesCopy())
        {
            if (note.pitch != pitch || note.startTick < 0 || note.startTick >= clipLengthTicks) continue;
            const auto visibleLengthTicks = juce::jmin(note.lengthTicks, clipLengthTicks - note.startTick);
            const int x = pianoKeyWidth + (int)std::llround((double)note.startTick * pixelsPerTick);
            const int y = rulerHeight + (visibleKeys - 1 - ((int)note.pitch - lowestKey)) * keyHeight + 2;
            const int w = juce::jmax(8, (int)std::llround((double)visibleLengthTicks * pixelsPerTick));
            if (juce::Rectangle<int>(x + 1, y, w - 2, keyHeight - 4).contains(p.x, p.y))
            {
                if (additive)
                {
                    pendingSelection = main->getMidiEngine().getSelectedNotesCopy();
                    main->getMidiEngine().toggleNoteSelectionAt(note.startTick, note.pitch, note.channel);
                    pendingSelection = main->getMidiEngine().getSelectedNotesCopy();
                }
                else
                {
                    main->getMidiEngine().selectNoteAt(note.startTick, note.pitch, note.channel);
                }
                if (selectionOverlay != nullptr) selectionOverlay->repaint();
                main->repaint();
                return true;
            }
        }

        main->getMidiEngine().clearNoteSelection();
        if (selectionOverlay != nullptr) selectionOverlay->repaint();
        main->repaint();
        return false;
    }

    void detachFromWindows()
    {
        if (attachedContent != nullptr) attachedContent->removeKeyListener(this);
        selectionOverlay.reset(); attachedContent = nullptr;
    }

    bool registered = true;
    juce::Component* attachedContent = nullptr;
    std::unique_ptr<MidiSelectionOverlay> selectionOverlay;
    std::optional<MidiEngine::NoteEvent> clipboardNote;
    std::vector<MidiEngine::NoteEvent> pendingSelection;
    bool pendingAdditiveSelection = false;
};

MidiNoteSelectionInteraction midiNoteSelectionInteraction;
}

bool handleLibertyMidiNoteSelectionKeyPress(const juce::KeyPress& key) { return midiNoteSelectionInteraction.handleKeyPress(key); }
void shutdownLibertyMidiNoteSelectionInteraction() { midiNoteSelectionInteraction.shutdown(); }
