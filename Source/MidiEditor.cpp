#include "MidiEditor.h"
#include "MainComponent.h"
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
constexpr double pixelsPerTick = 0.12;

const char* noteName(int n)
{
    static constexpr const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[n % 12];
}

bool isBlackKey(int n) { return n % 12 == 1 || n % 12 == 3 || n % 12 == 6 || n % 12 == 8 || n % 12 == 10; }

class PianoRoll final : public juce::Component
{
public:
    explicit PianoRoll(MainComponent& o) : owner(o) { setWantsKeyboardFocus(true); setMouseCursor(juce::MouseCursor::CrosshairCursor); }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0b0d10)); const auto b = getLocalBounds();
        const auto grid = juce::Rectangle<int>(pianoKeyWidth, rulerHeight, b.getWidth() - pianoKeyWidth, b.getHeight() - rulerHeight);
        g.setColour(juce::Colour(0xff15181d)); g.fillRect(0, 0, pianoKeyWidth, rulerHeight); g.setColour(juce::Colour(0xff20242b)); g.fillRect(grid.getX(), 0, grid.getWidth(), rulerHeight);
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        for (std::int64_t t = 0; t <= ticksPerMeasure * 32; t += gridTicks)
        {
            const int x = grid.getX() + (int)std::llround((double)t * pixelsPerTick); if (x >= grid.getRight()) break;
            const bool measure = ticksPerMeasure > 0 && t % ticksPerMeasure == 0; const bool beat = t % MidiEngine::ticksPerQuarterNote == 0;
            g.setColour(measure ? juce::Colour(0xff4b525c) : beat ? juce::Colour(0xff343a43) : juce::Colour(0xff252a31)); g.drawVerticalLine(x, (float)rulerHeight, (float)b.getBottom());
        }
        g.setColour(juce::Colour(0xff747b85)); g.setFont(juce::Font(10.0f));
        for (int m = 0; m < 32; ++m)
        {
            const int x = grid.getX() + (int)std::llround((double)m * (double)juce::jmax<std::int64_t>(1, ticksPerMeasure) * pixelsPerTick); if (x >= grid.getRight()) break;
            g.drawText(juce::String(m + 1), x + 5, 7, 36, 16, juce::Justification::left);
        }
        for (int row = 0; row < visibleKeys; ++row)
        {
            const int note = lowestKey + row, y = rulerHeight + (visibleKeys - 1 - row) * keyHeight; const auto key = juce::Rectangle<int>(0, y, pianoKeyWidth, keyHeight); const bool black = isBlackKey(note);
            g.setColour(black ? juce::Colour(0xff171a1f) : juce::Colour(0xffd6d9de)); g.fillRect(key); g.setColour(black ? juce::Colour(0xff303640) : juce::Colour(0xff777d86)); g.drawRect(key, 1);
            if (!black) { g.setColour(juce::Colour(0xff2b3037)); g.setFont(juce::Font(9.0f)); g.drawText(juce::String(noteName(note)) + juce::String(note / 12 - 1), key.reduced(5, 0), juce::Justification::centredLeft); }
        }
        for (const auto& n : owner.getMidiEngine().getNotesCopy())
        {
            if (n.pitch < lowestKey || n.pitch >= lowestKey + visibleKeys) continue;
            const int y = rulerHeight + (visibleKeys - 1 - ((int)n.pitch - lowestKey)) * keyHeight + 2; const int x = grid.getX() + (int)std::llround((double)n.startTick * pixelsPerTick); const int w = juce::jmax(8, (int)std::llround((double)n.lengthTicks * pixelsPerTick));
            const auto r = juce::Rectangle<int>(x + 1, y, w - 2, keyHeight - 4); if (r.getRight() <= grid.getX() || r.getX() >= grid.getRight()) continue;
            g.setColour(juce::Colour(0xff4f82ff)); g.fillRoundedRectangle(r.toFloat(), 3.0f); g.setColour(juce::Colour(0xff9fd8f5)); g.drawRoundedRectangle(r.toFloat(), 3.0f, 1.0f);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown() || e.y < rulerHeight || e.x < pianoKeyWidth) return; const int pitch = pitchFromY(e.y); auto& midi = owner.getMidiEngine();
        for (const auto& n : midi.getNotesCopy())
        {
            const int x = pianoKeyWidth + (int)std::llround((double)n.startTick * pixelsPerTick); const int y = rulerHeight + (visibleKeys - 1 - ((int)n.pitch - lowestKey)) * keyHeight + 2; const int w = juce::jmax(8, (int)std::llround((double)n.lengthTicks * pixelsPerTick));
            if (n.pitch == pitch && juce::Rectangle<int>(x + 1, y, w - 2, keyHeight - 4).contains(e.getPosition())) { draggingNote = true; dragMoved = false; dragStartTick = n.startTick; dragPitch = n.pitch; dragChannel = n.channel; return; }
        }
        if (midi.addNote(tickFromX(e.x), MidiEngine::ticksPerQuarterNote, pitch, 100, 1)) { repaint(); owner.repaint(); }
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!draggingNote) return; const auto newTick = tickFromX(e.x); const int newPitch = pitchFromY(e.y); if (newTick == dragStartTick && newPitch == dragPitch) return;
        if (owner.getMidiEngine().moveNote(dragStartTick, dragPitch, dragChannel, newTick, newPitch)) { dragStartTick = newTick; dragPitch = newPitch; dragMoved = true; repaint(); owner.repaint(); }
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (!draggingNote) return; if (!dragMoved) owner.getMidiEngine().removeNoteAt(dragStartTick, dragPitch, dragChannel); draggingNote = false; dragMoved = false; repaint(); owner.repaint();
    }
private:
    int pitchFromY(int y) const noexcept { const int row = juce::jlimit(0, visibleKeys - 1, (y - rulerHeight) / keyHeight); return lowestKey + visibleKeys - 1 - row; }
    std::int64_t tickFromX(int x) const noexcept { const auto raw = (std::int64_t)std::llround((x - pianoKeyWidth) / pixelsPerTick); return MidiEngine::quantizeTick(juce::jmax<std::int64_t>(0, raw), gridTicks); }
    MainComponent& owner; bool draggingNote = false, dragMoved = false; std::int64_t dragStartTick = 0; int dragPitch = 60, dragChannel = 1;
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
    void mouseDown(const juce::MouseEvent& e) override
    {
        auto* main = dynamic_cast<MainComponent*>(e.eventComponent); if (main == nullptr) return; const auto p = e.getEventRelativeTo(main).getPosition(); constexpr int top = 76 + 32 + 4 * 70, height = 70; if (p.y >= top && p.y < top + height) main->selectMidiTrack();
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        auto* main = dynamic_cast<MainComponent*>(e.eventComponent); if (main == nullptr) return; const auto p = e.getEventRelativeTo(main).getPosition(); constexpr int top = 76 + 32 + 4 * 70, height = 70; if (p.y >= top && p.y < top + height) { main->selectMidiTrack(); openLibertyMidiEditor(*main); }
    }
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
