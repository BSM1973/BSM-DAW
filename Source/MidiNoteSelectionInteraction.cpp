#include "MainComponent.h"
#include "MidiEngine.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace
{
constexpr int pianoKeyWidth = 72;
constexpr int rulerHeight = 30;
constexpr int keyHeight = 20;
constexpr int visibleKeys = 40;
constexpr int velocityLaneHeight = 92;
constexpr int lowestKey = 21;
constexpr int marqueeThreshold = 4;
constexpr std::int64_t gridTicks = MidiEngine::ticksPerQuarterNote / 4;

class MidiSelectionOverlay final : public juce::Component, private juce::Timer
{
public:
    explicit MidiSelectionOverlay(MainComponent& o) : owner(o)
    {
        setInterceptsMouseClicks(false, false);
        setOpaque(false);
        startTimerHz(30);
    }

    void setMarquee(const juce::Rectangle<int>& r, bool active)
    {
        marqueeBounds = r;
        marqueeActive = active;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto selectedNotes = owner.getMidiEngine().getSelectedNotesCopy();
        const auto gridWidth = juce::jmax(1, getWidth() - pianoKeyWidth);
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(juce::jmax<std::int64_t>(1, ticksPerMeasure), MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        const auto pixelsPerTick = static_cast<double>(gridWidth - 2) / static_cast<double>(clipLengthTicks);
        for (const auto& note : selectedNotes)
        {
            if (note.pitch < lowestKey || note.pitch >= lowestKey + visibleKeys || note.startTick < 0 || note.startTick >= clipLengthTicks) continue;
            const auto visibleLengthTicks = juce::jmin(note.lengthTicks, clipLengthTicks - note.startTick);
            const int x = pianoKeyWidth + (int)std::llround((double)note.startTick * pixelsPerTick);
            const int y = rulerHeight + (visibleKeys - 1 - ((int)note.pitch - lowestKey)) * keyHeight + 2;
            const int w = juce::jmax(8, (int)std::llround((double)visibleLengthTicks * pixelsPerTick));
            auto r = juce::Rectangle<int>(x + 1, y, w - 2, keyHeight - 4);
            r = r.getIntersection(juce::Rectangle<int>(pianoKeyWidth, rulerHeight, juce::jmax(1, getWidth() - pianoKeyWidth), juce::jmax(1, getHeight() - rulerHeight - velocityLaneHeight)));
            if (r.isEmpty()) continue;
            g.setColour(juce::Colour(0xfff4f7fb));
            g.drawRoundedRectangle(r.toFloat().expanded(1.0f), 3.0f, 2.0f);
            const int hw = juce::jmin(5, r.getWidth()), hh = juce::jmin(5, r.getHeight());
            g.fillRect(r.getX(), r.getY(), hw, hh); g.fillRect(r.getRight() - hw, r.getY(), hw, hh);
            g.fillRect(r.getX(), r.getBottom() - hh, hw, hh); g.fillRect(r.getRight() - hw, r.getBottom() - hh, hw, hh);
        }
        if (marqueeActive)
        {
            g.setColour(juce::Colour(0x332f80ed));
            g.fillRect(marqueeBounds);
            g.setColour(juce::Colour(0xff8fb7ff));
            g.drawRect(marqueeBounds, 1);
        }
    }

    bool hitTest(int, int) override { return false; }

    void timerCallback() override
    {
        if (auto* parent = getParentComponent())
        {
            setBounds(parent->getLocalBounds());
            repaint();
        }
    }

private:
    MainComponent& owner;
    juce::Rectangle<int> marqueeBounds;
    bool marqueeActive = false;
};

class MidiNoteSelectionInteraction final : public juce::KeyListener, public juce::MouseListener
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

    bool keyPressed(const juce::KeyPress& key, juce::Component*) override { return handleKeyPress(key); }

    bool handleKeyPress(const juce::KeyPress& key)
    {
        auto* main = findMainComponent();
        if (main == nullptr || attachedContent == nullptr) return false;
        auto& midi = main->getMidiEngine();
        const auto modifiers = key.getModifiers();
        const bool command = modifiers.isCommandDown();
        const bool shift = modifiers.isShiftDown();
        const int keyCode = key.getKeyCode();
        const auto isKey = [keyCode](int lower, int upper) noexcept { return keyCode == lower || keyCode == upper; };
        bool changed = false;

        if (command && isKey('z', 'Z')) changed = shift ? midi.redo() : midi.undo();
        else if (command && isKey('y', 'Y')) changed = midi.redo();
        else if (command && isKey('c', 'C')) { clipboardNotes = midi.getSelectedNotesCopy(); return !clipboardNotes.empty(); }
        else if (command && isKey('v', 'V'))
        {
            if (clipboardNotes.empty()) return false;
            const auto selected = midi.getSelectedNotesCopy();
            std::int64_t offset = gridTicks;
            if (!selected.empty())
            {
                std::int64_t end = 0;
                for (const auto& n : selected) end = std::max(end, n.startTick + n.lengthTicks);
                offset = end - selected.front().startTick;
                if (offset <= 0) offset = gridTicks;
            }
            std::vector<MidiEngine::NoteEvent> pasted;
            for (const auto& n : clipboardNotes) { auto copy = n; copy.startTick += offset; pasted.push_back(copy); }
            bool ok = true;
            for (const auto& n : pasted) if (!midi.addNote(n.startTick, n.lengthTicks, n.pitch, n.velocity, n.channel)) ok = false;
            if (!ok) return false;
            midi.setSelectedNotes(pasted); changed = true;
        }
        else if (command && isKey('d', 'D')) changed = midi.duplicateSelectedNotes();
        else if (isDeleteKey(key)) changed = midi.getNumSelectedNotes() > 1 ? midi.deleteSelectedNotes() : midi.deleteSelectedNote();
        else if (!command && (keyCode == juce::KeyPress::leftKey || keyCode == juce::KeyPress::rightKey || keyCode == juce::KeyPress::upKey || keyCode == juce::KeyPress::downKey))
        {
            const auto deltaTicks = keyCode == juce::KeyPress::leftKey ? -gridTicks : keyCode == juce::KeyPress::rightKey ? gridTicks : 0;
            const int deltaPitch = keyCode == juce::KeyPress::upKey ? 1 : keyCode == juce::KeyPress::downKey ? -1 : 0;
            changed = midi.moveSelectedNotesBy(deltaTicks, deltaPitch);
        }

        if (changed)
        {
            main->updateMidiClipTiming();
            main->repaint();
            if (selectionOverlay != nullptr) selectionOverlay->repaint();
            return true;
        }
        return false;
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        attachToMidiWindow(e.getScreenPosition());
        marqueeCandidate = false;
        marqueeActive = false;
        if (selectionOverlay != nullptr) selectionOverlay->setMarquee({}, false);

        if (e.mods.isCommandDown())
        {
            auto* main = findMainComponent();
            if (main != nullptr)
            {
                if (!pendingAdditiveSelectionActive) pendingAdditiveSelection = main->getMidiEngine().getSelectedNotesCopy();
                const auto before = pendingAdditiveSelection.size();
                selectNoteFromScreenPosition(e.getScreenPosition(), true, &pendingAdditiveSelection);
                pendingAdditiveSelectionActive = true;
                if (pendingAdditiveSelection.size() != before || !pendingAdditiveSelection.empty())
                    juce::Timer::callAfterDelay(100, [this]
                    {
                        if (!pendingAdditiveSelectionActive || marqueeActive) return;
                        if (auto* currentMain = findMainComponent())
                        {
                            currentMain->getMidiEngine().setSelectedNotes(pendingAdditiveSelection);
                            currentMain->repaint();
                            if (selectionOverlay != nullptr) selectionOverlay->repaint();
                        }
                        pendingAdditiveSelection.clear();
                        pendingAdditiveSelectionActive = false;
                    });
            }
        }
        else
        {
            pendingAdditiveSelection.clear();
            pendingAdditiveSelectionActive = false;
        }

        auto* main = findMainComponent();
        if (main == nullptr) return;
        if (!isInPianoRollGrid(e.getScreenPosition(), *main)) return;
        if (pointHitsAnyNote(e.getScreenPosition(), *main)) return;

        marqueeCandidate = true;
        marqueeStartScreen = e.getScreenPosition();
        marqueeCurrentScreen = marqueeStartScreen;
        marqueeAdditive = e.mods.isCommandDown();
        marqueeBaseSelection = marqueeAdditive ? main->getMidiEngine().getSelectedNotesCopy() : std::vector<MidiEngine::NoteEvent>{};
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!marqueeCandidate || !e.mods.isLeftButtonDown()) return;
        marqueeCurrentScreen = e.getScreenPosition();
        if (!marqueeActive && marqueeStartScreen.getDistanceFrom(marqueeCurrentScreen) >= marqueeThreshold)
        {
            marqueeActive = true;
            pendingAdditiveSelection.clear();
            pendingAdditiveSelectionActive = false;
            if (!marqueeAdditive)
            {
                if (auto* main = findMainComponent()) main->getMidiEngine().clearNoteSelection();
            }
        }
        if (marqueeActive) updateMarqueeVisual();
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (!marqueeCandidate) return;
        marqueeCurrentScreen = e.getScreenPosition();
        if (marqueeActive)
        {
            applyMarqueeSelection();
            marqueeActive = false;
            if (selectionOverlay != nullptr) selectionOverlay->setMarquee({}, false);
            if (auto* main = findMainComponent()) main->repaint();
        }
        marqueeCandidate = false;
        marqueeBaseSelection.clear();
    }

private:
    static bool isDeleteKey(const juce::KeyPress& key) noexcept { return key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey; }
    static bool sameNote(const MidiEngine::NoteEvent& a, const MidiEngine::NoteEvent& b) noexcept { return a.startTick == b.startTick && a.pitch == b.pitch && a.channel == b.channel; }

    static MainComponent* findMainInTree(juce::Component* c) noexcept
    {
        if (c == nullptr) return nullptr;
        if (auto* main = dynamic_cast<MainComponent*>(c)) return main;
        for (int i = 0; i < c->getNumChildComponents(); ++i) if (auto* main = findMainInTree(c->getChildComponent(i))) return main;
        return nullptr;
    }

    static MainComponent* findMainComponent() noexcept
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i) if (auto* main = findMainInTree(desktop.getComponent(i))) return main;
        return nullptr;
    }

    static juce::DocumentWindow* findMidiWindow(juce::Point<int> p) noexcept
    {
        auto* c = juce::Desktop::getInstance().findComponentAt(p);
        while (c != nullptr)
        {
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(c)) if (window->getName() == "Liberty - MIDI 1") return window;
            c = c->getParentComponent();
        }
        return nullptr;
    }

    static bool isInPianoRollGrid(juce::Point<int> screenPosition, MainComponent& main)
    {
        auto* window = findMidiWindow(screenPosition);
        if (window == nullptr) return false;
        auto* content = window->getContentComponent();
        if (content == nullptr) return false;
        const auto p = screenPosition - content->getScreenPosition();
        return p.x >= pianoKeyWidth && p.y >= rulerHeight && p.y < content->getHeight() - velocityLaneHeight;
    }

    static bool pointHitsAnyNote(juce::Point<int> screenPosition, MainComponent& main)
    {
        auto* window = findMidiWindow(screenPosition);
        if (window == nullptr) return false;
        auto* content = window->getContentComponent();
        if (content == nullptr) return false;
        const auto p = screenPosition - content->getScreenPosition();
        const auto gridWidth = juce::jmax(1, content->getWidth() - pianoKeyWidth);
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(main.getTimeSignatureNumerator(), main.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(juce::jmax<std::int64_t>(1, ticksPerMeasure), MidiEngine::secondsToTick(main.getMidiClipLengthSeconds(), main.getTempoBpm()));
        const auto pixelsPerTick = static_cast<double>(gridWidth - 2) / static_cast<double>(clipLengthTicks);
        const int pitch = juce::jlimit(0, 127, lowestKey + visibleKeys - 1 - ((p.y - rulerHeight) / keyHeight));
        for (const auto& note : main.getMidiEngine().getNotesCopy())
        {
            if (note.pitch != pitch || note.startTick < 0 || note.startTick >= clipLengthTicks) continue;
            const auto visibleLengthTicks = juce::jmin(note.lengthTicks, clipLengthTicks - note.startTick);
            const int x = pianoKeyWidth + (int)std::llround((double)note.startTick * pixelsPerTick);
            const int y = rulerHeight + (visibleKeys - 1 - ((int)note.pitch - lowestKey)) * keyHeight + 2;
            const int w = juce::jmax(8, (int)std::llround((double)visibleLengthTicks * pixelsPerTick));
            if (juce::Rectangle<int>(x + 1, y, w - 2, keyHeight - 4).contains(p.x, p.y)) return true;
        }
        return false;
    }

    static bool pointHitsSelectedNote(juce::Point<int> screenPosition, MainComponent& main)
    {
        auto* window = findMidiWindow(screenPosition); if (window == nullptr) return false;
        auto* content = window->getContentComponent(); if (content == nullptr) return false;
        const auto p = screenPosition - content->getScreenPosition();
        if (p.x < pianoKeyWidth || p.y < rulerHeight || p.y >= content->getHeight() - velocityLaneHeight) return false;
        const auto gridWidth = juce::jmax(1, content->getWidth() - pianoKeyWidth);
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(main.getTimeSignatureNumerator(), main.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(juce::jmax<std::int64_t>(1, ticksPerMeasure), MidiEngine::secondsToTick(main.getMidiClipLengthSeconds(), main.getTempoBpm()));
        const auto pixelsPerTick = static_cast<double>(gridWidth - 2) / static_cast<double>(clipLengthTicks);
        const int pitch = juce::jlimit(0, 127, lowestKey + visibleKeys - 1 - ((p.y - rulerHeight) / keyHeight));
        for (const auto& selected : main.getMidiEngine().getSelectedNotesCopy())
        {
            if (selected.pitch != pitch || selected.startTick < 0 || selected.startTick >= clipLengthTicks) continue;
            const auto visibleLengthTicks = juce::jmin(selected.lengthTicks, clipLengthTicks - selected.startTick);
            const int x = pianoKeyWidth + (int)std::llround((double)selected.startTick * pixelsPerTick);
            const int y = rulerHeight + (visibleKeys - 1 - ((int)selected.pitch - lowestKey)) * keyHeight + 2;
            const int w = juce::jmax(8, (int)std::llround((double)visibleLengthTicks * pixelsPerTick));
            if (juce::Rectangle<int>(x + 1, y, w - 2, keyHeight - 4).contains(p.x, p.y)) return true;
        }
        return false;
    }

    void updateMarqueeVisual()
    {
        auto* window = findMidiWindow(marqueeCurrentScreen);
        if (window == nullptr || selectionOverlay == nullptr) return;
        auto* content = window->getContentComponent();
        if (content == nullptr) return;
        const auto a = marqueeStartScreen - content->getScreenPosition();
        const auto b = marqueeCurrentScreen - content->getScreenPosition();
        selectionOverlay->setMarquee(juce::Rectangle<int>(a.x, a.y, 0, 0).getUnion(juce::Rectangle<int>(b.x, b.y, 0, 0)), true);
    }

    void applyMarqueeSelection()
    {
        auto* window = findMidiWindow(marqueeCurrentScreen);
        auto* main = findMainComponent();
        if (window == nullptr || main == nullptr || window->getContentComponent() == nullptr) return;
        auto* content = window->getContentComponent();
        const auto a = marqueeStartScreen - content->getScreenPosition();
        const auto b = marqueeCurrentScreen - content->getScreenPosition();
        auto r = juce::Rectangle<int>(a.x, a.y, 0, 0).getUnion(juce::Rectangle<int>(b.x, b.y, 0, 0));
        r = r.getIntersection(juce::Rectangle<int>(pianoKeyWidth, rulerHeight, juce::jmax(1, content->getWidth() - pianoKeyWidth), juce::jmax(1, content->getHeight() - rulerHeight - velocityLaneHeight)));
        if (r.isEmpty()) return;

        const auto gridWidth = juce::jmax(1, content->getWidth() - pianoKeyWidth);
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(main->getTimeSignatureNumerator(), main->getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(juce::jmax<std::int64_t>(1, ticksPerMeasure), MidiEngine::secondsToTick(main->getMidiClipLengthSeconds(), main->getTempoBpm()));
        const auto pixelsPerTick = static_cast<double>(gridWidth - 2) / static_cast<double>(clipLengthTicks);
        std::vector<MidiEngine::NoteEvent> selected = marqueeBaseSelection;
        for (const auto& note : main->getMidiEngine().getNotesCopy())
        {
            if (note.pitch < lowestKey || note.pitch >= lowestKey + visibleKeys || note.startTick < 0 || note.startTick >= clipLengthTicks) continue;
            const auto visibleLengthTicks = juce::jmin(note.lengthTicks, clipLengthTicks - note.startTick);
            const int x = pianoKeyWidth + (int)std::llround((double)note.startTick * pixelsPerTick);
            const int y = rulerHeight + (visibleKeys - 1 - ((int)note.pitch - lowestKey)) * keyHeight + 2;
            const int w = juce::jmax(8, (int)std::llround((double)visibleLengthTicks * pixelsPerTick));
            const auto noteRect = juce::Rectangle<int>(x + 1, y, w - 2, keyHeight - 4);
            if (!r.intersects(noteRect)) continue;
            const auto it = std::find_if(selected.begin(), selected.end(), [&](const auto& existing) { return sameNote(existing, note); });
            if (it == selected.end()) selected.push_back(note);
        }
        main->getMidiEngine().setSelectedNotes(selected);
        main->updateMidiClipTiming();
        main->repaint();
        if (selectionOverlay != nullptr) selectionOverlay->repaint();
    }

    void attachToMidiWindow(juce::Point<int> p)
    {
        auto* window = findMidiWindow(p); if (window == nullptr) return;
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
                    selectionOverlay->toBack();
                }
            }
            content->grabKeyboardFocus();
        }
    }

    void selectNoteFromScreenPosition(juce::Point<int> screenPosition, bool additive, std::vector<MidiEngine::NoteEvent>* selectionOverride = nullptr)
    {
        auto* window = findMidiWindow(screenPosition); if (window == nullptr) return;
        auto* content = window->getContentComponent(); auto* main = findMainComponent(); if (content == nullptr || main == nullptr) return;
        const auto p = screenPosition - content->getScreenPosition();
        if (p.x < pianoKeyWidth || p.y < rulerHeight || p.y >= content->getHeight() - velocityLaneHeight) return;
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
                if (selectionOverride != nullptr)
                {
                    const auto it = std::find_if(selectionOverride->begin(), selectionOverride->end(), [&](const MidiEngine::NoteEvent& selected) { return sameNote(selected, note); });
                    if (it != selectionOverride->end()) selectionOverride->erase(it); else selectionOverride->push_back(note);
                    return;
                }
                if (additive) main->getMidiEngine().toggleNoteSelectionAt(note.startTick, note.pitch, note.channel);
                else main->getMidiEngine().selectNoteAt(note.startTick, note.pitch, note.channel);
                if (selectionOverlay != nullptr) selectionOverlay->repaint();
                main->repaint();
                return;
            }
        }
        if (selectionOverride == nullptr)
        {
            main->getMidiEngine().clearNoteSelection();
            if (selectionOverlay != nullptr) selectionOverlay->repaint();
            main->repaint();
        }
    }

    void detachFromWindows()
    {
        if (attachedContent != nullptr) attachedContent->removeKeyListener(this);
        selectionOverlay.reset();
        attachedContent = nullptr;
        marqueeCandidate = false;
        marqueeActive = false;
    }

    bool registered = true;
    juce::Component* attachedContent = nullptr;
    std::unique_ptr<MidiSelectionOverlay> selectionOverlay;
    std::vector<MidiEngine::NoteEvent> clipboardNotes;
    std::vector<MidiEngine::NoteEvent> pendingAdditiveSelection;
    bool pendingAdditiveSelectionActive = false;
    bool marqueeCandidate = false;
    bool marqueeActive = false;
    bool marqueeAdditive = false;
    juce::Point<int> marqueeStartScreen;
    juce::Point<int> marqueeCurrentScreen;
    std::vector<MidiEngine::NoteEvent> marqueeBaseSelection;
};

MidiNoteSelectionInteraction midiNoteSelectionInteraction;
}

bool handleLibertyMidiNoteSelectionKeyPress(const juce::KeyPress& key) { return midiNoteSelectionInteraction.handleKeyPress(key); }
void shutdownLibertyMidiNoteSelectionInteraction() { midiNoteSelectionInteraction.shutdown(); }
