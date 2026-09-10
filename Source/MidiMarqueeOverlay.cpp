#include "MidiMarqueeOverlay.h"
#include "MainComponent.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
constexpr int pianoKeyWidth = 72;
constexpr int rulerHeight = 30;
constexpr int keyHeight = 20;
constexpr int visibleKeys = 40;
constexpr int velocityLaneHeight = 92;
constexpr int lowestKey = 21;
constexpr int marqueeThreshold = 4;
}

MidiMarqueeOverlay::MidiMarqueeOverlay(MainComponent& o) : owner(o)
{
    setInterceptsMouseClicks(true, false);
    setOpaque(false);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
}

void MidiMarqueeOverlay::paint(juce::Graphics& g)
{
    if (!marqueeActive)
        return;

    const auto r = juce::Rectangle<int>::leftTopRightBottom(
        std::min(dragStart.x, dragCurrent.x),
        std::min(dragStart.y, dragCurrent.y),
        std::max(dragStart.x, dragCurrent.x),
        std::max(dragStart.y, dragCurrent.y));

    if (r.isEmpty())
        return;

    g.setColour(juce::Colour(0x332f80ed));
    g.fillRect(r);
    g.setColour(juce::Colour(0xff8fb7ff));
    g.drawRect(r, 1);
}

bool MidiMarqueeOverlay::hitTest(int x, int y)
{
    const auto mods = juce::ModifierKeys::getCurrentModifiers();
    if (!mods.isCommandDown() && !mods.isCtrlDown())
        return false;

    const juce::Point<int> p(x, y);
    if (!isInPianoRollGrid(p))
        return false;

    return !pointHitsAnyNote(p);
}

void MidiMarqueeOverlay::mouseDown(const juce::MouseEvent& e)
{
    if (!e.mods.isLeftButtonDown() || (!e.mods.isCommandDown() && !e.mods.isCtrlDown()))
        return;

    dragStart = e.getPosition();
    dragCurrent = dragStart;
    dragging = true;
    marqueeActive = false;
    baseSelection = owner.getMidiEngine().getSelectedNotesCopy();
    repaint();
}

void MidiMarqueeOverlay::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging)
        return;

    dragCurrent = e.getPosition();
    if (!marqueeActive && dragStart.getDistanceFrom(dragCurrent) >= marqueeThreshold)
        marqueeActive = true;

    if (marqueeActive)
        repaint();
}

void MidiMarqueeOverlay::mouseUp(const juce::MouseEvent& e)
{
    if (!dragging)
        return;

    dragCurrent = e.getPosition();
    if (marqueeActive)
        applySelection();

    dragging = false;
    marqueeActive = false;
    baseSelection.clear();
    repaint();
}

bool MidiMarqueeOverlay::isInPianoRollGrid(juce::Point<int> p) const noexcept
{
    const auto gridBottom = getHeight() - velocityLaneHeight;
    return p.x >= pianoKeyWidth && p.y >= rulerHeight && p.y < gridBottom;
}

juce::Rectangle<int> MidiMarqueeOverlay::noteRectangle(juce::Point<int> /*p*/,
                                                        std::int64_t startTick,
                                                        std::int64_t lengthTicks,
                                                        int pitch) const
{
    const auto gridWidth = juce::jmax(1, getWidth() - pianoKeyWidth);
    const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(),
                                                             owner.getTimeSignatureDenominator());
    const auto clipLengthTicks = juce::jmax<std::int64_t>(
        juce::jmax<std::int64_t>(1, ticksPerMeasure),
        MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
    const auto pixelsPerTick = static_cast<double>(juce::jmax(1, gridWidth - 2))
                               / static_cast<double>(clipLengthTicks);

    const int x = pianoKeyWidth + static_cast<int>(std::llround(static_cast<double>(startTick) * pixelsPerTick));
    const int y = rulerHeight + (visibleKeys - 1 - (pitch - lowestKey)) * keyHeight + 2;
    const int w = juce::jmax(8, static_cast<int>(std::llround(static_cast<double>(lengthTicks) * pixelsPerTick)));

    return juce::Rectangle<int>(x + 1, y, juce::jmax(1, w - 2), keyHeight - 4);
}

bool MidiMarqueeOverlay::pointHitsAnyNote(juce::Point<int> p) const
{
    for (const auto& note : owner.getMidiEngine().getNotesCopy())
    {
        if (note.pitch < lowestKey || note.pitch >= lowestKey + visibleKeys)
            continue;

        if (noteRectangle(p, note.startTick, note.lengthTicks, note.pitch).contains(p))
            return true;
    }

    return false;
}

void MidiMarqueeOverlay::applySelection()
{
    auto selection = baseSelection;
    const auto marquee = juce::Rectangle<int>::leftTopRightBottom(
        std::min(dragStart.x, dragCurrent.x),
        std::min(dragStart.y, dragCurrent.y),
        std::max(dragStart.x, dragCurrent.x),
        std::max(dragStart.y, dragCurrent.y));

    if (marquee.isEmpty())
        return;

    const auto addIfMissing = [&selection](const MidiEngine::NoteEvent& note)
    {
        const auto it = std::find_if(selection.begin(), selection.end(), [&note](const auto& existing)
        {
            return existing.startTick == note.startTick
                && existing.pitch == note.pitch
                && existing.channel == note.channel;
        });
        if (it == selection.end())
            selection.push_back(note);
    };

    for (const auto& note : owner.getMidiEngine().getNotesCopy())
    {
        if (note.pitch < lowestKey || note.pitch >= lowestKey + visibleKeys)
            continue;

        if (marquee.intersects(noteRectangle({}, note.startTick, note.lengthTicks, note.pitch)))
            addIfMissing(note);
    }

    owner.getMidiEngine().setSelectedNotes(selection);
    owner.repaint();
}
