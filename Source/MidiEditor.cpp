#include "MidiEditor.h"
#include "MainComponent.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>

float getLibertyPianoHorizontalZoom() noexcept;
std::int64_t getLibertySnapStepTicks() noexcept;
float getLibertyPianoVerticalZoom() noexcept;
int getLibertyTrackRowHeight() noexcept;
int getLibertyActiveTool();
void setLibertyActiveTool(int);
void synchronizeLibertyPianoRollPlayback(MainComponent&);

namespace
{
constexpr int lowestKey = 21;
constexpr int pianoKeyWidth = 72;
constexpr int editorToolbarHeight = 34;
constexpr int rulerHeight = 64;
constexpr int baseKeyHeight = 20;
constexpr int visibleKeys = 40;
constexpr int velocityLaneHeight = 92;
constexpr std::int64_t gridTicks = MidiEngine::ticksPerQuarterNote / 4;
std::int64_t activePianoSnapTicks() noexcept { return getLibertySnapStepTicks(); }
constexpr int resizeEdgePixels = 12;

int currentKeyHeight() noexcept
{
    return juce::jlimit(10, 50, (int)std::lround((float)baseKeyHeight * getLibertyPianoVerticalZoom()));
}

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
        // Obsidian deep graphite aligns the velocity editor with the dock surfaces.
        g.fillAll(juce::Colour(0xff101821));
        g.setColour(juce::Colour(0xff070b10));
        g.fillRect(0, 0, getWidth(), 2);
        // Match the velocity header divider to the continuous keyboard/grid seam.
        g.setColour(juce::Colour(0xff40515f));
        g.drawHorizontalLine(2, 0.0f, (float)getWidth());
        // Clear turquoise section accent without changing the velocity lane geometry.
        g.setColour(juce::Colour(0xff54d6dc).withAlpha(0.80f));
        g.fillRect(0, 2, juce::jmin(pianoKeyWidth, getWidth()), 1);

        // Obsidian velocity-lane heading matches the clear dock title hierarchy.
        g.setColour(juce::Colour(0xffb8e8ec));
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText("VELOCITY", 8, 8, pianoKeyWidth - 16, 16, juce::Justification::centredLeft);

        const auto gridWidth = juce::jmax(1, getWidth() - pianoKeyWidth);
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(
            juce::jmax<std::int64_t>(1, ticksPerMeasure),
            MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        const auto pixelsPerTick = (static_cast<double>(gridWidth - 2) / static_cast<double>(clipLengthTicks))
            * (double)getLibertyPianoHorizontalZoom();

        for (const auto& note : owner.getMidiEngine().getNotesCopy())
        {
            if (note.startTick < 0 || note.startTick >= clipLengthTicks)
                continue;

            const int x = pianoKeyWidth + (int)std::llround((double)note.startTick * pixelsPerTick);
            const int nextX = pianoKeyWidth + (int)std::llround((double)(note.startTick + note.lengthTicks) * pixelsPerTick);
            const int barWidth = juce::jmax(4, juce::jmin(18, nextX - x));
            const int usableHeight = juce::jmax(1, getHeight() - 28);
            const int barHeight = juce::jlimit(2, usableHeight,
                                                (int)std::llround((double)note.velocity / 127.0 * usableHeight));
            const auto bar = juce::Rectangle<int>(x, getHeight() - barHeight - 8, barWidth, barHeight);

            // Match velocity bar underlay to the Obsidian inactive MIDI note fill.
            g.setColour(juce::Colour(0xff376d86));
            g.fillRoundedRectangle(bar.toFloat(), 2.0f);
            g.setColour(juce::Colour(0xff54d6dc));
            g.fillRoundedRectangle(bar.toFloat().reduced(1.0f, 0.0f), 2.0f);
            // Slightly brighter Obsidian turquoise outline defines each velocity bar.
            g.setColour(juce::Colour(0xffb6f2f2).withAlpha(0.95f));
            g.drawRoundedRectangle(bar.toFloat(), 2.0f, 1.0f);
        }
        // Continue the keyboard/grid seam through the velocity lane.
        g.setColour(juce::Colour(0xff40515f));
        g.fillRect(pianoKeyWidth - 1, 3, 1, juce::jmax(0, getHeight() - 3));
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!e.mods.isLeftButtonDown() || e.x < pianoKeyWidth)
            return; // Velocity lane remains directly editable in every tool, as before the palette.

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
        const auto pixelsPerTick = (static_cast<double>(gridWidth - 2) / static_cast<double>(clipLengthTicks))
            * (double)getLibertyPianoHorizontalZoom();

        const MidiEngine::NoteEvent* best = nullptr;
        double bestDistance = std::numeric_limits<double>::max();

        for (const auto& note : owner.getMidiEngine().getNotesCopy())
        {
            if (note.startTick < 0 || note.startTick >= clipLengthTicks)
                continue;

            const int noteX = pianoKeyWidth + (int)std::llround((double)note.startTick * pixelsPerTick);
            const int nextX = pianoKeyWidth + (int)std::llround((double)(note.startTick + note.lengthTicks) * pixelsPerTick);
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
            (int)std::llround((double)(laneBottom - clampedY) / (double)usableHeight * 127.0));

        if (owner.getMidiEngine().setNoteVelocity(editStartTick, editPitch, editChannel, velocity))
        {
            owner.updateMidiClipTiming(); synchronizeLibertyPianoRollPlayback(owner);
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
        // Shared Piano Roll component: identical tools in the dock and detached editor.
        static const char* glyphs[] = { "↖", "✎", "✂", "⌫", "↔", "◇", "▥", "▧", "◉" };
        static const char* names[] = { "Sélection", "Crayon", "Ciseaux", "Gomme",
            "Redimensionner", "Étirer", "Vélocité", "Sélection rectangulaire", "Muet" };
        for (int i = 0; i < 9; ++i)
        {
            auto& button = toolButtons[(size_t)i];
            button.setButtonText(juce::String::fromUTF8(glyphs[i]));
            button.setTooltip(juce::String::fromUTF8(names[i]) + " (" + juce::String(i + 1) + ")");
            button.setClickingTogglesState(false);
            button.onClick = [this, i] { setLibertyActiveTool(i); refreshToolButtons(); };
            addAndMakeVisible(button);
        }
        quantizeButton.setButtonText("Q");
        quantizeButton.setTooltip("Quantifier les notes sélectionnées sur la grille SNAP (Q)");
        quantizeButton.onClick = [this] { quantizeSelection(); };
        addAndMakeVisible(quantizeButton);
        selectAllButton.setButtonText("TOUT");
        selectAllButton.setTooltip("Sélectionner toutes les notes MIDI (Cmd/Ctrl + A)");
        selectAllButton.onClick = [this] { selectAllNotes(); };
        addAndMakeVisible(selectAllButton);
        velocityValue.setInputRestrictions(3, "0123456789");
        velocityValue.setJustification(juce::Justification::centred);
        velocityValue.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff233746));
        velocityValue.setColour(juce::TextEditor::textColourId, juce::Colour(0xffffdf99));
        velocityValue.setTooltip("Vélocité MIDI (1 à 127) : Entrée pour appliquer aux notes sélectionnées");
        velocityValue.onReturnKey = [this] { applyVelocityValue(); };
        velocityValue.onEscapeKey = [this] { velocityValue.giveAwayKeyboardFocus(); };
        velocityValue.setVisible(false);
        addAndMakeVisible(velocityValue);
        startTimerHz(30);
    }

    void applyVelocityValue()
    {
        auto& midi = owner.getMidiEngine();
        const auto selected = midi.getSelectedNotesCopy();
        if (selected.empty()) return;
        const int velocity = juce::jlimit(1, 127, velocityValue.getText().getIntValue());
        auto notes = midi.getNotesCopy();
        for (auto& note : notes)
            if (midi.isNoteSelected(note))
                note.velocity = static_cast<std::uint8_t>(velocity);
        if (midi.replaceNotes(notes))
        {
            midi.setSelectedNotes(selected);
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint(); owner.repaint();
        }
        velocityValue.setText(juce::String(velocity), juce::dontSendNotification);
        velocityValue.giveAwayKeyboardFocus();
    }

    void selectAllNotes()
    {
        auto& midi = owner.getMidiEngine();
        midi.setSelectedNotes(midi.getNotesCopy());
        repaint();
    }

    void quantizeSelection()
    {
        auto& midi = owner.getMidiEngine();
        const auto grid = activePianoSnapTicks();
        if (grid <= 0 || !midi.hasSelectedNote()) return;
        if (midi.quantizeSelectedNotes(grid))
        {
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint(); owner.repaint();
        }
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'A')
        { selectAllNotes(); return true; }
        if (key.getKeyCode() == 'Q' && !key.getModifiers().isCommandDown())
        { quantizeSelection(); return true; }
        if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
        {
            if (owner.getMidiEngine().deleteSelectedNotes())
            { owner.updateMidiClipTiming(); synchronizeLibertyPianoRollPlayback(owner); repaint(); owner.repaint(); }
            return true;
        }
        return false;
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (e.mods.isCommandDown() || e.mods.isCtrlDown() || e.mods.isAltDown()) return;
        const float delta = std::abs(wheel.deltaY) >= std::abs(wheel.deltaX) ? wheel.deltaY : wheel.deltaX;
        if (std::abs(delta) < 0.0001f) return;
        // Preserve both independently validated wheel interactions:
        // keyboard column = pitch scroll; note grid = horizontal time zoom.
        if (e.x < pianoKeyWidth)
        {
            const int steps = juce::jmax(1, (int) std::lround(std::abs(delta) * 6.0f));
            lowestVisibleKey = juce::jlimit(0, 128 - visibleKeys,
                lowestVisibleKey + (delta > 0.0f ? steps : -steps));
        }
        else
        {
            localGridZoom = juce::jlimit(0.40f, 8.0f,
                localGridZoom * (delta > 0.0f ? 1.12f : 1.0f / 1.12f));
            velocityLane.repaint();
        }
        repaint();
    }

    void refreshToolButtons()
    {
        for (int i = 0; i < 9; ++i)
        {
            auto& button = toolButtons[(size_t)i];
            const bool selected = getLibertyActiveTool() == i;
            button.setColour(juce::TextButton::buttonColourId,
                             selected ? juce::Colour(0xff315b69) : juce::Colour(0xff202a34));
            button.setColour(juce::TextButton::textColourOffId,
                             selected ? juce::Colour(0xfff3d48b) : juce::Colour(0xffd7e6ee));
        }
    }

    void resized() override
    {
        const int buttonWidth = 32;
        for (int i = 0; i < 9; ++i)
            toolButtons[(size_t)i].setBounds(pianoKeyWidth + 8 + i * 35, 3, buttonWidth, editorToolbarHeight - 6);
        quantizeButton.setBounds(pianoKeyWidth + 8 + 9 * 35 + 8, 3, 36, editorToolbarHeight - 6);
        selectAllButton.setBounds(pianoKeyWidth + 8 + 9 * 35 + 48, 3, 52, editorToolbarHeight - 6);
        // Keep the editable value inside the visible information line, not at the far-right edge.
        const int velocityX = pianoKeyWidth + 320;
        velocityValue.setBounds(velocityX, editorToolbarHeight + 3,
                                juce::jmax(0, juce::jmin(56, getWidth() - velocityX - 8)),
                                juce::jmax(16, rulerHeight - editorToolbarHeight - 6));
        velocityLane.setBounds(0, juce::jmax(rulerHeight, getHeight() - velocityLaneHeight),
                               getWidth(), juce::jmin(velocityLaneHeight, getHeight()));
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0b0d10));
        g.setColour(juce::Colour(0xff151f28));
        g.fillRect(0, 0, getWidth(), editorToolbarHeight);
        g.setColour(juce::Colour(0xff3b5262));
        g.fillRect(0, editorToolbarHeight - 1, getWidth(), 1);
        // Cubase-inspired MIDI information line, fitted into the existing ruler header.
        const auto selectedNotes = owner.getMidiEngine().getSelectedNotesCopy();
        const int infoY = editorToolbarHeight + 2;
        g.setColour(juce::Colour(0xff17242e));
        g.fillRect(pianoKeyWidth, infoY, juce::jmax(0, getWidth() - pianoKeyWidth), rulerHeight - infoY);
        g.setColour(juce::Colour(0xff405767));
        g.drawHorizontalLine(rulerHeight - 1, (float)pianoKeyWidth, (float)getWidth());
        g.setFont(juce::Font(11.0f));
        juce::String info;
        if (selectedNotes.empty())
            info = "MIDI  |  Aucune note sélectionnée";
        else if (selectedNotes.size() > 1)
            info = "MIDI  |  " + juce::String((int) selectedNotes.size()) + " notes sélectionnées";
        else
        {
            const auto& n = selectedNotes.front();
            const auto beat = MidiEngine::ticksPerQuarterNote;
            const auto measure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
            const auto barNumber = measure > 0 ? n.startTick / measure + 1 : 1;
            const auto beatNumber = measure > 0 ? (n.startTick % measure) / beat + 1 : 1;
            info = "Mes. " + juce::String(barNumber)
                 + "." + juce::String(beatNumber)
                 + "  Note " + juce::MidiMessage::getMidiNoteName((int)n.pitch, true, true, 3)
                 + "  Durée " + juce::String(n.lengthTicks);
        }
        g.setColour(juce::Colour(0xffd4e5ed));
        g.drawFittedText(info, pianoKeyWidth + 9, infoY, juce::jmax(0, juce::jmin(258, getWidth() - pianoKeyWidth - 14)),
                         rulerHeight - infoY - 1, juce::Justification::centredLeft, 1);
        if (!selectedNotes.empty() && getWidth() > pianoKeyWidth + 390)
        {
            g.setColour(juce::Colour(0xffffdf99));
            g.drawText("Vél.", pianoKeyWidth + 275, infoY, 42, rulerHeight - infoY - 1, juce::Justification::centredRight);
            g.setColour(juce::Colour(0xffd4e5ed));
            g.drawText("Canal " + juce::String((int)selectedNotes.front().channel)
                       + (selectedNotes.front().muted ? "  MUET" : ""),
                       pianoKeyWidth + 386, infoY, juce::jmax(0, getWidth() - pianoKeyWidth - 392),
                       rulerHeight - infoY - 1, juce::Justification::centredLeft);
        }
        const auto b = getLocalBounds();
        const auto velocityAreaY = juce::jmax(rulerHeight, b.getBottom() - velocityLaneHeight);
        const auto grid = juce::Rectangle<int>(pianoKeyWidth, rulerHeight, b.getWidth() - pianoKeyWidth, juce::jmax(1, velocityAreaY - rulerHeight));
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(juce::jmax<std::int64_t>(1, ticksPerMeasure), MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        const double pixelsPerTick = getPixelsPerTick();
        const int keyHeight = currentKeyHeight();

        // Obsidian keyboard/ruler corner mirrors the arranger section hierarchy.
        g.setColour(juce::Colour(0xff1d2a35)); g.fillRect(0, 0, pianoKeyWidth, rulerHeight);
        // Preserve the MIDI information line: only the lower ruler is repainted.
        g.setColour(juce::Colour(0xff141e28));
        g.fillRect(grid.getX(), rulerHeight - 1, grid.getWidth(), 1);
        for (std::int64_t t = 0; t <= clipLengthTicks; t += juce::jmax<std::int64_t>(1, activePianoSnapTicks() > 0 ? activePianoSnapTicks() : gridTicks))
        {
            const int x = grid.getX() + (int)std::llround((double)t * pixelsPerTick);
            if (x > grid.getRight()) break;
            const bool measure = ticksPerMeasure > 0 && t % ticksPerMeasure == 0;
            const bool beat = t % MidiEngine::ticksPerQuarterNote == 0;
            // Obsidian Piano Roll: measure lines lead, beats and subdivisions recede.
            g.setColour(measure ? juce::Colour(0xff688996) : beat ? juce::Colour(0xff344452) : juce::Colour(0xff1c2832));
            g.drawVerticalLine(x, (float)rulerHeight, (float)grid.getBottom());
            if (measure)
            {
                g.setColour(juce::Colour(0xff54d6dc).withAlpha(0.55f));
                g.fillRect(x, rulerHeight, 1, 6);
            }
        }
        // Measure numbers stay crisp against the dark Obsidian ruler.
        g.setColour(juce::Colour(0xffdceef1)); g.setFont(juce::Font(10.0f));
        const auto measureCount = ticksPerMeasure > 0 ? (int)((clipLengthTicks + ticksPerMeasure - 1) / ticksPerMeasure) : 1;
        for (int m = 0; m < measureCount; ++m)
        {
            const auto tick = (std::int64_t)m * juce::jmax<std::int64_t>(1, ticksPerMeasure);
            const int x = grid.getX() + (int)std::llround((double)tick * pixelsPerTick);
            if (x > grid.getRight()) break;
            g.drawText(juce::String(m + 1), x + 5, rulerHeight - 13, 36, 12, juce::Justification::left);
        }
        for (int row = 0; row < visibleKeys; ++row)
        {
            const int note = lowestVisibleKey + row;
            const int y = rulerHeight + (visibleKeys - 1 - row) * keyHeight;
            const auto key = juce::Rectangle<int>(0, y, pianoKeyWidth, keyHeight);
            const bool black = isBlackKey(note);
            g.setColour(auditionPitch == note ? juce::Colour(0xff54d6dc) : (black ? juce::Colour(0xff111922) : juce::Colour(0xffdbe3ea))); g.fillRect(key);
            // Obsidian keyboard edges: clearer black-key outlines, softer white-key dividers.
            g.setColour(black ? juce::Colour(0xff48606d) : juce::Colour(0xff91a4b0)); g.drawRect(key, 1);
            // Darker Obsidian note labels stay legible on light piano keys.
            if (!black) { g.setColour(juce::Colour(0xff17242e)); g.setFont(juce::Font(9.0f)); g.drawText(juce::String(noteName(note)) + juce::String(note / 12 - 1), key.reduced(5, 0), juce::Justification::centredLeft); }
            // One horizontal pitch separator per piano key, aligned exactly
            // with the note row in the dock and in the floating editor.
            if (y >= rulerHeight && y < velocityAreaY)
            {
                g.setColour(juce::Colour(0xff344452));
                g.fillRect(grid.getX(), y, grid.getWidth(), 1);
            }
        }
        // Bottom boundary of the lowest pitch row.
        const int lowestRowBottom = rulerHeight + visibleKeys * keyHeight;
        if (lowestRowBottom < velocityAreaY)
        {
            g.setColour(juce::Colour(0xff344452));
            g.fillRect(grid.getX(), lowestRowBottom, grid.getWidth(), 1);
        }

        // Crisp Obsidian seam between keyboard and note grid; no geometry changes.
        g.setColour(juce::Colour(0xff40515f));
        g.fillRect(pianoKeyWidth - 1, 0, 1, velocityAreaY);

        const auto playbackTick = MidiEngine::secondsToTick(owner.getMidiEngine().getPlaybackPositionSeconds(), owner.getTempoBpm());
        const int playheadX = grid.getX() + (int)std::llround((double)playbackTick * pixelsPerTick);
        if (playheadX >= grid.getX() && playheadX <= grid.getRight())
        {
            // Soft Obsidian playhead glow remains readable over dense MIDI notes.
            g.setColour(juce::Colour(0xffe4a85e).withAlpha(0.26f));
            g.drawLine((float)playheadX, (float)rulerHeight, (float)playheadX, (float)grid.getBottom(), 4.0f);
            // Obsidian transport marker stays warm orange, with a softer idle state.
            g.setColour(owner.getMidiEngine().isPlaying() ? juce::Colour(0xffffc06b) : juce::Colour(0xffc99255));
            g.drawLine((float)playheadX, (float)rulerHeight, (float)playheadX, (float)grid.getBottom(), 1.5f);
            // Fixed Obsidian orange keeps the ruler playhead marker identifiable at rest.
            g.setColour(juce::Colour(0xffe4a85e));
            g.fillRoundedRectangle((float)playheadX - 3.0f, (float)rulerHeight - 5.0f, 6.0f, 5.0f, 1.5f);
        }
        for (const auto& n : owner.getMidiEngine().getNotesCopy())
        {
            if (n.pitch < lowestVisibleKey || n.pitch >= lowestVisibleKey + visibleKeys || n.startTick >= clipLengthTicks) continue;
            const int y = rulerHeight + (visibleKeys - 1 - ((int)n.pitch - lowestVisibleKey)) * keyHeight + 2;
            const int x = grid.getX() + (int)std::llround((double)n.startTick * pixelsPerTick);
            const auto visibleLengthTicks = juce::jmin(n.lengthTicks, clipLengthTicks - n.startTick);
            const int w = juce::jmax(8, (int)std::llround((double)visibleLengthTicks * pixelsPerTick));
            const auto r = juce::Rectangle<int>(x + 1, y, w - 2, juce::jmax(4, keyHeight - 4));
            if (r.getRight() <= grid.getX() || r.getX() >= grid.getRight()) continue;
            const auto noteEnd = n.startTick + n.lengthTicks;
            const bool active = playbackTick >= n.startTick && playbackTick < noteEnd && owner.getMidiEngine().isPlaying();
            // Obsidian MIDI notes: brighter idle blue, clear turquoise playback state.
            const bool selected = owner.getMidiEngine().isNoteSelected(n);
            g.setColour(n.muted ? juce::Colour(0xff46505a) : selected ? juce::Colour(0xffe6aa55) : active ? juce::Colour(0xff54d6dc) : juce::Colour(0xff376d86));
            g.fillRoundedRectangle(r.toFloat(), 3.0f);
            // Subtle Obsidian highlight adds definition to idle MIDI notes.
            g.setColour(juce::Colour(0xffe8eff7).withAlpha(active ? 0.24f : 0.16f));
            g.fillRoundedRectangle(r.toFloat().withHeight(juce::jmin(4.0f, (float)r.getHeight())), 2.0f);
            // Defined idle-note outlines improve separation without changing active playback styling.
            g.setColour(n.muted ? juce::Colour(0xff75808b) : selected ? juce::Colour(0xffffe4ac) : active ? juce::Colour(0xffb6f2f2) : juce::Colour(0xff91cde3));
            g.drawRoundedRectangle(r.toFloat(), 3.0f, selected ? 2.2f : active ? 1.5f : 1.0f);
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

    // Both the embedded dock and the floating editor use this PianoRoll.
    // Handle note insertion on the editor itself rather than relying on a
    // separate selection overlay that may only exist in floating windows.
    void paintOverChildren(juce::Graphics& g) override
    {
        if (!selectingRectangle) return;
        const auto area = juce::Rectangle<int>::leftTopRightBottom(juce::jmin(rectangleStart.x, rectangleEnd.x), juce::jmin(rectangleStart.y, rectangleEnd.y), juce::jmax(rectangleStart.x, rectangleEnd.x), juce::jmax(rectangleStart.y, rectangleEnd.y));
        g.setColour(juce::Colour(0xff56bfd9).withAlpha(0.14f)); g.fillRect(area);
        g.setColour(juce::Colour(0xff56bfd9)); g.drawRect(area, 1);
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (!e.mods.isLeftButtonDown() || e.x < pianoKeyWidth
            || e.y < rulerHeight || isInVelocityLane(e.y))
            return;
        const int pitch = pitchFromY(e.y);
        const auto tick = tickFromX(e.x);
        if (owner.getMidiEngine().addNote(tick, MidiEngine::ticksPerQuarterNote, pitch, 100, 1))
        {
            owner.updateMidiClipTiming(); synchronizeLibertyPianoRollPlayback(owner);
            repaint();
            owner.repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isLeftButtonDown() && e.x >= 0 && e.x < pianoKeyWidth
            && e.y >= rulerHeight && !isInVelocityLane(e.y))
        {
            auditionPitch = pitchFromY(e.y);
            const int lane = owner.getSelectedPianoInstrumentLane();
            if (lane >= 0 && lane < owner.getInstrumentTrackCount())
            {
                auditionLane = lane;
                owner.auditionPianoKey(lane, auditionPitch, true);
            }
            repaint();
            return;
        }
        if (e.mods.isRightButtonDown() || e.y < rulerHeight || e.x < pianoKeyWidth) return;
        const int tool = getLibertyActiveTool();
        if (tool == 7) { selectingRectangle = true; rectangleStart = e.getPosition(); rectangleEnd = rectangleStart; repaint(); return; }

        const int pitch = pitchFromY(e.y);
        const int keyHeight = currentKeyHeight();
        auto& midi = owner.getMidiEngine();
        for (const auto& n : midi.getNotesCopy())
        {
            if (n.pitch < lowestVisibleKey || n.pitch >= lowestVisibleKey + visibleKeys) continue;
            const int x = pianoKeyWidth + (int)std::llround((double)n.startTick * getPixelsPerTick());
            const int y = rulerHeight + (visibleKeys - 1 - ((int)n.pitch - lowestVisibleKey)) * keyHeight + 2;
            const int w = juce::jmax(8, (int)std::llround((double)n.lengthTicks * getPixelsPerTick()));
            const auto noteRect = juce::Rectangle<int>(x + 1, y, w - 2, juce::jmax(4, keyHeight - 4));
            if (n.pitch == pitch && noteRect.contains(e.getPosition()))
            {
                if (tool == 8) // Non-destructive per-note MIDI mute.
                {
                    auto notes = midi.getNotesCopy();
                    const bool targetMute = !n.muted;
                    const bool group = midi.getNumSelectedNotes() > 1 && midi.isNoteSelected(n);
                    for (auto& note : notes)
                        if (group ? midi.isNoteSelected(note) :
                            (note.startTick == n.startTick && note.pitch == n.pitch && note.channel == n.channel))
                            note.muted = targetMute;
                    if (midi.replaceNotes(notes))
                    { owner.updateMidiClipTiming(); synchronizeLibertyPianoRollPlayback(owner); repaint(); owner.repaint(); }
                    return;
                }
                if (tool == 6) { editingVelocity = true; velocityNoteTick = n.startTick; velocityNotePitch = n.pitch; velocityNoteChannel = n.channel; velocityStartY = e.y; velocityStartValue = n.velocity; return; }
                if (tool == 2)
                {
                    const auto split = tickFromX(e.x);
                    if (split > n.startTick && split < n.startTick + n.lengthTicks)
                    {
                        auto notes = midi.getNotesCopy();
                        for (auto& part : notes)
                            if (part.startTick == n.startTick && part.pitch == n.pitch && part.channel == n.channel)
                            {
                                part.lengthTicks = split - n.startTick;
                                auto right = n; right.startTick = split; right.lengthTicks = n.startTick + n.lengthTicks - split;
                                notes.push_back(right);
                                break;
                            }
                        if (midi.replaceNotes(notes)) { owner.updateMidiClipTiming(); synchronizeLibertyPianoRollPlayback(owner); repaint(); owner.repaint(); }
                    }
                    return;
                }
                if (tool == 3) // ERASE / EFFACER
                {
                    if (midi.getNumSelectedNotes() > 1 && midi.isNoteSelected(n))
                    { if (midi.deleteSelectedNotes()) { owner.updateMidiClipTiming(); synchronizeLibertyPianoRollPlayback(owner); repaint(); owner.repaint(); } return; }
                    midi.selectNoteAt(n.startTick, n.pitch, n.channel);
                    midi.deleteSelectedNote();
                    owner.updateMidiClipTiming(); synchronizeLibertyPianoRollPlayback(owner); repaint(); owner.repaint();
                    return;
                }

                if (tool == 0 && midi.isNoteSelected(n) && midi.getNumSelectedNotes() > 1)
                {
                    draggingGroup = true;
                    groupDragTick = tickFromX(e.x);
                    groupDragPitch = pitchFromY(e.y);
                    return;
                }
                if (tool == 4) // RESIZE / REDIMENSIONNER: edge only
                {
                    const auto side = findResizeSide(e.position);
                    if (side == ResizeSide::none) return;
                    draggingNote = true; dragMoved = false; resizeSide = side;
                }
                else if (tool == 0) // SELECT / SELECTION: move without deleting on click
                {
                    draggingNote = true; dragMoved = false; resizeSide = ResizeSide::none;
                }
                else
                    return;

                dragStartTick = n.startTick; dragEndTick = n.startTick + n.lengthTicks;
                dragPitch = n.pitch; dragChannel = n.channel; originalLengthTicks = n.lengthTicks;
                setMouseCursor(resizeSide == ResizeSide::none ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::LeftRightResizeCursor);
                return;
            }
        }

        if (tool == 1) // DRAW / DESSINER
            if (midi.addNote(tickFromX(e.x), MidiEngine::ticksPerQuarterNote, pitch, 100, 1))
            { owner.updateMidiClipTiming(); synchronizeLibertyPianoRollPlayback(owner); repaint(); owner.repaint(); }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (draggingGroup)
        {
            const auto tick = tickFromX(e.x);
            const int pitch = pitchFromY(e.y);
            const auto dt = tick - groupDragTick;
            const int dp = pitch - groupDragPitch;
            if ((dt != 0 || dp != 0) && owner.getMidiEngine().moveSelectedNotesBy(dt, dp))
            {
                groupDragTick = tick; groupDragPitch = pitch;
                owner.updateMidiClipTiming(); synchronizeLibertyPianoRollPlayback(owner); repaint(); owner.repaint();
            }
            return;
        }
        if (selectingRectangle)
        {
            rectangleEnd = e.getPosition();
            const auto area = juce::Rectangle<int>::leftTopRightBottom(juce::jmin(rectangleStart.x, rectangleEnd.x), juce::jmin(rectangleStart.y, rectangleEnd.y), juce::jmax(rectangleStart.x, rectangleEnd.x), juce::jmax(rectangleStart.y, rectangleEnd.y));
            std::vector<MidiEngine::NoteEvent> selected;
            for (const auto& n : owner.getMidiEngine().getNotesCopy())
            {
                const int x = pianoKeyWidth + (int) std::llround(n.startTick * getPixelsPerTick());
                const int y = rulerHeight + (visibleKeys - 1 - ((int)n.pitch - lowestVisibleKey)) * currentKeyHeight() + 2;
                const int w = juce::jmax(8, (int) std::llround(n.lengthTicks * getPixelsPerTick()));
                if (area.intersects(juce::Rectangle<int>(x, y, w, currentKeyHeight() - 3))) selected.push_back(n);
            }
            owner.getMidiEngine().setSelectedNotes(selected);
            repaint(); return;
        }
        if (editingVelocity)
        {
            const int value = juce::jlimit(1, 127, velocityStartValue + (velocityStartY - e.y) * 2);
            if (owner.getMidiEngine().setNoteVelocity(velocityNoteTick, velocityNotePitch, velocityNoteChannel, value))
            { owner.updateMidiClipTiming(); synchronizeLibertyPianoRollPlayback(owner); repaint(); owner.repaint(); }
            return;
        }
        if (auditionPitch >= 0)
        {
            const int next = e.x >= 0 && e.x < pianoKeyWidth && e.y >= rulerHeight && !isInVelocityLane(e.y)
                ? pitchFromY(e.y) : -1;
            if (next != auditionPitch)
            {
                if (auditionLane >= 0)
                    owner.auditionPianoKey(auditionLane, auditionPitch, false);
                auditionPitch = next;
                if (next >= 0 && auditionLane >= 0)
                    owner.auditionPianoKey(auditionLane, next, true);
                repaint();
            }
            return;
        }
        if (!draggingNote) return;
        const auto mouseTick = tickFromX(e.x);
        if (resizeSide == ResizeSide::right)
        {
            const auto newLength = juce::jmax(juce::jmax<std::int64_t>(1, activePianoSnapTicks()), mouseTick - dragStartTick);
            if (newLength != originalLengthTicks && owner.getMidiEngine().setNoteLength(dragStartTick, dragPitch, dragChannel, newLength)) { originalLengthTicks = newLength; dragEndTick = dragStartTick + newLength; dragMoved = true; owner.updateMidiClipTiming(); synchronizeLibertyPianoRollPlayback(owner); repaint(); owner.repaint(); }
            return;
        }
        if (resizeSide == ResizeSide::left)
        {
            const auto maxStart = juce::jmax<std::int64_t>(0, dragEndTick - juce::jmax<std::int64_t>(1, activePianoSnapTicks()));
            const auto newStart = juce::jlimit<std::int64_t>(0, maxStart, mouseTick);
            const auto newLength = dragEndTick - newStart;
            if (newStart != dragStartTick && newLength >= gridTicks && owner.getMidiEngine().moveNote(dragStartTick, dragPitch, dragChannel, newStart, dragPitch) && owner.getMidiEngine().setNoteLength(newStart, dragPitch, dragChannel, newLength)) { dragStartTick = newStart; originalLengthTicks = newLength; dragMoved = true; owner.updateMidiClipTiming(); synchronizeLibertyPianoRollPlayback(owner); repaint(); owner.repaint(); }
            return;
        }
        const auto newTick = tickFromX(e.x); const int newPitch = pitchFromY(e.y);
        if (newTick == dragStartTick && newPitch == dragPitch) return;
        if (owner.getMidiEngine().moveNote(dragStartTick, dragPitch, dragChannel, newTick, newPitch)) { dragStartTick = newTick; dragPitch = newPitch; dragEndTick = newTick + originalLengthTicks; dragMoved = true; owner.updateMidiClipTiming(); synchronizeLibertyPianoRollPlayback(owner); repaint(); owner.repaint(); }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (draggingGroup) { draggingGroup = false; repaint(); return; }
        if (selectingRectangle) { selectingRectangle = false; repaint(); return; }
        if (editingVelocity) { editingVelocity = false; repaint(); return; }
        if (auditionPitch >= 0)
        {
            if (auditionLane >= 0)
                owner.auditionPianoKey(auditionLane, auditionPitch, false);
            auditionPitch = -1;
            auditionLane = -1;
            repaint();
            return;
        }
        if (!draggingNote) return;
        owner.updateMidiClipTiming(); synchronizeLibertyPianoRollPlayback(owner); draggingNote = false; dragMoved = false; resizeSide = ResizeSide::none; originalLengthTicks = 0; dragEndTick = 0;
        setMouseCursor(juce::MouseCursor::CrosshairCursor); repaint(); owner.repaint();
    }

private:
    std::array<juce::TextButton, 9> toolButtons;
    juce::TextButton quantizeButton, selectAllButton;
    juce::TextEditor velocityValue;
    // JUCE tooltips require a live TooltipWindow; setTooltip alone is not enough.
    juce::TooltipWindow tooltips { this, 550 };
    int lastDisplayedTool = -1;
    int lastCursorTool = -1;
    bool selectingRectangle = false, editingVelocity = false, draggingGroup = false;
    std::int64_t groupDragTick = 0;
    int groupDragPitch = 0;
    juce::Point<int> rectangleStart, rectangleEnd;
    std::int64_t velocityNoteTick = 0;
    int velocityNotePitch = 60, velocityNoteChannel = 1, velocityStartY = 0, velocityStartValue = 100;
    int lowestVisibleKey = lowestKey;
    float localGridZoom = 1.0f;
    int auditionPitch = -1;
    int auditionLane = -1;
    enum class ResizeSide { none, left, right };
    double getPixelsPerTick() const
    {
        const auto gridWidth = juce::jmax(1, getWidth() - pianoKeyWidth);
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(juce::jmax<std::int64_t>(1, ticksPerMeasure), MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        return (static_cast<double>(gridWidth - 2) / static_cast<double>(clipLengthTicks))
            * (double)getLibertyPianoHorizontalZoom() * (double)localGridZoom;
    }
    bool isInVelocityLane(int y) const noexcept { return y >= juce::jmax(rulerHeight, getHeight() - velocityLaneHeight) && y < getHeight(); }

    const MidiEngine::NoteEvent* findNoteAt(juce::Point<float> position) const
    {
        const auto pixelsPerTick = getPixelsPerTick();
        const int keyHeight = currentKeyHeight();
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(juce::jmax<std::int64_t>(1, ticksPerMeasure), MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        for (const auto& n : owner.getMidiEngine().getNotesCopy())
        {
            if (n.pitch < lowestVisibleKey || n.pitch >= lowestVisibleKey + visibleKeys || n.startTick >= clipLengthTicks) continue;
            const int x = pianoKeyWidth + (int)std::llround((double)n.startTick * pixelsPerTick);
            const int y = rulerHeight + (visibleKeys - 1 - ((int)n.pitch - lowestVisibleKey)) * keyHeight + 2;
            const auto visibleLengthTicks = juce::jmin(n.lengthTicks, clipLengthTicks - n.startTick);
            const int w = juce::jmax(8, (int)std::llround((double)visibleLengthTicks * pixelsPerTick));
            const auto noteRect = juce::Rectangle<int>(x + 1, y, w - 2, juce::jmax(4, keyHeight - 4));
            if (n.pitch == pitchFromY((int)position.y) && noteRect.contains((int)position.x, (int)position.y)) return &n;
        }
        return nullptr;
    }

    ResizeSide findResizeSide(juce::Point<float> position) const
    {
        if (position.y < rulerHeight || position.y >= getHeight() - velocityLaneHeight || position.x < pianoKeyWidth) return ResizeSide::none;
        const auto pixelsPerTick = getPixelsPerTick();
        const int keyHeight = currentKeyHeight();
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(juce::jmax<std::int64_t>(1, ticksPerMeasure), MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        ResizeSide bestSide = ResizeSide::none; float bestDistance = (float)resizeEdgePixels + 1.0f;
        for (const auto& n : owner.getMidiEngine().getNotesCopy())
        {
            if (n.pitch < lowestVisibleKey || n.pitch >= lowestVisibleKey + visibleKeys || n.startTick >= clipLengthTicks) continue;
            const int x = pianoKeyWidth + (int)std::llround((double)n.startTick * pixelsPerTick);
            const int y = rulerHeight + (visibleKeys - 1 - ((int)n.pitch - lowestVisibleKey)) * keyHeight + 2;
            const auto visibleLengthTicks = juce::jmin(n.lengthTicks, clipLengthTicks - n.startTick);
            const int w = juce::jmax(8, (int)std::llround((double)visibleLengthTicks * pixelsPerTick));
            const auto noteRect = juce::Rectangle<int>(x + 1, y, w - 2, juce::jmax(4, keyHeight - 4));
            if (n.pitch != pitchFromY((int)position.y) || !noteRect.contains((int)position.x, (int)position.y)) continue;
            const float leftDistance = std::abs(position.x - (float)noteRect.getX()); const float rightDistance = std::abs(position.x - (float)noteRect.getRight());
            if (leftDistance <= (float)resizeEdgePixels && leftDistance <= bestDistance) { bestDistance = leftDistance; bestSide = ResizeSide::left; }
            if (rightDistance <= (float)resizeEdgePixels && rightDistance < bestDistance) { bestDistance = rightDistance; bestSide = ResizeSide::right; }
        }
        return bestSide;
    }
    void timerCallback() override {
        auto& midi = owner.getMidiEngine(); midi.setPlaybackPositionSeconds(owner.getAudioCurrentTimeSeconds()); midi.setPlaying(owner.isAudioPlaying());
        const auto selected = midi.getSelectedNotesCopy();
        const bool hasSelection = !selected.empty();
        velocityValue.setVisible(hasSelection && velocityValue.getWidth() >= 36);
        if (hasSelection && !velocityValue.hasKeyboardFocus(true))
        {
            const int v = selected.front().velocity;
            const auto display = juce::String(v);
            if (velocityValue.getText() != display)
                velocityValue.setText(display, juce::dontSendNotification);
        }
        const int tool = getLibertyActiveTool();
        if (lastDisplayedTool != tool) { lastDisplayedTool = tool; refreshToolButtons(); }
        if (lastCursorTool != tool)
        {
            lastCursorTool = tool;
            if (tool == 0) setMouseCursor(juce::MouseCursor::NormalCursor);
            else if (tool == 4) setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            else
            {
                // Tool-shaped cursor, shared by dock and floating editors.
                static const char* symbols[] = { "", "✎", "✂", "⌫", "", "◇", "▥", "▧", "◉" };
                juce::Image cursorImage(juce::Image::ARGB, 36, 36, true);
                juce::Graphics cursorGraphics(cursorImage);
                cursorGraphics.setColour(juce::Colours::white);
                cursorGraphics.setFont(juce::Font(25.0f));
                cursorGraphics.drawText(juce::String::fromUTF8(symbols[juce::jlimit(0, 8, tool)]),
                                        2, 2, 32, 32, juce::Justification::centred);
                setMouseCursor(juce::MouseCursor(cursorImage, 3, 3));
            }
        }
        repaint(); velocityLane.repaint();
    }
    int pitchFromY(int y) const noexcept { const int keyHeight = currentKeyHeight(); const int row = juce::jlimit(0, visibleKeys - 1, (y - rulerHeight) / juce::jmax(1, keyHeight)); return lowestVisibleKey + visibleKeys - 1 - row; }
    std::int64_t tickFromX(int x) const noexcept { const auto raw = (std::int64_t)std::llround((x - pianoKeyWidth) / getPixelsPerTick()); const auto tick = juce::jmax<std::int64_t>(0, raw); const auto snap = activePianoSnapTicks(); return snap > 0 ? MidiEngine::quantizeTick(tick, snap) : tick; }

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
    void mouseDown(const juce::MouseEvent& e) override
    {
        auto* main = findMainComponent(e.eventComponent); if (main == nullptr) return;
        const auto p = e.getEventRelativeTo(main).getPosition();
        const int rowH = getLibertyTrackRowHeight();
        const int top = 76 + 32 + 4 * rowH, height = rowH;
        // Never clear ARRANGE's selected instrument merely because the user
        // clicked a legacy fixed-position MIDI lane. The dock owns selection.
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        auto* main = findMainComponent(e.eventComponent); if (main == nullptr) return;
        const auto p = e.getEventRelativeTo(main).getPosition();
        // A global mouse listener must never use a legacy fixed MIDI-lane Y.
        // Only an actual MIDI track inside the visible arranger may open this editor.
        if (p.x < main->trackHeaderWidth || p.y < main->getArrangeTop()
            || p.y >= main->getMixerTop()) return;
        const int track = main->getArrangeTrackAtY(p.y);
        const int midiFirst = main->getAudioTrackCount();
        if (track >= midiFirst && track < midiFirst + main->getMidiTrackCount())
            openLibertyMidiEditor(*main);
    }
private: bool registered = true;
};

MidiEditorMouseListener globalMidiEditorMouseListener;
std::vector<std::unique_ptr<MidiEditorWindow>> midiEditorWindows;
std::unique_ptr<PianoRoll> dockPianoRoll;
MainComponent* dockPianoRollOwner = nullptr;
}

void openLibertyMidiEditor(MainComponent& owner)
{
    for (auto& w : midiEditorWindows) if (w != nullptr && w->isVisible()) { w->toFront(true); return; }
    midiEditorWindows.push_back(std::make_unique<MidiEditorWindow>(owner));
}

void shutdownLibertyMidiEditor()
{
    dockPianoRoll.reset();
    dockPianoRollOwner = nullptr;
    midiEditorWindows.clear();
    globalMidiEditorMouseListener.shutdown();
}

void layoutLibertyPianoRollDock(MainComponent& owner, juce::Rectangle<int> bounds, bool visible)
{
    if (!dockPianoRoll || dockPianoRollOwner != &owner)
    {
        dockPianoRoll.reset();
        dockPianoRollOwner = &owner;
        dockPianoRoll = std::make_unique<PianoRoll>(owner);
        owner.addAndMakeVisible(*dockPianoRoll);
    }
    dockPianoRoll->setBounds(bounds);
    dockPianoRoll->setVisible(visible && bounds.getWidth() > 80 && bounds.getHeight() > 120);
    if (dockPianoRoll->isVisible()) dockPianoRoll->toFront(false);
}
