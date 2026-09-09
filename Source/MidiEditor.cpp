#include "MidiEditor.h"
#include "MainComponent.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace
{
constexpr int lowestKey = 21;
constexpr int pianoKeyWidth = 72;
constexpr int rulerHeight = 30;
constexpr int keyHeight = 20;
constexpr int visibleKeys = 40;
constexpr std::int64_t gridTicks = MidiEngine::ticksPerQuarterNote / 4;

const char* noteName(int midiNote)
{
    static constexpr const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[midiNote % 12];
}

bool isBlackKey(int midiNote)
{
    switch (midiNote % 12)
    {
        case 1: case 3: case 6: case 8: case 10: return true;
        default: return false;
    }
}

class PianoRoll final : public juce::Component
{
public:
    explicit PianoRoll(MainComponent& ownerIn) : owner(ownerIn)
    {
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0b0d10));
        const auto bounds = getLocalBounds();
        const auto grid = juce::Rectangle<int>(pianoKeyWidth, rulerHeight, bounds.getWidth() - pianoKeyWidth, bounds.getHeight() - rulerHeight);
        g.setColour(juce::Colour(0xff15181d)); g.fillRect(juce::Rectangle<int>(0, 0, pianoKeyWidth, rulerHeight));
        g.setColour(juce::Colour(0xff20242b)); g.fillRect(juce::Rectangle<int>(pianoKeyWidth, 0, grid.getWidth(), rulerHeight));

        const double pixelsPerTick = 0.12;
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        for (std::int64_t tick = 0; tick <= ticksPerMeasure * 32; tick += gridTicks)
        {
            const auto x = grid.getX() + static_cast<int>(std::llround(static_cast<double>(tick) * pixelsPerTick));
            if (x >= grid.getRight()) break;
            const bool measure = ticksPerMeasure > 0 && tick % ticksPerMeasure == 0;
            const bool beat = tick % MidiEngine::ticksPerQuarterNote == 0;
            g.setColour(measure ? juce::Colour(0xff4b525c) : beat ? juce::Colour(0xff343a43) : juce::Colour(0xff252a31));
            g.drawVerticalLine(x, static_cast<float>(rulerHeight), static_cast<float>(bounds.getBottom()));
        }

        g.setColour(juce::Colour(0xff747b85)); g.setFont(juce::Font(10.0f));
        for (int measure = 0; measure < 32; ++measure)
        {
            const auto tick = static_cast<std::int64_t>(measure) * juce::jmax<std::int64_t>(1, ticksPerMeasure);
            const auto x = grid.getX() + static_cast<int>(std::llround(static_cast<double>(tick) * pixelsPerTick));
            if (x >= grid.getRight()) break;
            g.drawText(juce::String(measure + 1), x + 5, 7, 36, 16, juce::Justification::left);
        }

        for (int row = 0; row < visibleKeys; ++row)
        {
            const int note = lowestKey + row;
            const int y = rulerHeight + (visibleKeys - 1 - row) * keyHeight;
            const bool black = isBlackKey(note);
            const auto key = juce::Rectangle<int>(0, y, pianoKeyWidth, keyHeight);
            g.setColour(black ? juce::Colour(0xff171a1f) : juce::Colour(0xffd6d9de)); g.fillRect(key);
            g.setColour(black ? juce::Colour(0xff303640) : juce::Colour(0xff777d86)); g.drawRect(key, 1);
            if (!black)
            {
                g.setColour(juce::Colour(0xff2b3037)); g.setFont(juce::Font(9.0f));
                g.drawText(juce::String(noteName(note)) + juce::String(note / 12 - 1), key.reduced(5, 0), juce::Justification::centredLeft);
            }
        }

        for (const auto& note : owner.getMidiEngine().getNotesCopy())
        {
            if (note.pitch < lowestKey || note.pitch >= lowestKey + visibleKeys) continue;
            const int row = static_cast<int>(note.pitch) - lowestKey;
            const int y = rulerHeight + (visibleKeys - 1 - row) * keyHeight + 2;
            const int x = grid.getX() + static_cast<int>(std::llround(static_cast<double>(note.startTick) * pixelsPerTick));
            const int width = juce::jmax(8, static_cast<int>(std::llround(static_cast<double>(note.lengthTicks) * pixelsPerTick)));
            auto noteRect = juce::Rectangle<int>(x + 1, y, width - 2, keyHeight - 4);
            if (noteRect.getRight() <= grid.getX() || noteRect.getX() >= grid.getRight()) continue;
            g.setColour(juce::Colour(0xff4f82ff)); g.fillRoundedRectangle(noteRect.toFloat(), 3.0f);
            g.setColour(juce::Colour(0xff9fd8f5)); g.drawRoundedRectangle(noteRect.toFloat(), 3.0f, 1.0f);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.mods.isRightButtonDown() || event.y < rulerHeight || event.x < pianoKeyWidth) return;
        const int row = juce::jlimit(0, visibleKeys - 1, (event.y - rulerHeight) / keyHeight);
        const int pitch = lowestKey + (visibleKeys - 1 - row);
        const double pixelsPerTick = 0.12;
        const auto rawTick = static_cast<std::int64_t>(std::llround((event.x - pianoKeyWidth) / pixelsPerTick));
        const auto startTick = MidiEngine::quantizeTick(juce::jmax<std::int64_t>(0, rawTick), gridTicks);
        auto& midi = owner.getMidiEngine();
        for (const auto& note : midi.getNotesCopy())
        {
            if (note.pitch == pitch && note.startTick == startTick)
            {
                midi.removeNoteAt(startTick, pitch, note.channel); repaint(); owner.repaint(); return;
            }
        }
        if (midi.addNote(startTick, MidiEngine::ticksPerQuarterNote, pitch, 100, 1)) { repaint(); owner.repaint(); }
    }

private:
    MainComponent& owner;
};

class MidiEditorWindow final : public juce::DocumentWindow
{
public:
    explicit MidiEditorWindow(MainComponent& owner)
        : DocumentWindow("Liberty - MIDI 1", juce::Colour(0xff0b0d10), DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true); setContentOwned(new PianoRoll(owner), true); setResizable(true, true);
        setResizeLimits(900, 500, 1800, 1000); centreWithSize(1200, 760); setVisible(true); toFront(true);
    }
    void closeButtonPressed() override { setVisible(false); }
};

class MidiEditorMouseListener final : public juce::MouseListener
{
public:
    MidiEditorMouseListener() { juce::Desktop::getInstance().addGlobalMouseListener(this); }
    ~MidiEditorMouseListener() override
    {
        juce::Desktop::getInstance().removeGlobalMouseListener(this);
    }
    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        auto* main = dynamic_cast<MainComponent*>(event.eventComponent);
        if (main == nullptr) return;
        const auto position = event.getEventRelativeTo(main).getPosition();
        constexpr int midiRowTop = 76 + 32 + (4 * 70);
        constexpr int midiRowHeight = 70;
        if (position.y >= midiRowTop && position.y < midiRowTop + midiRowHeight) openLibertyMidiEditor(*main);
    }
};

MidiEditorMouseListener globalMidiEditorMouseListener;
std::vector<std::unique_ptr<MidiEditorWindow>> midiEditorWindows;
}

void openLibertyMidiEditor(MainComponent& owner)
{
    for (auto& window : midiEditorWindows)
        if (window != nullptr && window->isVisible()) { window->toFront(true); return; }
    midiEditorWindows.push_back(std::make_unique<MidiEditorWindow>(owner));
}
