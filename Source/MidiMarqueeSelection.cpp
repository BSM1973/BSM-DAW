#include "MainComponent.h"
#include "MidiEngine.h"
#include <juce_gui_basics/juce_gui_basics.h>
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
constexpr int dragThreshold = 4;

bool isAdditiveModifier() noexcept
{
    const auto mods = juce::ModifierKeys::getCurrentModifiersRealtime();
    return mods.isCommandDown() || mods.isCtrlDown();
}

class MidiMarqueeOverlay final : public juce::Component, private juce::Timer
{
public:
    MidiMarqueeOverlay(juce::Component& contentComponent, MainComponent& owner)
        : content(contentComponent), main(owner)
    {
        setOpaque(false);
        setInterceptsMouseClicks(true, false);
        setAlwaysOnTop(true);
        setBounds(content.getLocalBounds());
        startTimerHz(30);
    }

    bool hitTest(int x, int y) override
    {
        if (!isAdditiveModifier()) return false;
        if (x < pianoKeyWidth || y < rulerHeight || y >= getHeight() - velocityLaneHeight) return false;
        return !pointHitsAnyNote(x, y);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!e.mods.isLeftButtonDown() || !isAdditiveModifier()) return;
        marqueeStart = e.getPosition();
        marqueeCurrent = marqueeStart;
        marqueeActive = false;
        baseSelection = main.getMidiEngine().getSelectedNotesCopy();
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!e.mods.isLeftButtonDown()) return;
        marqueeCurrent = e.getPosition();
        if (!marqueeActive && marqueeStart.getDistanceFrom(marqueeCurrent) >= dragThreshold)
            marqueeActive = true;
        if (marqueeActive) repaint();
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        marqueeCurrent = e.getPosition();
        if (marqueeActive) applySelection();
        marqueeActive = false;
        baseSelection.clear();
        repaint();
        main.repaint();
    }

    void paint(juce::Graphics& g) override
    {
        if (!marqueeActive) return;
        const auto r = makeMarqueeRect();
        g.setColour(juce::Colour(0x332f80ed));
        g.fillRect(r);
        g.setColour(juce::Colour(0xff8fb7ff));
        g.drawRect(r, 1);
    }

private:
    void timerCallback() override
    {
        if (content.isShowing())
        {
            setBounds(content.getLocalBounds());
            toFront(false);
        }
    }

    juce::Rectangle<int> makeMarqueeRect() const noexcept
    {
        return juce::Rectangle<int>(marqueeStart.x, marqueeStart.y, 0, 0)
            .getUnion(juce::Rectangle<int>(marqueeCurrent.x, marqueeCurrent.y, 0, 0));
    }

    bool pointHitsAnyNote(int x, int y) const
    {
        const auto gridWidth = juce::jmax(1, getWidth() - pianoKeyWidth);
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(main.getTimeSignatureNumerator(), main.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(juce::jmax<std::int64_t>(1, ticksPerMeasure), MidiEngine::secondsToTick(main.getMidiClipLengthSeconds(), main.getTempoBpm()));
        const auto pixelsPerTick = static_cast<double>(gridWidth - 2) / static_cast<double>(clipLengthTicks);
        const int pitch = juce::jlimit(0, 127, lowestKey + visibleKeys - 1 - ((y - rulerHeight) / keyHeight));
        for (const auto& note : main.getMidiEngine().getNotesCopy())
        {
            if (note.pitch != pitch || note.startTick < 0 || note.startTick >= clipLengthTicks) continue;
            const auto visibleLengthTicks = juce::jmin(note.lengthTicks, clipLengthTicks - note.startTick);
            const int nx = pianoKeyWidth + (int)std::llround((double)note.startTick * pixelsPerTick);
            const int ny = rulerHeight + (visibleKeys - 1 - ((int)note.pitch - lowestKey)) * keyHeight + 2;
            const int nw = juce::jmax(8, (int)std::llround((double)visibleLengthTicks * pixelsPerTick));
            if (juce::Rectangle<int>(nx + 1, ny, nw - 2, keyHeight - 4).contains(x, y)) return true;
        }
        return false;
    }

    void applySelection()
    {
        auto r = makeMarqueeRect().getIntersection(juce::Rectangle<int>(pianoKeyWidth, rulerHeight, juce::jmax(1, getWidth() - pianoKeyWidth), juce::jmax(1, getHeight() - rulerHeight - velocityLaneHeight)));
        if (r.isEmpty()) return;
        const auto gridWidth = juce::jmax(1, getWidth() - pianoKeyWidth);
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(main.getTimeSignatureNumerator(), main.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(juce::jmax<std::int64_t>(1, ticksPerMeasure), MidiEngine::secondsToTick(main.getMidiClipLengthSeconds(), main.getTempoBpm()));
        const auto pixelsPerTick = static_cast<double>(gridWidth - 2) / static_cast<double>(clipLengthTicks);
        std::vector<MidiEngine::NoteEvent> selection = baseSelection;
        for (const auto& note : main.getMidiEngine().getNotesCopy())
        {
            if (note.pitch < lowestKey || note.pitch >= lowestKey + visibleKeys || note.startTick < 0 || note.startTick >= clipLengthTicks) continue;
            const auto visibleLengthTicks = juce::jmin(note.lengthTicks, clipLengthTicks - note.startTick);
            const int x = pianoKeyWidth + (int)std::llround((double)note.startTick * pixelsPerTick);
            const int y = rulerHeight + (visibleKeys - 1 - ((int)note.pitch - lowestKey)) * keyHeight + 2;
            const int w = juce::jmax(8, (int)std::llround((double)visibleLengthTicks * pixelsPerTick));
            const auto noteRect = juce::Rectangle<int>(x + 1, y, w - 2, keyHeight - 4);
            if (!r.intersects(noteRect)) continue;
            const auto alreadySelected = std::find_if(selection.begin(), selection.end(), [&note](const auto& selected)
            { return selected.startTick == note.startTick && selected.pitch == note.pitch && selected.channel == note.channel; });
            if (alreadySelected == selection.end()) selection.push_back(note);
        }
        main.getMidiEngine().setSelectedNotes(selection);
    }

    juce::Component& content;
    MainComponent& main;
    juce::Point<int> marqueeStart;
    juce::Point<int> marqueeCurrent;
    std::vector<MidiEngine::NoteEvent> baseSelection;
    bool marqueeActive = false;
};

class MidiMarqueeManager final : public juce::Timer
{
public:
    MidiMarqueeManager() { startTimerHz(20); }
    ~MidiMarqueeManager() override { overlay.reset(); }
private:
    void timerCallback() override
    {
        juce::DocumentWindow* midiWindow = nullptr;
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i)))
                if (window->getName() == "Liberty - MIDI 1") { midiWindow = window; break; }
        if (midiWindow == nullptr) { overlay.reset(); content = nullptr; return; }
        auto* newContent = midiWindow->getContentComponent();
        auto* main = dynamic_cast<MainComponent*>(newContent);
        if (newContent == nullptr || main == nullptr) return;
        if (content != newContent)
        {
            overlay.reset(); content = newContent;
            overlay = std::make_unique<MidiMarqueeOverlay>(*newContent, *main);
            newContent->addAndMakeVisible(overlay.get()); overlay->toFront(false);
        }
    }
    juce::Component* content = nullptr;
    std::unique_ptr<MidiMarqueeOverlay> overlay;
};
MidiMarqueeManager midiMarqueeManager;
}
