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
constexpr int resizeEdgePixels = 12;
constexpr std::int64_t gridTicks = MidiEngine::ticksPerQuarterNote / 4;

class MidiGroupDragOverlay final : public juce::Component, private juce::Timer
{
public:
    explicit MidiGroupDragOverlay(MainComponent& o) : owner(o)
    {
        setOpaque(false);
        setInterceptsMouseClicks(true, false);
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        startTimerHz(30);
    }

    void resized() override { repaint(); }

    bool hitTest(int x, int y) override
    {
        if (dragging || owner.getMidiEngine().getNumSelectedNotes() <= 1)
            return dragging;
        if (x < pianoKeyWidth || y < rulerHeight || y >= getHeight() - velocityLaneHeight)
            return false;
        const auto* note = findSelectedNoteAt({ x, y });
        return note != nullptr && !isNearResizeEdge(*note, x);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!e.mods.isLeftButtonDown()) return;
        const auto* note = findSelectedNoteAt(e.getPosition());
        if (note == nullptr || owner.getMidiEngine().getNumSelectedNotes() <= 1)
            return;

        if (lastSelection.size() > 1)
            owner.getMidiEngine().setSelectedNotes(lastSelection);
        selectionBeforeDrag = owner.getMidiEngine().getSelectedNotesCopy();
        dragStartTick = tickFromX(e.x);
        dragStartPitch = pitchFromY(e.y);
        appliedTickDelta = 0;
        appliedPitchDelta = 0;
        dragging = true;
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!dragging) return;

        const auto targetTick = tickFromX(e.x);
        const int targetPitch = pitchFromY(e.y);
        const auto requestedTickDelta = targetTick - dragStartTick;
        const int requestedPitchDelta = targetPitch - dragStartPitch;
        const auto stepTickDelta = requestedTickDelta - appliedTickDelta;
        const int stepPitchDelta = requestedPitchDelta - appliedPitchDelta;

        if (stepTickDelta == 0 && stepPitchDelta == 0)
            return;

        if (owner.getMidiEngine().moveSelectedNotesBy(stepTickDelta, stepPitchDelta))
        {
            appliedTickDelta = requestedTickDelta;
            appliedPitchDelta = requestedPitchDelta;
            owner.updateMidiClipTiming();
            owner.repaint();
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (!dragging) return;
        dragging = false;
        selectionBeforeDrag.clear();
        owner.updateMidiClipTiming();
        owner.repaint();
        repaint();
    }

private:
    void timerCallback() override
    {
        if (!dragging)
            lastSelection = owner.getMidiEngine().getSelectedNotesCopy();
    }

    const MidiEngine::NoteEvent* findSelectedNoteAt(juce::Point<int> p) const
    {
        const auto selected = owner.getMidiEngine().getSelectedNotesCopy();
        const auto pixelsPerTick = getPixelsPerTick();
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(
            juce::jmax<std::int64_t>(1, ticksPerMeasure),
            MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));

        for (const auto& n : selected)
        {
            if (n.pitch < lowestKey || n.pitch >= lowestKey + visibleKeys || n.startTick < 0 || n.startTick >= clipLengthTicks)
                continue;
            const int x = pianoKeyWidth + (int) std::llround((double) n.startTick * pixelsPerTick);
            const int y = rulerHeight + (visibleKeys - 1 - ((int) n.pitch - lowestKey)) * keyHeight + 2;
            const auto visibleLengthTicks = juce::jmin(n.lengthTicks, clipLengthTicks - n.startTick);
            const int w = juce::jmax(8, (int) std::llround((double) visibleLengthTicks * pixelsPerTick));
            const auto rect = juce::Rectangle<int>(x + 1, y, w - 2, keyHeight - 4);
            if (rect.contains(p)) return &n;
        }
        return nullptr;
    }

    bool isNearResizeEdge(const MidiEngine::NoteEvent& note, int x) const
    {
        const auto pixelsPerTick = getPixelsPerTick();
        const int left = pianoKeyWidth + (int) std::llround((double) note.startTick * pixelsPerTick) + 1;
        const int width = juce::jmax(8, (int) std::llround((double) note.lengthTicks * pixelsPerTick)) - 2;
        const int right = left + width;
        return std::abs(x - left) <= resizeEdgePixels || std::abs(x - right) <= resizeEdgePixels;
    }

    double getPixelsPerTick() const
    {
        const auto gridWidth = juce::jmax(1, getWidth() - pianoKeyWidth);
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(
            juce::jmax<std::int64_t>(1, ticksPerMeasure),
            MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        return static_cast<double>(gridWidth - 2) / static_cast<double>(clipLengthTicks);
    }

    int pitchFromY(int y) const noexcept
    {
        const int row = juce::jlimit(0, visibleKeys - 1, (y - rulerHeight) / keyHeight);
        return lowestKey + visibleKeys - 1 - row;
    }

    std::int64_t tickFromX(int x) const noexcept
    {
        const auto raw = (std::int64_t) std::llround((x - pianoKeyWidth) / getPixelsPerTick());
        return MidiEngine::quantizeTick(juce::jmax<std::int64_t>(0, raw), gridTicks);
    }

    MainComponent& owner;
    bool dragging = false;
    std::int64_t dragStartTick = 0;
    std::int64_t appliedTickDelta = 0;
    int dragStartPitch = 60;
    int appliedPitchDelta = 0;
    std::vector<MidiEngine::NoteEvent> selectionBeforeDrag;
    std::vector<MidiEngine::NoteEvent> lastSelection;
};

class MidiGroupDragManager final : private juce::Timer
{
public:
    MidiGroupDragManager() { startTimerHz(10); }
    ~MidiGroupDragManager() override { detach(); }

private:
    void timerCallback() override
    {
        auto* content = findMidiContent();
        if (content == attachedContent) return;
        detach();
        if (content == nullptr) return;
        attachedContent = content;
        if (auto* main = findMainComponent())
        {
            overlay = std::make_unique<MidiGroupDragOverlay>(*main);
            content->addAndMakeVisible(overlay.get());
            overlay->setBounds(content->getLocalBounds());
            overlay->toFront(false);
        }
    }

    static MainComponent* findMainInTree(juce::Component* c) noexcept
    {
        if (c == nullptr) return nullptr;
        if (auto* main = dynamic_cast<MainComponent*>(c)) return main;
        for (int i = 0; i < c->getNumChildComponents(); ++i)
            if (auto* main = findMainInTree(c->getChildComponent(i))) return main;
        return nullptr;
    }

    static MainComponent* findMainComponent() noexcept
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* main = findMainInTree(desktop.getComponent(i))) return main;
        return nullptr;
    }

    static juce::Component* findMidiContent() noexcept
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
        {
            auto* c = desktop.getComponent(i);
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(c))
                if (window->getName() == "Liberty - MIDI 1")
                    return window->getContentComponent();
        }
        return nullptr;
    }

    void detach() noexcept
    {
        overlay.reset();
        attachedContent = nullptr;
    }

    juce::Component* attachedContent = nullptr;
    std::unique_ptr<MidiGroupDragOverlay> overlay;
};

MidiGroupDragManager groupDragManager;
}
