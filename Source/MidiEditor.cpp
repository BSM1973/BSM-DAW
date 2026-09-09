#include "MidiEditor.h"
#include "MainComponent.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>

namespace
{
constexpr int lowestKey = 21;
constexpr int pianoKeyWidth = 72;
constexpr int rulerHeight = 30;
constexpr int keyHeight = 20;
constexpr int visibleKeys = 40;
constexpr int velocityLaneHeight = 92;
constexpr std::int64_t gridTicks = MidiEngine::ticksPerQuarterNote / 4;
constexpr int resizeEdgePixels = 12;

const char* noteName(int n)
{
    static constexpr const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[n % 12];
}

bool isBlackKey(int n) { return n % 12 == 1 || n % 12 == 3 || n % 12 == 6 || n % 12 == 8 || n % 12 == 10; }

MainComponent* findMainComponent(juce::Component* component) noexcept
{
    while (component != nullptr)
    {
        if (auto* main = dynamic_cast<MainComponent*>(component)) return main;
        component = component->getParentComponent();
    }
    return nullptr;
}

class VelocityLane final : public juce::Component
{
public:
    explicit VelocityLane(MainComponent& o) : owner(o)
    {
        setInterceptsMouseClicks(true, true);
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff15181d));
        g.setColour(juce::Colour(0xff3a414a));
        g.drawHorizontalLine(0, 0.0f, (float) getWidth());

        g.setColour(juce::Colour(0xff8b929b));
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText("VELOCITY", 8, 8, pianoKeyWidth - 16, 16, juce::Justification::centredLeft);

        const auto gridWidth = juce::jmax(1, getWidth() - pianoKeyWidth);
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(
            juce::jmax<std::int64_t>(1, ticksPerMeasure),
            MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        const auto pixelsPerTick = static_cast<double>(gridWidth - 2) / static_cast<double>(clipLengthTicks);

        for (const auto& note : owner.getMidiEngine().getNotesCopy())
        {
            if (note.startTick < 0 || note.startTick >= clipLengthTicks)
                continue;

            const int x = pianoKeyWidth + (int) std::llround((double) note.startTick * pixelsPerTick);
            const int nextX = pianoKeyWidth + (int) std::llround((double) (note.startTick + note.lengthTicks) * pixelsPerTick);
            const int barWidth = juce::jmax(4, juce::jmin(18, nextX - x));
            const int usableHeight = juce::jmax(1, getHeight() - 28);
            const int barHeight = juce::jlimit(2, usableHeight,
                                                (int) std::llround((double) note.velocity / 127.0 * usableHeight));
            const auto bar = juce::Rectangle<int>(x, getHeight() - barHeight - 8, barWidth, barHeight);

            g.setColour(juce::Colour(0xff63c7e8));
            g.fillRoundedRectangle(bar.toFloat(), 2.0f);
            g.setColour(juce::Colour(0xffaee8fa));
            g.drawRoundedRectangle(bar.toFloat(), 2.0f, 1.0f);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!e.mods.isLeftButtonDown() || e.x < pianoKeyWidth)
            return;

        const auto note = findNoteForX(e.x);
        if (note == nullptr)
            return;

        editing = true;
        editStartTick = note->startTick;
        editPitch = note->pitch;
        editChannel = note->channel;
        updateVelocity(e.y);
        repaint();
        owner.repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!editing)
            return;
        updateVelocity(e.y);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        editing = false;
        repaint();
        owner.repaint();
    }

private:
    const MidiEngine::NoteEvent* findNoteForX(int x) const
    {
        const auto gridWidth = juce::jmax(1, getWidth() - pianoKeyWidth);
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(
            juce::jmax<std::int64_t>(1, ticksPerMeasure),
            MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        const auto pixelsPerTick = static_cast<double>(gridWidth - 2) / static_cast<double>(clipLengthTicks);

        const MidiEngine::NoteEvent* best = nullptr;
        double bestDistance = std::numeric_limits<double>::max();

        for (const auto& note : owner.getMidiEngine().getNotesCopy())
        {
            if (note.startTick < 0 || note.startTick >= clipLengthTicks)
                continue;

            const int noteX = pianoKeyWidth + (int) std::llround((double) note.startTick * pixelsPerTick);
            const int nextX = pianoKeyWidth + (int) std::llround((double) (note.startTick + note.lengthTicks) * pixelsPerTick);
            const int barWidth = juce::jmax(4, juce::jmin(18, nextX - noteX));
            const double hitLeft = static_cast<double>(noteX) - 8.0;
            const double hitRight = static_cast<double>(noteX + barWidth) + 8.0;

            if (static_cast<double>(x) < hitLeft || static_cast<double>(x) > hitRight)
                continue;

            const double distance = std::abs(static_cast<double>(x) - (static_cast<double>(noteX) + barWidth * 0.5));
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = &note;
            }
        }

        return best;
    }

    void updateVelocity(int y)
    {
        const int laneBottom = getHeight() - 8;
        const int usableHeight = juce::jmax(1, getHeight() - 24);
        const int clampedY = juce::jlimit(8, laneBottom, y);
        const int velocity = juce::jlimit(1, 127,
            (int) std::llround((double) (laneBottom - clampedY) / (double) usableHeight * 127.0));

        if (owner.getMidiEngine().setNoteVelocity(editStartTick, editPitch, editChannel, velocity))
        {
            owner.updateMidiClipTiming();
            repaint();
            owner.repaint();
        }
    }

    MainComponent& owner;
    bool editing = false;
    std::int64_t editStartTick = 0;
    int editPitch = 60;
    int editChannel = 1;
};

class PianoRoll final : public juce::Component, private juce::Timer
{
public:
    explicit PianoRoll(MainComponent& o) : owner(o), velocityLane(o)
    {
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        addAndMakeVisible(velocityLane);
        startTimerHz(30);
    }

    void resized() override
    {
        velocityLane.setBounds(0, juce::jmax(rulerHeight, getHeight() - velocityLaneHeight),
                               getWidth(), juce::jmin(velocityLaneHeight, getHeight()));
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0b0d10));
        const auto b = getLocalBounds();
        const auto velocityAreaY = juce::jmax(rulerHeight, b.getBottom() - velocityLaneHeight);
        const auto grid = juce::Rectangle<int>(pianoKeyWidth, rulerHeight, b.getWidth() - pianoKeyWidth, juce::jmax(1, velocityAreaY - rulerHeight));
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(juce::jmax<std::int64_t>(1, ticksPerMeasure), MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        const double pixelsPerTick = getPixelsPerTick();

        g.setColour(juce::Colour(0xff15181d)); g.fillRect(0, 0, pianoKeyWidth, rulerHeight);
        g.setColour(juce::Colour(0xff20242b)); g.fillRect(grid.getX(), 0, grid.getWidth(), rulerHeight);
        for (std::int64_t t = 0; t <= clipLengthTicks; t += gridTicks)
        {
            const int x = grid.getX() + (int)std::llround((double)t * pixelsPerTick);
            if (x > grid.getRight()) break;
            const bool measure = ticksPerMeasure > 0 && t % ticksPerMeasure == 0;
            const bool beat = t % MidiEngine::ticksPerQuarterNote == 0;
            g.setColour(measure ? juce::Colour(0xff4b525c) : beat ? juce::Colour(0xff343a43) : juce::Colour(0xff252a31));
            g.drawVerticalLine(x, (float)rulerHeight, (float)grid.getBottom());
        }
        g.setColour(juce::Colour(0xff747b85)); g.setFont(juce::Font(10.0f));
        const auto measureCount = ticksPerMeasure > 0 ? (int)((clipLengthTicks + ticksPerMeasure - 1) / ticksPerMeasure) : 1;
        for (int m = 0; m < measureCount; ++m)
        {
            const auto tick = (std::int64_t)m * juce::jmax<std::int64_t>(1, ticksPerMeasure);
            const int x = grid.getX() + (int)std::llround((double)tick * pixelsPerTick);
            if (x > grid.getRight()) break;
            g.drawText(juce::String(m + 1), x + 5, 7, 36, 16, juce::Justification::left);
        }
        for (int row = 0; row < visibleKeys; ++row)
        {
            const int note = lowestKey + row;
            const int y = rulerHeight + (visibleKeys - 1 - row) * keyHeight;
            const auto key = juce::Rectangle<int>(0, y, pianoKeyWidth, keyHeight);
            const bool black = isBlackKey(note);
            g.setColour(black ? juce::Colour(0xff171a1f) : juce::Colour(0xffd6d9de)); g.fillRect(key);
            g.setColour(black ? juce::Colour(0xff303640) : juce::Colour(0xff777d86)); g.drawRect(key, 1);
            if (!black) { g.setColour(juce::Colour(0xff2b3037)); g.setFont(juce::Font(9.0f)); g.drawText(juce::String(noteName(note)) + juce::String(note / 12 - 1), key.reduced(5, 0), juce::Justification::centredLeft); }
        }

        const auto playbackTick = MidiEngine::secondsToTick(owner.getMidiEngine().getPlaybackPositionSeconds(), owner.getTempoBpm());
        const int playheadX = grid.getX() + (int)std::llround((double)playbackTick * pixelsPerTick);
        if (playheadX >= grid.getX() && playheadX <= grid.getRight())
        {
            g.setColour(owner.getMidiEngine().isPlaying() ? juce::Colour(0xfff3f6fa) : juce::Colour(0xff8b929b));
            g.drawLine((float)playheadX, (float)rulerHeight, (float)playheadX, (float)grid.getBottom(), 2.0f);
        }
        for (const auto& n : owner.getMidiEngine().getNotesCopy())
        {
            if (n.pitch < lowestKey || n.pitch >= lowestKey + visibleKeys || n.startTick >= clipLengthTicks) continue;
            const int y = rulerHeight + (visibleKeys - 1 - ((int)n.pitch - lowestKey)) * keyHeight + 2;
            const int x = grid.getX() + (int)std::llround((double)n.startTick * pixelsPerTick);
            const auto visibleLengthTicks = juce::jmin(n.lengthTicks, clipLengthTicks - n.startTick);
            const int w = juce::jmax(8, (int)std::llround((double)visibleLengthTicks * pixelsPerTick));
            const auto r = juce::Rectangle<int>(x + 1, y, w - 2, keyHeight - 4);
            if (r.getRight() <= grid.getX() || r.getX() >= grid.getRight()) continue;
            const auto noteEnd = n.startTick + n.lengthTicks;
            const bool active = playbackTick >= n.startTick && playbackTick < noteEnd && owner.getMidiEngine().isPlaying();
            g.setColour(active ? juce::Colour(0xff72d8f5) : juce::Colour(0xff4f82ff)); g.fillRoundedRectangle(r.toFloat(), 3.0f);
            g.setColour(active ? juce::Colours::white : juce::Colour(0xff9fd8f5)); g.drawRoundedRectangle(r.toFloat(), 3.0f, 1.0f);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        if (draggingNote) return;
        if (isInVelocityLane(e.y)) { setMouseCursor(juce::MouseCursor::UpDownResizeCursor); return; }
        const auto side = findResizeSide(e.position);
        if (side != ResizeSide::none) { setMouseCursor(juce::MouseCursor::LeftRightResizeCursor); return; }
        if (findNoteAt(e.position) != nullptr) { setMouseCursor(juce::MouseCursor::DraggingHandCursor); return; }
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
    }
    void mouseExit(const juce::MouseEvent&) override { if (!draggingNote) setMouseCursor(juce::MouseCursor::CrosshairCursor); }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown() || e.y < rulerHeight || e.x < pianoKeyWidth) return;
        const int pitch = pitchFromY(e.y);
        auto& midi = owner.getMidiEngine();
        for (const auto& n : midi.getNotesCopy())
        {
            if (n.pitch < lowestKey || n.pitch >= lowestKey + visibleKeys) continue;
            const int x = pianoKeyWidth + (int)std::llround((double)n.startTick * getPixelsPerTick());
            const int y = rulerHeight + (visibleKeys - 1 - ((int)n.pitch - lowestKey)) * keyHeight + 2;
            const int w = juce::jmax(8, (int)std::llround((double)n.lengthTicks * getPixelsPerTick()));
            const auto noteRect = juce::Rectangle<int>(x + 1, y, w - 2, keyHeight - 4);
            if (n.pitch == pitch && noteRect.contains(e.getPosition()))
            {
                draggingNote = true; dragMoved = false; resizeSide = findResizeSide(e.position);
                dragStartTick = n.startTick; dragEndTick = n.startTick + n.lengthTicks; dragPitch = n.pitch; dragChannel = n.channel; originalLengthTicks = n.lengthTicks;
                setMouseCursor(resizeSide == ResizeSide::none ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::LeftRightResizeCursor);
                return;
            }
        }
        if (midi.addNote(tickFromX(e.x), MidiEngine::ticksPerQuarterNote, pitch, 100, 1)) { owner.updateMidiClipTiming(); repaint(); owner.repaint(); }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!draggingNote) return;
        const auto mouseTick = tickFromX(e.x);
        if (resizeSide == ResizeSide::right)
        {
            const auto newLength = juce::jmax(gridTicks, mouseTick - dragStartTick);
            if (newLength != originalLengthTicks && owner.getMidiEngine().setNoteLength(dragStartTick, dragPitch, dragChannel, newLength)) { originalLengthTicks = newLength; dragEndTick = dragStartTick + newLength; dragMoved = true; owner.updateMidiClipTiming(); repaint(); owner.repaint(); }
            return;
        }
        if (resizeSide == ResizeSide::left)
        {
            const auto maxStart = juce::jmax<std::int64_t>(0, dragEndTick - gridTicks);
            const auto newStart = juce::jlimit<std::int64_t>(0, maxStart, mouseTick);
            const auto newLength = dragEndTick - newStart;
            if (newStart != dragStartTick && newLength >= gridTicks && owner.getMidiEngine().moveNote(dragStartTick, dragPitch, dragChannel, newStart, dragPitch) && owner.getMidiEngine().setNoteLength(newStart, dragPitch, dragChannel, newLength)) { dragStartTick = newStart; originalLengthTicks = newLength; dragMoved = true; owner.updateMidiClipTiming(); repaint(); owner.repaint(); }
            return;
        }
        const auto newTick = tickFromX(e.x); const int newPitch = pitchFromY(e.y);
        if (newTick == dragStartTick && newPitch == dragPitch) return;
        if (owner.getMidiEngine().moveNote(dragStartTick, dragPitch, dragChannel, newTick, newPitch)) { dragStartTick = newTick; dragPitch = newPitch; dragEndTick = newTick + originalLengthTicks; dragMoved = true; owner.updateMidiClipTiming(); repaint(); owner.repaint(); }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (!draggingNote) return;
        if (!dragMoved && resizeSide == ResizeSide::none) owner.getMidiEngine().removeNoteAt(dragStartTick, dragPitch, dragChannel);
        owner.updateMidiClipTiming(); draggingNote = false; dragMoved = false; resizeSide = ResizeSide::none; originalLengthTicks = 0; dragEndTick = 0;
        setMouseCursor(juce::MouseCursor::CrosshairCursor); repaint(); owner.repaint();
    }

private:
    enum class ResizeSide { none, left, right };
    double getPixelsPerTick() const
    {
        const auto gridWidth = juce::jmax(1, getWidth() - pianoKeyWidth);
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(juce::jmax<std::int64_t>(1, ticksPerMeasure), MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        return static_cast<double>(gridWidth - 2) / static_cast<double>(clipLengthTicks);
    }
    bool isInVelocityLane(int y) const noexcept { return y >= juce::jmax(rulerHeight, getHeight() - velocityLaneHeight) && y < getHeight(); }

    const MidiEngine::NoteEvent* findNoteAt(juce::Point<float> position) const
    {
        const auto pixelsPerTick = getPixelsPerTick();
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(juce::jmax<std::int64_t>(1, ticksPerMeasure), MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        for (const auto& n : owner.getMidiEngine().getNotesCopy())
        {
            if (n.pitch < lowestKey || n.pitch >= lowestKey + visibleKeys || n.startTick >= clipLengthTicks) continue;
            const int x = pianoKeyWidth + (int)std::llround((double)n.startTick * pixelsPerTick);
            const int y = rulerHeight + (visibleKeys - 1 - ((int)n.pitch - lowestKey)) * keyHeight + 2;
            const auto visibleLengthTicks = juce::jmin(n.lengthTicks, clipLengthTicks - n.startTick);
            const int w = juce::jmax(8, (int)std::llround((double)visibleLengthTicks * pixelsPerTick));
            const auto noteRect = juce::Rectangle<int>(x + 1, y, w - 2, keyHeight - 4);
            if (n.pitch == pitchFromY((int)position.y) && noteRect.contains((int)position.x, (int)position.y)) return &n;
        }
        return nullptr;
    }

    ResizeSide findResizeSide(juce::Point<float> position) const
    {
        if (position.y < rulerHeight || position.y >= getHeight() - velocityLaneHeight || position.x < pianoKeyWidth) return ResizeSide::none;
        const auto pixelsPerTick = getPixelsPerTick();
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(juce::jmax<std::int64_t>(1, ticksPerMeasure), MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        ResizeSide bestSide = ResizeSide::none; float bestDistance = (float)resizeEdgePixels + 1.0f;
        for (const auto& n : owner.getMidiEngine().getNotesCopy())
        {
            if (n.pitch < lowestKey || n.pitch >= lowestKey + visibleKeys || n.startTick >= clipLengthTicks) continue;
            const int x = pianoKeyWidth + (int)std::llround((double)n.startTick * pixelsPerTick);
            const int y = rulerHeight + (visibleKeys - 1 - ((int)n.pitch - lowestKey)) * keyHeight + 2;
            const auto visibleLengthTicks = juce::jmin(n.lengthTicks, clipLengthTicks - n.startTick);
            const int w = juce::jmax(8, (int)std::llround((double)visibleLengthTicks * pixelsPerTick));
            const auto noteRect = juce::Rectangle<int>(x + 1, y, w - 2, keyHeight - 4);
            if (n.pitch != pitchFromY((int)position.y) || !noteRect.contains((int)position.x, (int)position.y)) continue;
            const float leftDistance = std::abs(position.x - (float)noteRect.getX()); const float rightDistance = std::abs(position.x - (float)noteRect.getRight());
            if (leftDistance <= (float)resizeEdgePixels && leftDistance <= bestDistance) { bestDistance = leftDistance; bestSide = ResizeSide::left; }
            if (rightDistance <= (float)resizeEdgePixels && rightDistance < bestDistance) { bestDistance = rightDistance; bestSide = ResizeSide::right; }
        }
        return bestSide;
    }
    void timerCallback() override { auto& midi = owner.getMidiEngine(); midi.setPlaybackPositionSeconds(owner.getAudioCurrentTimeSeconds()); midi.setPlaying(owner.isAudioPlaying()); repaint(); }
    int pitchFromY(int y) const noexcept { const int row = juce::jlimit(0, visibleKeys - 1, (y - rulerHeight) / keyHeight); return lowestKey + visibleKeys - 1 - row; }
    std::int64_t tickFromX(int x) const noexcept { const auto raw = (std::int64_t)std::llround((x - pianoKeyWidth) / getPixelsPerTick()); return MidiEngine::quantizeTick(juce::jmax<std::int64_t>(0, raw), gridTicks); }

    MainComponent& owner;
    VelocityLane velocityLane;
    bool draggingNote = false, dragMoved = false;
    ResizeSide resizeSide = ResizeSide::none;
    std::int64_t dragStartTick = 0, dragEndTick = 0, originalLengthTicks = 0;
    int dragPitch = 60, dragChannel = 1;
};

class MidiEditorWindow final : public juce::DocumentWindow
{
public:
    explicit MidiEditorWindow(MainComponent& owner) : DocumentWindow("Liberty - MIDI 1", juce::Colour(0xff0b0d10), DocumentWindow::closeButton)
    { setUsingNativeTitleBar(true); setContentOwned(new PianoRoll(owner), true); setResizable(true, true); setResizeLimits(900, 500, 1800, 1000); centreWithSize(1200, 760); setVisible(true); toFront(true); }
    void closeButtonPressed() override { setVisible(false); }
};

class MidiEditorMouseListener final : public juce::MouseListener
{
public:
    MidiEditorMouseListener() { juce::Desktop::getInstance().addGlobalMouseListener(this); }
    void shutdown() { if (registered) { juce::Desktop::getInstance().removeGlobalMouseListener(this); registered = false; } }
    ~MidiEditorMouseListener() override { shutdown(); }
    void mouseDown(const juce::MouseEvent& e) override { auto* main = findMainComponent(e.eventComponent); if (main == nullptr) return; const auto p = e.getEventRelativeTo(main).getPosition(); constexpr int top = 76 + 32 + 4 * 70, height = 70; if (p.y >= top && p.y < top + height) main->selectMidiTrack(); }
    void mouseDoubleClick(const juce::MouseEvent& e) override { auto* main = findMainComponent(e.eventComponent); if (main == nullptr) return; const auto p = e.getEventRelativeTo(main).getPosition(); constexpr int top = 76 + 32 + 4 * 70, height = 70; if (p.y >= top && p.y < top + height) { main->selectMidiTrack(); openLibertyMidiEditor(*main); } }
private: bool registered = true;
};

MidiEditorMouseListener globalMidiEditorMouseListener;
std::vector<std::unique_ptr<MidiEditorWindow>> midiEditorWindows;
}

void openLibertyMidiEditor(MainComponent& owner)
{
    for (auto& w : midiEditorWindows) if (w != nullptr && w->isVisible()) { w->toFront(true); return; }
    midiEditorWindows.push_back(std::make_unique<MidiEditorWindow>(owner));
}

void shutdownLibertyMidiEditor()
{
    midiEditorWindows.clear();
    globalMidiEditorMouseListener.shutdown();
}
