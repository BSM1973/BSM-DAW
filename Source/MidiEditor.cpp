#include "MidiEditor.h"
#include "MainComponent.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

float getLibertyPianoHorizontalZoom() noexcept;
std::int64_t getLibertySnapStepTicks() noexcept;
float getLibertyPianoVerticalZoom() noexcept;
int getLibertyTrackRowHeight() noexcept;
int getLibertyActiveTool();
void setLibertyActiveTool(int);
void synchronizeLibertyPianoRollPlayback(MainComponent&);

bool handleLibertyClipNameDoubleClick(MainComponent&,juce::Point<int>);

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

juce::Colour colourForMidiVelocity(int velocity)
{
    const float ratio = juce::jlimit(0.0f, 1.0f, ((float)velocity - 1.0f) / 126.0f);
    const juce::Colour stops[] = {
        juce::Colour(0xff3575d9), juce::Colour(0xff36c9c5),
        juce::Colour(0xff55c86b), juce::Colour(0xffffbe45),
        juce::Colour(0xffed3949)
    };
    const float scaled = ratio * 4.0f;
    const int segment = juce::jlimit(0, 3, (int)scaled);
    return stops[segment].interpolatedWith(stops[segment + 1], scaled - (float)segment);
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
        g.drawText(juce::String::fromUTF8("VÉLOCITÉ"), 8, 8, pianoKeyWidth - 16, 16, juce::Justification::centredLeft);

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

            // Velocity lane and notes use exactly the same heatmap.
            const auto velocityColour = note.muted ? juce::Colour(0xff46505a)
                                                   : colourForMidiVelocity((int)note.velocity);
            g.setColour(velocityColour);
            g.fillRoundedRectangle(bar.toFloat(), 2.0f);
            const bool selected = owner.getMidiEngine().isNoteSelected(note);
            g.setColour(note.muted ? juce::Colour(0xff75808b)
                                   : selected ? juce::Colour(0xffffe4ac) : velocityColour.brighter(0.35f));
            g.drawRoundedRectangle(bar.toFloat(), 2.0f, selected ? 2.0f : 1.0f);
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
        if (!note.has_value())
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
    std::optional<MidiEngine::NoteEvent> findNoteForX(int x) const
    {
        const auto gridWidth = juce::jmax(1, getWidth() - pianoKeyWidth);
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(
            juce::jmax<std::int64_t>(1, ticksPerMeasure),
            MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        const auto pixelsPerTick = (static_cast<double>(gridWidth - 2) / static_cast<double>(clipLengthTicks))
            * (double)getLibertyPianoHorizontalZoom();

        std::optional<MidiEngine::NoteEvent> best;
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
                best = note;
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

class ControllerLane final : public juce::Component
{
    class CurvePalette final : public juce::Component
    {
    public:
        CurvePalette(int selected, std::function<void(int)> callback)
            : selectedShape(selected), onSelect(std::move(callback))
        {
            setSize(368, 248);
        }
        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xff152530));
            g.setColour(juce::Colour(0xffe7f5fa));
            g.setFont(juce::Font(15.0f, juce::Font::bold));
            g.drawText(juce::String::fromUTF8("PALETTE DE COURBES"), 14, 8, 335, 26,
                juce::Justification::centredLeft);
            static const char* names[] = { "Ligne", "Sinusoïdale", "Triangle", "Douce",
                "Descente", "Carrée", "Exponentielle", "Logarithmique" };
            constexpr double pi = juce::MathConstants<double>::pi;
            for (int i = 0; i < 8; ++i)
            {
                const int x = 12 + (i % 2) * 174;
                const int y = 40 + (i / 2) * 50;
                const auto tile = juce::Rectangle<float>((float)x, (float)y, 166.0f, 44.0f);
                g.setColour(juce::Colour(i == selectedShape ? 0xff286873 : 0xff203744));
                g.fillRoundedRectangle(tile, 6.0f);
                g.setColour(juce::Colour(i == selectedShape ? 0xffa5faff : 0xff587383));
                g.drawRoundedRectangle(tile, 6.0f, 1.0f);
                juce::Path wave;
                for (int n = 0; n <= 48; ++n)
                {
                    const double t = (double)n / 48.0;
                    const double phase = std::fmod(t * 2.0, 1.0);
                    double v = t;
                    switch (i)
                    {
                        case 1: v = 0.5 - 0.5 * std::cos(2.0 * pi * phase); break;
                        case 2: v = phase < 0.5 ? 2.0 * phase : 2.0 * (1.0 - phase); break;
                        case 3: v = t * t * (3.0 - 2.0 * t); break;
                        case 4: v = std::sin(t * pi * 0.5); break;
                        case 5: v = phase < 0.5 ? 0.0 : 1.0; break;
                        case 6: v = t * t * t; break;
                        case 7: v = std::sqrt(t); break;
                        default: break;
                    }
                    const float px = (float)(x + 12 + t * 142.0);
                    const float py = (float)(y + 25 - v * 17.0);
                    if (n == 0) wave.startNewSubPath(px, py);
                    else wave.lineTo(px, py);
                }
                g.setColour(juce::Colour(0xff6ee3e7));
                g.strokePath(wave, juce::PathStrokeType(1.7f));
                g.setColour(juce::Colour(0xffe7f5fa));
                g.setFont(juce::Font(10.0f));
                g.drawText(juce::String::fromUTF8(names[i]), x + 8, y + 30, 150, 12,
                    juce::Justification::centred);
            }
        }
        void mouseUp(const juce::MouseEvent& e) override
        {
            const int col = (e.x - 12) / 174;
            const int row = (e.y - 40) / 50;
            if (e.x < 12 || e.y < 40 || col < 0 || col > 1 || row < 0 || row > 3) return;
            const int shape = row * 2 + col;
            selectedShape = shape;
            onSelect(shape);
            repaint();
        }
    private:
        int selectedShape;
        std::function<void(int)> onSelect;
    };
public:
    ControllerLane(MainComponent& o, int& controllerRef) : owner(o), controller(controllerRef)
    {
        curveButton.setButtonText(juce::String::fromUTF8("COURBES"));
        curveButton.setTooltip(juce::String::fromUTF8("Choisir une forme, puis cliquer-glisser dans la piste MIDI pour la dessiner"));
        curveButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff243e4b));
        curveButton.onClick = [this]
        {
            auto palette = std::make_unique<CurvePalette>(curveShape,
                [safe = juce::Component::SafePointer<ControllerLane>(this)](int shape)
                {
                    if (safe == nullptr) return;
                    safe->curveShape = shape;
                    safe->curveMode = true;
                    safe->curveButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff176b65));
                    safe->repaint();
                });
            juce::CallOutBox::launchAsynchronously(std::move(palette),
                curveButton.getScreenBounds(), nullptr);
        };
        addAndMakeVisible(curveButton);
        snapButton.setButtonText("SNAP");
        snapButton.setClickingTogglesState(true);
        snapButton.setToggleState(true, juce::dontSendNotification);
        snapButton.setTooltip(juce::String::fromUTF8("Activer ou désactiver la grille pour les contrôleurs MIDI"));
        snapButton.onClick = [this] { refreshSnapAppearance(); };
        refreshSnapAppearance();
        addAndMakeVisible(snapButton);
    }
    void resized() override
    {
        curveButton.setBounds(pianoKeyWidth + 8, 4, 112, 23);
        snapButton.setBounds(pianoKeyWidth + 126, 4, 84, 23);
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff101821));
        // Discreet graphite toolbar, visually separated from the MIDI grid.
        g.setColour(juce::Colour(0xff1c232b));
        g.fillRect(0, 0, getWidth(), 34);
        g.setColour(juce::Colour(0xff303942));
        g.drawHorizontalLine(34, 0.0f, (float)getWidth());
        const auto label = controller == 128 ? "PITCH BEND" : controller == 129 ? "AFTERTOUCH" : "CC " + juce::String(controller);
        g.setColour(juce::Colour(0xffb8e8ec));
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(label, 7, 7, pianoKeyWidth - 12, 22, juce::Justification::centredLeft);
        const auto clipTicks = juce::jmax<std::int64_t>(MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator()),
            MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        const double pixelsPerTick = (double)juce::jmax(1, getWidth() - pianoKeyWidth - 2) / (double)juce::jmax<std::int64_t>(1, clipTicks)
            * getLibertyPianoHorizontalZoom();
        const int maxValue = controller == 128 ? 16383 : 127;
        const int bottom = getHeight() - 9;
        const int usable = juce::jmax(1, getHeight() - 51);
        juce::Path curve;
        bool started = false;
        for (const auto& event : owner.getMidiEngine().getControllerEventsCopy())
        {
            if (event.controller != controller || event.channel != 1) continue;
            const int x = pianoKeyWidth + (int)std::llround((double)event.tick * pixelsPerTick);
            const int y = bottom - (int)std::llround((double)event.value / maxValue * usable);
            if (!started) { curve.startNewSubPath((float)x, (float)y); started = true; }
            else curve.lineTo((float)x, (float)y);
            // Keep the automation line clean: do not paint dozens of control-point dots.
        }
        g.setColour(juce::Colour(0xff63d7dc).withAlpha(0.75f));
        g.strokePath(curve, juce::PathStrokeType(2.0f));
        if (draggingCurve)
        {
            const int leftX = juce::jmax(pianoKeyWidth, juce::jmin(dragStart.x, dragEnd.x));
            const int rightX = juce::jmax(pianoKeyWidth, juce::jmax(dragStart.x, dragEnd.x));
            const int y0 = dragStart.x <= dragEnd.x ? dragStart.y : dragEnd.y;
            const int y1 = dragStart.x <= dragEnd.x ? dragEnd.y : dragStart.y;
            juce::Path preview;
            for (int i = 0; i <= 96; ++i)
            {
                const double t = (double)i / 96.0;
                const float x = (float)(leftX + (rightX - leftX) * t);
                const float y = (float)juce::jlimit(35.0, (double)getHeight() - 9.0, y0 + (y1 - y0) * curveFraction(t, (double)(rightX - leftX) / 100.0));
                if (i == 0) preview.startNewSubPath(x, y);
                else preview.lineTo(x, y);
            }
            g.setColour(juce::Colour(0xffffd486));
            g.strokePath(preview, juce::PathStrokeType(2.5f));
        }
        g.setColour(juce::Colour(0xff40515f));
        g.fillRect(pianoKeyWidth - 1, 2, 1, juce::jmax(0, getHeight() - 2));
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.x < pianoKeyWidth || e.y < 35) return;
        if (e.mods.isRightButtonDown())
        {
            curveMode = false;
            draggingCurve = false;
            curveButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff243e4b));
            repaint();
            return;
        }
        if (!e.mods.isLeftButtonDown()) return;
        if (curveMode)
        {
            draggingCurve = true;
            dragStart = e.getPosition();
            dragEnd = dragStart;
            repaint();
        }
        else edit(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (draggingCurve)
        {
            dragEnd = e.getPosition();
            repaint();
        }
        else if (e.mods.isLeftButtonDown()) edit(e);
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        if (!draggingCurve) return;
        dragEnd = e.getPosition();
        draggingCurve = false;
        const int x0 = juce::jmax(pianoKeyWidth, dragStart.x);
        const int x1 = juce::jmax(pianoKeyWidth, dragEnd.x);
        if (std::abs(x1 - x0) < 4) { repaint(); return; }
        const auto clipTicks = juce::jmax<std::int64_t>(
            MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator()),
            MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        const double ppt = (double)juce::jmax(1, getWidth() - pianoKeyWidth - 2)
            / (double)juce::jmax<std::int64_t>(1, clipTicks) * getLibertyPianoHorizontalZoom();
        auto tick0 = juce::jmax<std::int64_t>(0, (std::int64_t)std::llround((x0 - pianoKeyWidth) / ppt));
        auto tick1 = juce::jmax<std::int64_t>(0, (std::int64_t)std::llround((x1 - pianoKeyWidth) / ppt));
        const auto snap = snapButton.getToggleState() ? activePianoSnapTicks() : 0;
        if (snap > 0)
        {
            tick0 = MidiEngine::quantizeTick(tick0, snap);
            tick1 = MidiEngine::quantizeTick(tick1, snap);
        }
        if (tick0 == tick1) tick1 = tick0 + juce::jmax<std::int64_t>(1, snap);
        const int bottom = getHeight() - 9;
        const int usable = juce::jmax(1, getHeight() - 51);
        const int maxValue = controller == 128 ? 16383 : 127;
        const auto valueAt = [&](int y)
        {
            return juce::jlimit(0, maxValue,
                (int)std::llround((double)(bottom - y) / usable * maxValue));
        };
        const auto left = juce::jmin(tick0, tick1);
        const auto right = juce::jmax(tick0, tick1);
        const int startValue = tick0 <= tick1 ? valueAt(dragStart.y) : valueAt(dragEnd.y);
        const int endValue = tick0 <= tick1 ? valueAt(dragEnd.y) : valueAt(dragStart.y);
        generateCurve(left, right, startValue, endValue, (double)std::abs(x1 - x0) / 100.0);
    }
    double curveFraction(double t, double cycles) const
    {
        constexpr double pi = juce::MathConstants<double>::pi;
        // One full waveform every 100 horizontal pixels; dragging farther
        // repeats the pattern rather than stretching a single cycle.
        const double phase = std::fmod(t * juce::jmax(0.01, cycles), 1.0);
        switch (curveShape)
        {
            case 1: return 0.5 - 0.5 * std::cos(2.0 * pi * phase);
            case 2: return phase < 0.5 ? 2.0 * phase : 2.0 * (1.0 - phase);
            case 5: return phase < 0.5 ? 0.0 : 1.0;
            case 3: return t * t * (3.0 - 2.0 * t);
            case 4: return std::sin(t * pi * 0.5);
            case 6: return t * t * t;
            case 7: return std::sqrt(t);
            default: return t;
        }
    }
    void generateCurve(std::int64_t left, std::int64_t right, int startValue, int endValue, double cycles)
    {
        if (right <= left) return;
        const auto span = right - left;
        const int maxValue = controller == 128 ? 16383 : 127;
        // MIDI CC is discrete, but the visible curve is rendered as a continuous path.
        // Cap event density to prevent flooding the instrument with automation.
        const int steps = (int)juce::jmin<std::int64_t>(juce::jmax(96, (int)std::ceil(cycles * 24.0)), juce::jmin<std::int64_t>(512, span));
        std::int64_t previousTick = -1;
        for (int i = 0; i <= steps; ++i)
        {
            const auto tick = left + (span * i) / juce::jmax(1, steps);
            if (tick == previousTick) continue;
            previousTick = tick;
            const double t = (double)(tick - left) / (double)span;
            const double fraction = curveFraction(t, cycles);
            const int value = juce::jlimit(0, maxValue,
                (int)std::llround(startValue + (endValue - startValue) * fraction));
            owner.getMidiEngine().setControllerEvent(tick, controller, value);
        }
        owner.publishPianoRollControllers();
        repaint(); owner.repaint();
    }
private:
    void refreshSnapAppearance()
    {
        const bool enabled = snapButton.getToggleState();
        snapButton.setButtonText(enabled ? "SNAP ON" : "SNAP OFF");
        snapButton.setColour(juce::TextButton::buttonColourId,
            enabled ? juce::Colour(0xff176b65) : juce::Colour(0xff4a2931));
        snapButton.setColour(juce::TextButton::buttonOnColourId,
            enabled ? juce::Colour(0xff176b65) : juce::Colour(0xff4a2931));
        snapButton.setColour(juce::TextButton::textColourOffId,
            enabled ? juce::Colour(0xffd8fff2) : juce::Colour(0xffffcbd0));
        snapButton.setColour(juce::TextButton::textColourOnId,
            enabled ? juce::Colour(0xffd8fff2) : juce::Colour(0xffffcbd0));
        snapButton.repaint();
    }
    void edit(const juce::MouseEvent& e)
    {
        if (e.x < pianoKeyWidth || e.y < 35 || getWidth() <= pianoKeyWidth) return;
        const auto clipTicks = juce::jmax<std::int64_t>(MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator()),
            MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        const double ppt = (double)juce::jmax(1, getWidth() - pianoKeyWidth - 2) / (double)juce::jmax<std::int64_t>(1, clipTicks)
            * getLibertyPianoHorizontalZoom();
        auto tick = juce::jmax<std::int64_t>(0, (std::int64_t)std::llround((e.x - pianoKeyWidth) / ppt));
        const auto snap = activePianoSnapTicks();
        if (snapButton.getToggleState() && snap > 0) tick = MidiEngine::quantizeTick(tick, snap);
        const int bottom = getHeight() - 9;
        const int value = juce::jlimit(0, controller == 128 ? 16383 : 127,
            (int)std::llround((double)(bottom - e.y) / juce::jmax(1, getHeight() - 27) * (controller == 128 ? 16383 : 127)));
        if (owner.getMidiEngine().setControllerEvent(tick, controller, value))
        { owner.publishPianoRollControllers(); repaint(); owner.repaint(); }
    }
    MainComponent& owner;
    int& controller;
    juce::TextButton curveButton, snapButton;
    int curveShape = 0;
    bool curveMode = false;
    bool draggingCurve = false;
    juce::Point<int> dragStart, dragEnd;
};

class PianoRoll final : public juce::Component, private juce::Timer
{
public:
    explicit PianoRoll(MainComponent& o) : owner(o), velocityLane(o), controllerLane(o, activeController)
    {
        setWantsKeyboardFocus(true);
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        addAndMakeVisible(velocityLane);
        addAndMakeVisible(controllerLane);
        controllerLane.setVisible(false);
        controllerSelector.addItem(juce::String::fromUTF8("Vélocité"), 1);
        controllerSelector.addItem("Modulation CC1", 2);
        controllerSelector.addItem("Expression CC11", 3);
        controllerSelector.addItem("Sustain CC64", 4);
        controllerSelector.addItem("Pitch Bend", 5);
        controllerSelector.addItem("Aftertouch", 6);
        controllerSelector.setTooltip(juce::String::fromUTF8("Choisir le contrôleur MIDI à éditer"));
        controllerSelector.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff233746));
        controllerSelector.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe2edf2));
        controllerSelector.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff40515f));
        controllerSelector.setSelectedId(1, juce::dontSendNotification);
        controllerSelector.onChange = [this] {
            static constexpr int ids[] = { -1, 1, 11, 64, 128, 129 };
            activeController = ids[juce::jlimit(0, 5, controllerSelector.getSelectedId() - 1)];
            velocityLane.setVisible(dualLanes || activeController < 0);
            controllerLane.setVisible(dualLanes || activeController >= 0);
            resized(); repaint();
        };
        addAndMakeVisible(controllerSelector);
        dualLaneButton.setButtonText("");
        dualLaneButton.setTooltip(juce::String::fromUTF8("Afficher la vélocité et un contrôleur MIDI sur deux lignes"));
        dualLaneButton.setClickingTogglesState(true);
        dualLaneButton.onClick = [this]
        {
            dualLanes = dualLaneButton.getToggleState();
            if (dualLanes && activeController < 0)
                controllerSelector.setSelectedId(5, juce::sendNotificationSync);
            dualLaneButton.setColour(juce::TextButton::buttonColourId,
                dualLanes ? juce::Colour(0xff176b65) : juce::Colour(0xff28333d));
            velocityLane.setVisible(dualLanes || activeController < 0);
            controllerLane.setVisible(dualLanes || activeController >= 0);
            resized(); repaint();
        };
        dualLaneButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff28333d));
        addAndMakeVisible(dualLaneButton);
        topLaneHandle.onDrag = [this](int y)
        {
            const int previousHeight = dualLaneTotalHeight;
            dualLaneTotalHeight = juce::jlimit(96, juce::jmax(96, getHeight() - rulerHeight - 40), getHeight() - y);
            dualLaneFirstHeight = juce::jlimit(36, dualLaneTotalHeight - 36,
                dualLaneFirstHeight + dualLaneTotalHeight - previousHeight);
            resized(); repaint();
        };
        dividerLaneHandle.onDrag = [this](int y)
        {
            dualLaneFirstHeight = juce::jlimit(36, juce::jmax(36, dualLaneTotalHeight - 36),
                y - (getHeight() - dualLaneTotalHeight));
            resized(); repaint();
        };
        addAndMakeVisible(topLaneHandle);
        addAndMakeVisible(dividerLaneHandle);
        topLaneHandle.setVisible(false);
        dividerLaneHandle.setVisible(false);
        dualLaneButton.setLookAndFeel(&dualLaneLookAndFeel);
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
            button.onClick = [this, i] { setLibertyActiveTool(i); resized(); };
            addAndMakeVisible(button);
        }
        quantizeButton.setButtonText("Q");
        quantizeButton.setTooltip("Quantifier les notes sélectionnées sur la grille SNAP (Q)");
        quantizeButton.onClick = [this] { quantizeSelection(); };
        addAndMakeVisible(quantizeButton);
        selectAllButton.setButtonText(juce::String::fromUTF8("ÉDITER"));
        selectAllButton.setTooltip(juce::String::fromUTF8("Édition MIDI : sélection, duplication, legato et quantification"));
        selectAllButton.onClick = [this] { showEditWindow(); };
        addAndMakeVisible(selectAllButton);
        moreToolsButton.setButtonText(juce::String::fromUTF8("⋯"));
        moreToolsButton.setTooltip(juce::String::fromUTF8("Outils MIDI supplémentaires — outil actif indiqué dans la palette"));
        moreToolsButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff263544));
        moreToolsButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdcb66f));
        moreToolsButton.onClick = [this]
        {
            juce::PopupMenu menu;
            static const char* names[] = { "Sélection", "Crayon", "Ciseaux", "Gomme",
                "Redimensionner", "Étirer", "Vélocité", "Sélection rectangulaire", "Muet" };
            for (int i = 0; i < 9; ++i)
                menu.addItem(i + 1, juce::String::fromUTF8(names[i]), true, getLibertyActiveTool() == i);
            menu.addSeparator();
            menu.addItem(20, juce::String::fromUTF8("Quantifier"));
            menu.addItem(21, juce::String::fromUTF8("ÉDITION MIDI"));
            menu.addItem(24, juce::String::fromUTF8("COMPOSITION · ACCORDS…"));
            menu.addItem(25, juce::String::fromUTF8("COMPOSITION · PROGRESSIONS…"));
            menu.addItem(26, juce::String::fromUTF8("COMPOSITION · GAMMES…"));
            // Fender-inspired pitch selection, without changing any MIDI notes.
            juce::PopupMenu pitchSelection;
            pitchSelection.addItem(30, juce::String::fromUTF8("Notes les plus aiguës"));
            pitchSelection.addItem(31, juce::String::fromUTF8("Notes les plus graves"));
            pitchSelection.addSeparator();
            pitchSelection.addItem(32, juce::String::fromUTF8("Notes muettes"));
            pitchSelection.addItem(33, juce::String::fromUTF8("Une note sur deux"));
            pitchSelection.addItem(34, juce::String::fromUTF8("Une note sur trois"));
            menu.addSubMenu(juce::String::fromUTF8("SÉLECTION · HAUTEURS"), pitchSelection);
            menu.addItem(35, juce::String::fromUTF8("SÉLECTION · FILTRES MIDI…"));
            menu.addItem(22, juce::String::fromUTF8("Annuler"), owner.getMidiEngine().canUndo());
            menu.addItem(23, juce::String::fromUTF8("Rétablir"), owner.getMidiEngine().canRedo());
            juce::Component::SafePointer<PianoRoll> safe(this);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&moreToolsButton),
                [safe](int result)
                {
                    if (safe == nullptr) return;
                    if (result >= 1 && result <= 9)
                    { setLibertyActiveTool(result - 1); safe->resized(); }
                    else if (result == 20) safe->quantizeSelection();
                    else if (result == 21) safe->showEditWindow();
                    else if (result == 22) safe->applyMidiHistory(false);
                    else if (result == 23) safe->applyMidiHistory(true);
                    else if (result == 24) safe->showCompositionWindow();
                    else if (result == 25) safe->showProgressionWindow();
                    else if (result == 26) safe->showScaleWindow();
                    else if (result == 30) safe->selectExtremePitches(true);
                    else if (result == 31) safe->selectExtremePitches(false);
                    else if (result == 32) safe->selectMutedNotes();
                    else if (result == 33) safe->selectEveryNthNote(2);
                    else if (result == 34) safe->selectEveryNthNote(3);
                    else if (result == 35) safe->showMidiSelectionFilter();
                });
        };
        addAndMakeVisible(moreToolsButton);
        undoMidiButton.setButtonText(juce::String::fromUTF8("↶"));
        redoMidiButton.setButtonText(juce::String::fromUTF8("↷"));
        undoMidiButton.setTooltip(juce::String::fromUTF8("Annuler la dernière édition MIDI (Cmd/Ctrl+Z)"));
        redoMidiButton.setTooltip(juce::String::fromUTF8("Rétablir une édition MIDI (Cmd/Ctrl+Maj+Z)"));
        for (auto* button : { &undoMidiButton, &redoMidiButton })
        {
            button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff263544));
            button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdcb66f));
            addAndMakeVisible(*button);
        }
        undoMidiButton.onClick = [this] { applyMidiHistory(false); };
        redoMidiButton.onClick = [this] { applyMidiHistory(true); };
        durationValue.setBufferedToImage(true);
        durationValue.setButtonText(juce::String::fromUTF8("Durée"));
        durationValue.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff233746));
        durationValue.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffffdf99));
        durationValue.setTooltip(juce::String::fromUTF8("Choisir une durée dans la palette musicale"));
        durationValue.onClick = [this] { showDurationPalette(); };
        durationValue.setVisible(false);
        addAndMakeVisible(durationValue);
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

    class DurationPalette final : public juce::Component
    {
    public:
        DurationPalette(int selected, std::function<void(int)> onSelect)
            : selection(std::move(onSelect))
        {
            // This panel paints every pixel of its rectangular bounds.
            // Treat it as an opaque, buffered layer so the rapidly repainted
            // piano-roll underneath cannot show through between frames.
            setOpaque(true);
            setBufferedToImage(true);
            static const char* names[] = {
                "Ronde", "Blanche", "Noire", "Croche", "Double-croche", "Triple-croche",
                "Ronde pointée", "Blanche pointée", "Noire pointée", "Croche pointée",
                "Double-croche pointée", "Triple-croche pointée",
                "Blanche triolet", "Noire triolet", "Croche triolet",
                "Double-croche triolet", "Triple-croche triolet"
            };
            for (int i = 0; i < 25; ++i)
            {
                auto button = std::make_unique<juce::TextButton>(juce::String::fromUTF8(names[i]));
                button->setColour(juce::TextButton::buttonColourId,
                                  juce::Colour(i + 1 == selected ? 0xff28606c : 0xff203948));
                button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff28606c));
                button->setColour(juce::TextButton::textColourOffId,
                                  juce::Colour(i + 1 == selected ? 0xff9ffaff : 0xffe8f3fa));
                const int id = i + 1;
                button->onClick = [this, id]
                {
                    selection(id);
                    // The owner closes the palette asynchronously after this click.
                    // Never delete the button while its onClick is still executing.
                };
                addAndMakeVisible(*button);
                buttons.push_back(std::move(button));
            }
            setSize(462, 360);
        }
        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xff142633));
            g.setColour(juce::Colour(0xff54d6dc));
            g.fillRoundedRectangle(16.0f, 15.0f, 4.0f, 28.0f, 2.0f);
            g.setColour(juce::Colour(0xfff1faff));
            g.setFont(juce::Font(19.0f, juce::Font::bold));
            g.drawText(juce::String::fromUTF8("DURÉE MUSICALE"), 29, 13, 395, 30,
                       juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff89aab9));
            g.setFont(juce::Font(12.0f));
            g.drawText("STANDARD", 18, 53, 140, 20, juce::Justification::centredLeft);
            g.drawText(juce::String::fromUTF8("POINTÉE"), 18, 148, 140, 20,
                       juce::Justification::centredLeft);
            g.drawText("TRIOLET", 18, 243, 140, 20, juce::Justification::centredLeft);
        }
        void resized() override
        {
            for (int i = 0; i < (int)buttons.size(); ++i)
            {
                const int group = i < 6 ? 0 : (i < 12 ? 1 : 2);
                const int index = i - (group == 0 ? 0 : group == 1 ? 6 : 12);
                buttons[(size_t)i]->setBounds(18 + (index % 3) * 144,
                                               76 + group * 95 + (index / 3) * 32,
                                               136, 28);
            }
        }
    private:
        std::function<void(int)> selection;
        std::vector<std::unique_ptr<juce::TextButton>> buttons;
    };

    void adjustSelectedVelocity(int delta, bool absolute)
    {
        auto& midi = owner.getMidiEngine();
        const auto notes = midi.getSelectedNotesCopy();
        bool changed = false;
        for (const auto& note : notes)
        {
            const int target = juce::jlimit(1, 127, absolute ? delta : (int)note.velocity + delta);
            if (target != (int)note.velocity)
                changed = midi.setNoteVelocity(note.startTick, note.pitch, note.channel, target) || changed;
        }
        if (changed)
        {
            synchronizeLibertyPianoRollPlayback(owner);
            repaint(); owner.repaint();
        }
    }

    void applyVelocityRamp(bool ascending, int startVelocity, int endVelocity, int curveType)
    {
        auto& midi = owner.getMidiEngine();
        auto notes = midi.getSelectedNotesCopy();
        if (notes.empty()) return;
        std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b)
        {
            if (a.startTick != b.startTick) return a.startTick < b.startTick;
            return a.pitch < b.pitch;
        });
        const auto first = notes.front().startTick;
        const auto last = notes.back().startTick;
        bool changed = false;
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const double fraction = last > first
                ? (double)(notes[i].startTick - first) / (double)(last - first)
                : (notes.size() > 1 ? (double)i / (double)(notes.size() - 1) : 0.0);
            const int shape = curveType % 4;
            const double strength = juce::jlimit(0.25, 2.0, (curveType / 4) * 0.05);
            const double shaped = shape == 1 ? fraction * fraction
                : shape == 2 ? std::sqrt(fraction)
                : shape == 3 ? fraction * fraction * (3.0 - 2.0 * fraction) : fraction;
            const double adjusted = std::pow(shaped, strength);
            const int target = juce::jlimit(1, 127, juce::roundToInt(
                ascending ? startVelocity + adjusted * (endVelocity - startVelocity)
                          : endVelocity + adjusted * (startVelocity - endVelocity)));
            changed = midi.setNoteVelocity(notes[i].startTick, notes[i].pitch,
                notes[i].channel, target) || changed;
        }
        if (changed)
        {
            synchronizeLibertyPianoRollPlayback(owner);
            repaint(); owner.repaint();
        }
    }

    class EditWindowContent final : public juce::Component
    {
    public:
        explicit EditWindowContent(std::function<juce::String(int, int, int, int)> action, bool hasSelection, std::vector<MidiEngine::NoteEvent> notes,
            std::function<void(double, double, double)> quantizeAction,
            std::function<void(int)> chordAction, std::function<juce::String()> recognizeAction,
            std::function<void(int)> rhythmAction, std::function<void(int, int)> expressiveAction, std::function<void(int, int)> euclideanAction, std::function<void(int)> probabilityAction,
              std::function<void(int, int, int)> melodyAction,
              std::function<void(int, int, int, int)> selectionAction)
            : onAction(std::move(action)), onQuantize(std::move(quantizeAction)),
              onChordTransform(std::move(chordAction)), onRecognizeChord(std::move(recognizeAction)),
              onRhythmTool(std::move(rhythmAction)), onExpressiveTool(std::move(expressiveAction)),
              onEuclidean(std::move(euclideanAction)), onProbability(std::move(probabilityAction)), onMelody(std::move(melodyAction)), onSelection(std::move(selectionAction)),
              previewNotes(std::move(notes))
        {
            selectionCriterion.addItem(juce::String::fromUTF8("Hauteur MIDI"), 1);
            selectionCriterion.addItem(juce::String::fromUTF8("Vélocité"), 2);
            selectionCriterion.addItem(juce::String::fromUTF8("Durée (échelle 0–127)"), 3);
            selectionCriterion.addItem(juce::String::fromUTF8("Position sur la grille"), 4);
            selectionCriterion.addItem(juce::String::fromUTF8("Notes dans la gamme active"), 5);
            selectionCriterion.addItem(juce::String::fromUTF8("Temps forts (noires)"), 6);
            selectionCriterion.addItem(juce::String::fromUTF8("Temps faibles (hors noires)"), 7);
            selectionCriterion.addItem(juce::String::fromUTF8("Hauteurs répétées"), 8);
            selectionCriterion.addItem(juce::String::fromUTF8("Probabilité de lecture"), 9);
            selectionCriterion.addItem(juce::String::fromUTF8("Notes muettes"), 10);
            selectionCriterion.addItem(juce::String::fromUTF8("Notes actives"), 11);
            selectionCriterion.addItem(juce::String::fromUTF8("Doublons exacts"), 12);
            selectionCriterion.addItem(juce::String::fromUTF8("Vélocité différente de 100"), 13);
            selectionCriterion.addItem(juce::String::fromUTF8("Probabilité différente de 100 %"), 14);
            selectionCriterion.addItem(juce::String::fromUTF8("Notes avec contrôleurs CC"), 15);
            selectionCriterion.addItem(juce::String::fromUTF8("Notes avec Pitch Bend"), 16);
            selectionCriterion.addItem(juce::String::fromUTF8("Notes avec pression canal"), 17);
            selectionCriterion.setSelectedId(1, juce::dontSendNotification);
            selectionCriterion.onChange = [this]
            {
                const int criterion = selectionCriterion.getSelectedId();
                const bool usesRange = criterion <= 4 || criterion == 9;
                selectionMin.setEnabled(usesRange);
                selectionMax.setEnabled(usesRange);
                const double upper = criterion == 9 ? 100.0 : 127.0;
                selectionMin.setRange(0.0, upper, 1.0);
                selectionMax.setRange(0.0, upper, 1.0);
                if (selectionMax.getValue() > upper)
                    selectionMax.setValue(upper, juce::dontSendNotification);
            };
            addAndMakeVisible(selectionCriterion);
            selectionMode.addItem(juce::String::fromUTF8("Remplacer la sélection"), 1);
            selectionMode.addItem(juce::String::fromUTF8("Ajouter à la sélection"), 2);
            selectionMode.addItem(juce::String::fromUTF8("Retirer de la sélection"), 3);
            selectionMode.addItem(juce::String::fromUTF8("Conserver les correspondances"), 4);
            selectionMode.setSelectedId(1, juce::dontSendNotification);
            addAndMakeVisible(selectionMode);
            for (auto* slider : { &selectionMin, &selectionMax })
            {
                slider->setRange(0, 127, 1);
                slider->setSliderStyle(juce::Slider::LinearHorizontal);
                slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 25);
                slider->setColour(juce::Slider::trackColourId, juce::Colour(0xff348fc2));
                addAndMakeVisible(*slider);
            }
            selectionMax.setValue(127, juce::dontSendNotification);
            applySelection.setButtonText(juce::String::fromUTF8("SÉLECTIONNER LES NOTES CORRESPONDANTES"));
            applySelection.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff205d85));
            applySelection.onClick = [this]
            {
                if (onSelection) onSelection(selectionCriterion.getSelectedId(), (int)selectionMin.getValue(), (int)selectionMax.getValue(), selectionMode.getSelectedId());
            };
            addAndMakeVisible(applySelection);
            static const char* tabs[] = { "NOTES", "TRANSFORMATIONS", "VÉLOCITÉ", "HUMANISER", "QUANTIFICATION", "ACCORDS", "RYTHME", "EXPRESSION", "GÉNÉRATEURS" };
            for (int i = 0; i < 9; ++i)
            {
                categoryButtons[(size_t)i].setButtonText(juce::String::fromUTF8(tabs[i]));
                categoryButtons[(size_t)i].onClick = [this, i] { currentCategory = i; refreshCategory(); };
                addAndMakeVisible(categoryButtons[(size_t)i]);
            }
            static const char* labels[] = {
                "TOUT SÉLECTIONNER", "DUPLIQUER", "LEGATO", "SUPPRIMER",
                "QUANTIFIER", "DURÉE = SNAP", "TRANSPOSER +12", "TRANSPOSER -12",
                "VÉLOCITÉ = 100", "VÉLOCITÉ +10", "VÉLOCITÉ -10",
                "CRESCENDO", "DECRESCENDO",
                "TRANSPOSER +1", "TRANSPOSER -1",
                "DURÉE ×2", "DURÉE ÷2",
                "QUANTIFIER 50 %", "QUANTIFIER 75 %",
                "SWING 55 %", "SWING 65 %",
                "HUMANISER VÉLOCITÉ", "HUMANISER TIMING",
                "GROOVE 25 %", "GROOVE 50 %"
            };
            static const char* details[] = {
                "Sélectionner toutes les notes MIDI", "Répéter la phrase MIDI",
                "Relier les notes de même hauteur", "Effacer les notes sélectionnées",
                "Aligner sur la grille SNAP", "Appliquer la durée de la grille SNAP",
                "Monter les notes d'une octave", "Descendre les notes d'une octave",
                "Appliquer une vélocité uniforme de 100",
                "Augmenter la vélocité de 10", "Diminuer la vélocité de 10",
                "Progression de 40 à 120", "Progression de 120 à 40",
                "Monter les notes d'un demi-ton", "Descendre les notes d'un demi-ton",
                "Doubler la durée des notes sélectionnées", "Réduire de moitié la durée des notes sélectionnées",
                "Rapprocher les notes de la grille à moitié", "Rapprocher les notes de la grille aux trois quarts",
                "Décaler légèrement les contretemps", "Décaler davantage les contretemps",
                "Variation douce des attaques MIDI", "Variation subtile du placement rythmique",
                "Quantifier en préservant 25 % du placement", "Quantifier en préservant 50 % du placement"
            };
            static const int actionIds[] = { 1, 2, 3, 8, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 26, 27 };
            for (int i = 0; i < 25; ++i)
            {
                auto button = std::make_unique<juce::TextButton>(
                    juce::String::fromUTF8(labels[i]) + "   -   " + juce::String::fromUTF8(details[i]));
                button->setEnabled(actionIds[i] == 1 || hasSelection);
                button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff202d3b));
                button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe4edf4));
                const int actionId = actionIds[i];
                button->onClick = [this, actionId] { if (onAction) onAction(actionId, (int)startVelocity.getValue(), (int)endVelocity.getValue(), curveSelector.getSelectedId() - 1 + 4 * ((int)curvature.getValue() / 5)); };
                addAndMakeVisible(*button);
                buttons.push_back(std::move(button));
            }
            for (auto* slider : { &startVelocity, &endVelocity })
            {
                slider->setRange(1.0, 127.0, 1.0);
                slider->setSliderStyle(juce::Slider::LinearHorizontal);
                slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 25);
                slider->setColour(juce::Slider::trackColourId, juce::Colour(0xffc6a76b));
                addAndMakeVisible(*slider);
            }
            curvature.setRange(25.0, 200.0, 5.0);
            curvature.setTextValueSuffix(" %");
            curvature.setTooltip(juce::String::fromUTF8("Intensité de la courbe : 100 % conserve la forme d'origine."));
            curvature.setValue(100.0, juce::dontSendNotification);
            curvature.setSliderStyle(juce::Slider::LinearHorizontal);
            curvature.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 25);
            curvature.setColour(juce::Slider::trackColourId, juce::Colour(0xffc6a76b));
            curvature.onValueChange = [this] { repaint(); };
            addAndMakeVisible(curvature);
            resetCurvature.setButtonText("100 %");
            resetCurvature.setTooltip(juce::String::fromUTF8("Rétablir la courbure d'origine sans changer les vélocités."));
            resetCurvature.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff263544));
            resetCurvature.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdcb66f));
            resetCurvature.onClick = [this] { curvature.setValue(100.0, juce::sendNotification); };
            addAndMakeVisible(resetCurvature);
            startVelocity.onValueChange = [this] { repaint(); };
            endVelocity.onValueChange = [this] { repaint(); };
            curveSelector.addItem(juce::String::fromUTF8("Linéaire"), 1);
            curveSelector.addItem(juce::String::fromUTF8("Exponentielle"), 2);
            curveSelector.addItem(juce::String::fromUTF8("Douce (logarithmique)"), 3);
            curveSelector.addItem(juce::String::fromUTF8("Courbe en S (progressive)"), 4);
            curveSelector.setSelectedId(1, juce::dontSendNotification);
            curveSelector.setTooltip(juce::String::fromUTF8("Choisir la forme de progression des vélocités MIDI."));
            curveSelector.onChange = [this] { repaint(); };
            addAndMakeVisible(curveSelector);
            reverseVelocity.setButtonText(juce::String::fromUTF8("INVERSER DÉPART / ARRIVÉE"));
            reverseVelocity.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff263544));
            reverseVelocity.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe4edf4));
            reverseVelocity.setTooltip(juce::String::fromUTF8("Échanger les vélocités de départ et d'arrivée sans modifier les notes."));
            reverseVelocity.onClick = [this]
            {
                const double previousStart = startVelocity.getValue();
                startVelocity.setValue(endVelocity.getValue(), juce::sendNotification);
                endVelocity.setValue(previousStart, juce::sendNotification);
            };
            addAndMakeVisible(reverseVelocity);
            resetVelocity.setButtonText(juce::String::fromUTF8("RÉINITIALISER LES RÉGLAGES"));
            resetVelocity.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff263544));
            resetVelocity.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe4edf4));
            resetVelocity.setTooltip(juce::String::fromUTF8("Restaurer les valeurs 40 / 120, la forme linéaire et la courbure 100 %."));
            resetVelocity.onClick = [this]
            {
                startVelocity.setValue(40.0, juce::sendNotification);
                endVelocity.setValue(120.0, juce::sendNotification);
                curveSelector.setSelectedId(1, juce::sendNotification);
                curvature.setValue(100.0, juce::sendNotification);
            };
            addAndMakeVisible(resetVelocity);
            humanizeVelocity.setRange(0.0, 24.0, 1.0);
            humanizeVelocity.setValue(6.0, juce::dontSendNotification);
            humanizeTiming.setRange(0.0, 48.0, 1.0);
            humanizeTiming.setValue(12.0, juce::dontSendNotification);
            for (auto* slider : { &humanizeVelocity, &humanizeTiming })
            {
                slider->setSliderStyle(juce::Slider::LinearHorizontal);
                slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 25);
                slider->setColour(juce::Slider::trackColourId, juce::Colour(0xffc6a76b));
                addAndMakeVisible(*slider);
            }
            humanizeVelocity.onValueChange = [this] { repaint(); };
            humanizeTiming.onValueChange = [this] { repaint(); };
            regeneratePreview.setButtonText(juce::String::fromUTF8("NOUVEL APERÇU"));
            regeneratePreview.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff263544));
            regeneratePreview.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdcb66f));
            regeneratePreview.onClick = [this] { ++previewSeed; statusMessage.clear(); repaint(); };
            addAndMakeVisible(regeneratePreview);
                        applyHumanize.setButtonText(juce::String::fromUTF8("APPLIQUER L'HUMANISATION"));
            applyHumanize.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff78603c));
            applyHumanize.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            applyHumanize.onClick = [this]
            {
                if (previewNotes.empty())
                {
                    statusMessage = juce::String::fromUTF8("Aucune note sélectionnée.");
                    repaint(); return;
                }
                if (onAction) statusMessage = onAction(24, (int)humanizeVelocity.getValue(), (int)humanizeTiming.getValue(), previewSeed);
                repaint();
            };
            addAndMakeVisible(applyHumanize);
            undoHumanize.setButtonText(juce::String::fromUTF8("ANNULER LA DERNIÈRE APPLICATION"));
            undoHumanize.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff263544));
            undoHumanize.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffdcb66f));
            undoHumanize.onClick = [this]
            {
                if (onAction) statusMessage = onAction(25, 0, 0, 0);
                repaint();
            };
            addAndMakeVisible(undoHumanize);
            for (auto* slider : { &quantizeStrength, &quantizeSwing, &quantizeGroove })
            {
                slider->setRange(0.0, 100.0, 1.0);
                slider->setTextValueSuffix(" %");
                slider->setSliderStyle(juce::Slider::LinearHorizontal);
                slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 26);
                slider->setColour(juce::Slider::trackColourId, juce::Colour(0xff348fc2));
                addAndMakeVisible(*slider);
            }
            quantizeStrength.setValue(100.0, juce::dontSendNotification);
            quantizeSwing.setRange(50.0, 75.0, 1.0);
            quantizeSwing.setValue(50.0, juce::dontSendNotification);
            quantizeGroove.setValue(25.0, juce::dontSendNotification);
            applyQuantize.setButtonText(juce::String::fromUTF8("APPLIQUER LA QUANTIFICATION"));
            applyQuantize.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff205d85));
            applyQuantize.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            applyQuantize.setEnabled(hasSelection);
            applyQuantize.onClick = [this]
            {
                if (onQuantize) onQuantize(quantizeStrength.getValue() / 100.0,
                    quantizeSwing.getValue() / 100.0, quantizeGroove.getValue() / 100.0);
            };
            addAndMakeVisible(applyQuantize);
            static const char* chordNames[] = {
                "INVERSION ASCENDANTE  +12", "INVERSION DESCENDANTE  -12",
                "VOICING OUVERT", "VOICING DROP",
                "DROP 2", "DROP 3", "ÉTENDRE LES VOIX +12", "ABAISSER LES VOIX -12"
            };
            recognizeChord.setButtonText(juce::String::fromUTF8("IDENTIFIER L'ACCORD"));
            recognizeChord.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff205d85));
            recognizeChord.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            recognizeChord.setEnabled(hasSelection);
            recognizeChord.onClick = [this]
            {
                if (onRecognizeChord) chordName = onRecognizeChord();
                repaint();
            };
            addAndMakeVisible(recognizeChord);
            for (int i = 0; i < 8; ++i)
            {
                auto& button = chordButtons[(size_t)i];
                button.setButtonText(juce::String::fromUTF8(chordNames[i]));
                button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff205d85));
                button.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
                button.setEnabled(hasSelection);
                button.onClick = [this, i] { if (onChordTransform) onChordTransform(i); };
                addAndMakeVisible(button);
            }
            static const char* rhythmNames[] = {
                "RÉPÉTITION ×2", "RÉPÉTITION ×4",
                "ARPÈGE MONTANT", "ARPÈGE DESCENDANT",
                "NOTES COURTES ×0,5", "NOTES LONGUES ×2"
            };
            for (int i = 0; i < 6; ++i)
            {
                auto& button = rhythmButtons[(size_t)i];
                button.setButtonText(juce::String::fromUTF8(rhythmNames[i]));
                button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff205d85));
                button.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
                button.setEnabled(hasSelection);
                button.onClick = [this, i] { if (onRhythmTool) onRhythmTool(i); };
                addAndMakeVisible(button);
            }
            expressiveAmount.setRange(1.0, 127.0, 1.0);
            expressiveAmount.setValue(100.0, juce::dontSendNotification);
            expressiveAmount.setSliderStyle(juce::Slider::LinearHorizontal);
            expressiveAmount.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 26);
            expressiveAmount.setColour(juce::Slider::trackColourId, juce::Colour(0xff348fc2));
            addAndMakeVisible(expressiveAmount);
            static const char* expressiveNames[] = {
                "VÉLOCITÉ = VALEUR", "VÉLOCITÉ + VALEUR",
                "STACCATO (50 %)", "LEGATO (95 %)",
                "CC1 MODULATION", "CC11 EXPRESSION",
                "CC64 SUSTAIN", "AFTERTOUCH CANAL"
            };
            for (int i = 0; i < 8; ++i)
            {
                auto& button = expressiveButtons[(size_t)i];
                button.setButtonText(juce::String::fromUTF8(expressiveNames[i]));
                button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff205d85));
                button.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
                button.setEnabled(hasSelection);
                button.onClick = [this, i]
                {
                    if (onExpressiveTool) onExpressiveTool(i, (int)expressiveAmount.getValue());
                };
                addAndMakeVisible(button);
            }
            for (auto* slider : { &euclideanSteps, &euclideanPulses })
            {
                slider->setRange(1.0, 32.0, 1.0);
                slider->setSliderStyle(juce::Slider::LinearHorizontal);
                slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 26);
                slider->setColour(juce::Slider::trackColourId, juce::Colour(0xff348fc2));
                addAndMakeVisible(*slider);
            }
            euclideanSteps.setRange(2.0, 32.0, 1.0);
            euclideanSteps.setValue(16.0, juce::dontSendNotification);
            euclideanPulses.setValue(5.0, juce::dontSendNotification);
            for (auto* slider : { &melodySteps, &melodyRange, &melodySeed })
            {
                slider->setSliderStyle(juce::Slider::LinearHorizontal);
                slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 26);
                slider->setColour(juce::Slider::trackColourId, juce::Colour(0xff348fc2));
                addAndMakeVisible(*slider);
            }
            melodySteps.setRange(2.0, 32.0, 1.0);
            melodySteps.setValue(8.0, juce::dontSendNotification);
            melodyRange.setRange(1.0, 24.0, 1.0);
            melodyRange.setValue(12.0, juce::dontSendNotification);
            melodySeed.setRange(0.0, 9999.0, 1.0);
            melodySeed.setValue(1.0, juce::dontSendNotification);
            generateMelody.setButtonText(juce::String::fromUTF8("GÉNÉRER UNE MÉLODIE"));
            generateMelody.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff205d85));
            generateMelody.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            generateMelody.setEnabled(hasSelection);
            generateMelody.onClick = [this]
            {
                if (onMelody) onMelody((int)melodySteps.getValue(), (int)melodyRange.getValue(), (int)melodySeed.getValue());
            };
            addAndMakeVisible(generateMelody);
            generateEuclidean.setButtonText(juce::String::fromUTF8("GÉNÉRER LE RYTHME"));
            generateEuclidean.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff205d85));
            generateEuclidean.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            generateEuclidean.setEnabled(hasSelection);
            generateEuclidean.onClick = [this]
            {
                if (onEuclidean) onEuclidean((int)euclideanSteps.getValue(), (int)euclideanPulses.getValue());
            };
            addAndMakeVisible(generateEuclidean);
            noteProbability.setRange(0.0, 100.0, 1.0);
            noteProbability.setValue(100.0, juce::dontSendNotification);
            noteProbability.setTextValueSuffix(" %");
            noteProbability.setSliderStyle(juce::Slider::LinearHorizontal);
            noteProbability.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 26);
            noteProbability.setColour(juce::Slider::trackColourId, juce::Colour(0xff348fc2));
            addAndMakeVisible(noteProbability);
            applyProbability.setButtonText(juce::String::fromUTF8("APPLIQUER LA PROBABILITÉ"));
            applyProbability.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff205d85));
            applyProbability.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            applyProbability.setEnabled(hasSelection);
            applyProbability.onClick = [this]
            {
                if (onProbability) onProbability((int)noteProbability.getValue());
            };
            addAndMakeVisible(applyProbability);
            startVelocity.setValue(40.0, juce::dontSendNotification);
            endVelocity.setValue(120.0, juce::dontSendNotification);
            setSize(600, 720);
            refreshCategory();
        }
        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xff111923));
            g.setColour(juce::Colour(0xffc6a76b));
            g.fillRoundedRectangle(24.0f, 26.0f, 4.0f, 46.0f, 2.0f);
            g.setColour(juce::Colour(0xfff4f5f8));
            g.setFont(juce::Font(25.0f, juce::Font::bold));
            g.drawText(juce::String::fromUTF8("ÉDITION MIDI"), 42, 26, 520, 34, juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff98a9ba));
            g.setFont(juce::Font(14.0f));
            g.drawText(currentCategory == 2 ? "LIBERTY / VÉLOCITÉ / COURBES" : "LIBERTY / PIANO ROLL / CENTRE D'ÉDITION", 26, 78, 540, 24,
                juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff344250));
            g.drawHorizontalLine(113, 26.0f, 574.0f);
            if (currentCategory == 0)
            {
                g.setColour(juce::Colour(0xffd4e0e9));
                g.setFont(juce::Font(15.0f));
                g.drawText(juce::String::fromUTF8("SÉLECTION INTELLIGENTE"), 26, 390, getWidth() - 52, 30, juce::Justification::centredLeft);
                g.drawText(juce::String::fromUTF8("MINIMUM"), 26, 482, 125, 30, juce::Justification::centredLeft);
                g.drawText(juce::String::fromUTF8("MAXIMUM"), 26, 530, 125, 30, juce::Justification::centredLeft);
            }
            if (currentCategory == 8)
            {
                g.setColour(juce::Colour(0xffd4e0e9));
                g.setFont(juce::Font(17.0f));
                g.drawText(juce::String::fromUTF8("RYTHMES EUCLIDIENS"), 26, 191, getWidth() - 52, 34, juce::Justification::centredLeft);
                g.setFont(juce::Font(15.0f));
                g.drawText(juce::String::fromUTF8("NOMBRE DE PAS"), 26, 260, 190, 36, juce::Justification::centredLeft);
                g.drawText(juce::String::fromUTF8("FRAPPES"), 26, 352, 190, 36, juce::Justification::centredLeft);
                g.setColour(juce::Colour(0xff98a9ba));
                g.setFont(juce::Font(13.0f));
                g.drawText(juce::String::fromUTF8("PAS MÉLODIQUES"), 26, 404, 190, 30, juce::Justification::centredLeft);
                g.drawText(juce::String::fromUTF8("ÉTENDUE (DEMI-TONS)"), 26, 448, 190, 30, juce::Justification::centredLeft);
                g.drawText(juce::String::fromUTF8("GRAINE MÉLODIQUE"), 26, 485, 190, 30, juce::Justification::centredLeft);
                g.drawText(juce::String::fromUTF8("PROBABILITÉ DE LECTURE"),
                    26, 590, 195, 32, juce::Justification::centredLeft);
            }
            if (currentCategory == 7)
            {
                g.setColour(juce::Colour(0xffd4e0e9));
                g.setFont(juce::Font(15.0f));
                g.drawText(juce::String::fromUTF8("INTENSITÉ / VALEUR"), 26, 208, 190, 32, juce::Justification::centredLeft);
                g.setColour(juce::Colour(0xff98a9ba));
                g.setFont(juce::Font(13.0f));
                g.drawText(juce::String::fromUTF8("Vélocité, articulation et contrôleurs aux attaques des notes sélectionnées."),
                    26, 175, getWidth() - 52, 28, juce::Justification::centredLeft);
            }
            if (currentCategory == 6)
            {
                g.setColour(juce::Colour(0xffd4e0e9));
                g.setFont(juce::Font(15.0f));
                g.drawText(juce::String::fromUTF8("RÉPÉTITIONS · ARPÈGES · DURÉES"),
                    26, 185, getWidth() - 52, 30, juce::Justification::centredLeft);
                g.setColour(juce::Colour(0xff98a9ba));
                g.setFont(juce::Font(13.0f));
                g.drawText(juce::String::fromUTF8("Opérations sur les notes sélectionnées, annulables en une étape."),
                    26, 600, getWidth() - 52, 28, juce::Justification::centredLeft);
            }
            if (currentCategory == 5)
            {
                g.setColour(juce::Colour(0xffd4e0e9));
                g.setFont(juce::Font(15.0f));
                g.drawText(juce::String::fromUTF8("TRANSFORMATIONS DES ACCORDS SÉLECTIONNÉS"),
                    26, 178, getWidth() - 52, 30, juce::Justification::centredLeft);
                g.setColour(juce::Colour(0xff98a9ba));
                g.setFont(juce::Font(13.0f));
                g.drawText(juce::String::fromUTF8("Accords simultanés, canal par canal. Durées et vélocités conservées."),
                    26, 586, getWidth() - 52, 30, juce::Justification::centredLeft);
                g.setColour(juce::Colour(0xffd4e0e9));
                g.drawText(chordName, 26, 654, getWidth() - 52, 28, juce::Justification::centred);
            }
            if (currentCategory == 4)
            {
                g.setColour(juce::Colour(0xffd4e0e9));
                g.setFont(juce::Font(16.0f));
                g.drawText(juce::String::fromUTF8("FORCE"), 26, 224, 160, 32, juce::Justification::centredLeft);
                g.drawText(juce::String::fromUTF8("SWING"), 26, 314, 160, 32, juce::Justification::centredLeft);
                g.drawText(juce::String::fromUTF8("PRÉSERVER LE GROOVE"), 26, 404, 220, 32, juce::Justification::centredLeft);
                g.setColour(juce::Colour(0xff98a9ba));
                g.setFont(juce::Font(13.0f));
                g.drawText(juce::String::fromUTF8("Quantification sur la grille SNAP active. Les durées sont conservées."),
                    26, 182, getWidth() - 52, 30, juce::Justification::centredLeft);
                g.drawText(juce::String::fromUTF8("50 % = swing neutre. Préservation 100 % = timing original."),
                    26, 490, getWidth() - 52, 28, juce::Justification::centredLeft);
            }
            if (currentCategory == 3)
            {
                g.setColour(juce::Colour(0xffd4e0e9));
                g.setFont(juce::Font(15.0f));
                g.drawText(juce::String::fromUTF8("VÉLOCITÉ ±"), 26, 235, 158, 32, juce::Justification::centredLeft);
                g.drawText(juce::String::fromUTF8("TIMING ± (ticks)"), 26, 310, 158, 32, juce::Justification::centredLeft);
                g.setColour(juce::Colour(0xff98a9ba));
                g.setFont(juce::Font(13.0f));
                g.drawText(juce::String::fromUTF8("Variations aléatoires indépendantes sur les notes sélectionnées."), 26, 185, getWidth() - 52, 25, juce::Justification::centredLeft);
                g.drawText(juce::String::fromUTF8("0 = désactivé. Les hauteurs et durées des notes sont conservées."), 26, 450, getWidth() - 52, 30, juce::Justification::centredLeft);
                if (statusMessage.isNotEmpty())
                {
                    g.setColour(juce::Colour(0xffe9b96e));
                    g.drawText(statusMessage, 26, 475, getWidth() - 52, 25, juce::Justification::centredLeft);
                }
                const juce::Rectangle<float> graph(26.0f, 506.0f, (float)getWidth() - 52.0f, 140.0f);
                g.setColour(juce::Colour(0xff202f40));
                g.fillRoundedRectangle(graph, 7.0f);
                g.setFont(juce::Font(12.0f));
                g.setColour(juce::Colour(0xffaabcca));
                g.drawText(juce::String::fromUTF8("GRIS : ORIGINAL     OR : APERÇU HUMANISÉ"), 36, 511, getWidth() - 72, 20, juce::Justification::centredLeft);
                if (previewNotes.empty())
                {
                    g.setColour(juce::Colour(0xff98a9ba));
                    g.drawText(juce::String::fromUTF8("Sélectionnez des notes pour prévisualiser."), 36, 565, getWidth() - 72, 30, juce::Justification::centred);
                }
                else
                {
                    const auto minTick = std::min_element(previewNotes.begin(), previewNotes.end(),
                        [](const auto& a, const auto& b) { return a.startTick < b.startTick; })->startTick;
                    const auto maxTick = std::max_element(previewNotes.begin(), previewNotes.end(),
                        [](const auto& a, const auto& b) { return a.startTick < b.startTick; })->startTick;
                    const double span = (double)juce::jmax<std::int64_t>(1, maxTick - minTick);
                    const float scale = (graph.getWidth() - 68.0f) / (float)span;
                    const int count = juce::jmin(64, (int)previewNotes.size());
                    for (int n = 0; n < count; ++n)
                    {
                        const auto& note = previewNotes[(size_t)n];
                        const int tickVariation = (((n * 37 + previewSeed * 19) % 17) - 8)
                            * (int)humanizeTiming.getValue() / 8;
                        const int velocityVariation = (((n * 13 + previewSeed * 7) % 13) - 6)
                            * (int)humanizeVelocity.getValue() / 6;
                        const float originalX = graph.getX() + 19.0f + (float)(note.startTick - minTick) * scale;
                        const float previewX = graph.getX() + 19.0f
                            + (float)(juce::jmax<std::int64_t>(0, note.startTick + tickVariation) - minTick) * scale;
                        const float originalY = graph.getBottom() - 14.0f - (float)note.velocity / 127.0f * 91.0f;
                        const float previewY = graph.getBottom() - 14.0f
                            - (float)juce::jlimit(1, 127, (int)note.velocity + velocityVariation) / 127.0f * 91.0f;
                        g.setColour(juce::Colour(0xff617789));
                        g.fillRoundedRectangle(originalX, originalY, 7.0f, 7.0f, 2.0f);
                        g.setColour(juce::Colour(0xffdcb66f));
                        g.fillRoundedRectangle(previewX, previewY, 7.0f, 7.0f, 2.0f);
                    }
                }
            }
            if (currentCategory == 2)
            {
                g.setColour(juce::Colour(0xffd4e0e9));
                g.setFont(juce::Font(14.0f));
                g.drawText(juce::String::fromUTF8("FORME"), 26, 411, 115, 25, juce::Justification::centredLeft);
                g.drawText(juce::String::fromUTF8("DÉPART"), 26, 448, 115, 25, juce::Justification::centredLeft);
                g.drawText(juce::String::fromUTF8("ARRIVÉE"), 26, 495, 115, 25, juce::Justification::centredLeft);
                g.drawText(juce::String::fromUTF8("COURBURE"), 26, 530, 115, 25, juce::Justification::centredLeft);
                // Keep the velocity preview in its own dark panel, below the reset control.
                const juce::Rectangle<float> preview(26.0f, 596.0f, (float)getWidth() - 52.0f, 78.0f);
                g.setColour(juce::Colour(0xff202f40));
                g.fillRoundedRectangle(preview, 7.0f);
                g.setColour(juce::Colour(0xff516476).withAlpha(0.55f));
                for (int line = 1; line <= 3; ++line)
                {
                    const float y = preview.getY() + line * preview.getHeight() / 4.0f;
                    g.drawHorizontalLine((int)y, preview.getX() + 10.0f, preview.getRight() - 10.0f);
                    const float x = preview.getX() + line * preview.getWidth() / 4.0f;
                    g.drawVerticalLine((int)x, preview.getY() + 5.0f, preview.getBottom() - 5.0f);
                }
                g.setColour(juce::Colour(0xffa7b9c8));
                g.setFont(juce::Font(10.0f));
                g.drawText("127", (int)preview.getX() + 8, (int)preview.getY() + 3, 27, 12, juce::Justification::centredLeft);
                g.drawText("1", (int)preview.getX() + 8, (int)preview.getBottom() - 16, 27, 12, juce::Justification::centredLeft);
                juce::Path curve;
                for (int i = 0; i <= 64; ++i)
                {
                    const double t = (double)i / 64.0;
                    const int type = curveSelector.getSelectedId() - 1;
                    const double shaped = type == 1 ? t * t : type == 2 ? std::sqrt(t)
                        : type == 3 ? t * t * (3.0 - 2.0 * t) : t;
                    const double adjusted = std::pow(shaped, curvature.getValue() / 100.0);
                    const double value = startVelocity.getValue()
                        + adjusted * (endVelocity.getValue() - startVelocity.getValue());
                    const float x = preview.getX() + 10.0f + (float)t * (preview.getWidth() - 20.0f);
                    const float y = preview.getBottom() - 5.0f - (float)(value / 127.0) * (preview.getHeight() - 10.0f);
                    if (i == 0) curve.startNewSubPath(x, y);
                    else curve.lineTo(x, y);
                }
                g.setColour(juce::Colour(0xffc6a76b));
                g.strokePath(curve, juce::PathStrokeType(3.0f));
                // Endpoint markers and MIDI values stay inside the preview panel.
                const float leftX = preview.getX() + 10.0f;
                const float rightX = preview.getRight() - 10.0f;
                const float graphHeight = preview.getHeight() - 10.0f;
                const float startY = preview.getBottom() - 5.0f - (float)(startVelocity.getValue() / 127.0) * graphHeight;
                const float endY = preview.getBottom() - 5.0f - (float)(endVelocity.getValue() / 127.0) * graphHeight;
                g.fillEllipse(leftX - 3.0f, startY - 3.0f, 6.0f, 6.0f);
                g.fillEllipse(rightX - 3.0f, endY - 3.0f, 6.0f, 6.0f);
                const int type = curveSelector.getSelectedId() - 1;
                const double midShape = type == 1 ? 0.25 : type == 2 ? std::sqrt(0.5)
                    : type == 3 ? 0.5 : 0.5;
                const double midValue = startVelocity.getValue()
                    + std::pow(midShape, curvature.getValue() / 100.0)
                        * (endVelocity.getValue() - startVelocity.getValue());
                const float midX = (leftX + rightX) * 0.5f;
                const float midY = preview.getBottom() - 5.0f - (float)(midValue / 127.0) * graphHeight;
                g.setColour(juce::Colour(0xfff0d9a6));
                g.fillEllipse(midX - 4.5f, midY - 4.5f, 9.0f, 9.0f);
                g.setColour(juce::Colour(0xfff2e6ce));
                g.setFont(juce::Font(11.0f, juce::Font::bold));
                const int midpointVelocity = juce::jlimit(1, 127, juce::roundToInt(midValue));
                const int labelY = (int)(midY < preview.getCentreY() ? midY + 7.0f : midY - 19.0f);
                g.drawText(juce::String(midpointVelocity), (int)midX - 20, labelY, 40, 15,
                    juce::Justification::centred);
                g.setColour(juce::Colour(0xffc6a76b));
                g.setColour(juce::Colour(0xffdce5ed));
                g.setFont(juce::Font(11.0f));
                g.drawText(juce::String((int)startVelocity.getValue()), (int)leftX + 6, (int)preview.getY() + 2, 36, 15, juce::Justification::centredLeft);
                g.drawText(juce::String((int)endVelocity.getValue()), (int)rightX - 42, (int)preview.getY() + 2, 36, 15, juce::Justification::centredRight);
            }
            g.setColour(juce::Colour(0xff8194a5));
            g.setFont(juce::Font(13.0f));
            g.drawText(juce::String::fromUTF8("LIBERTY  •  ÉDITION NON DESTRUCTIVE DE LA POSITION DES NOTES"),
                26, 690, 548, 20, juce::Justification::centredLeft);
        }
        void mouseDown(const juce::MouseEvent& event) override
        {
            if (currentCategory != 2) return;
            const juce::Rectangle<float> preview(26.0f, 596.0f, (float)getWidth() - 52.0f, 78.0f);
            if (!preview.contains(event.position)) return;
            const float leftX = preview.getX() + 10.0f;
            const float rightX = preview.getRight() - 10.0f;
            const float graphHeight = preview.getHeight() - 10.0f;
            const float startY = preview.getBottom() - 5.0f - (float)(startVelocity.getValue() / 127.0) * graphHeight;
            const float endY = preview.getBottom() - 5.0f - (float)(endVelocity.getValue() / 127.0) * graphHeight;
            const float leftDistance = event.position.getDistanceFrom({ leftX, startY });
            const float rightDistance = event.position.getDistanceFrom({ rightX, endY });
            const int type = curveSelector.getSelectedId() - 1;
            const double midShape = type == 1 ? 0.25 : type == 2 ? std::sqrt(0.5) : 0.5;
            const double midValue = startVelocity.getValue()
                + std::pow(midShape, curvature.getValue() / 100.0)
                    * (endVelocity.getValue() - startVelocity.getValue());
            const float midY = preview.getBottom() - 5.0f - (float)(midValue / 127.0) * graphHeight;
            const float midDistance = event.position.getDistanceFrom({ (leftX + rightX) * 0.5f, midY });
            draggedVelocityEndpoint = midDistance <= 16.0f ? 2
                : leftDistance <= 18.0f && leftDistance <= rightDistance ? 0
                : rightDistance <= 18.0f ? 1 : -1;
        }
        void mouseDrag(const juce::MouseEvent& event) override
        {
            if (currentCategory != 2 || draggedVelocityEndpoint < 0) return;
            const float graphBottom = 596.0f + 78.0f - 5.0f;
            const float graphHeight = 78.0f - 10.0f;
            const int velocity = juce::jlimit(1, 127, juce::roundToInt((graphBottom - event.position.y) * 127.0f / graphHeight));
            if (draggedVelocityEndpoint == 2)
            {
                const double start = startVelocity.getValue();
                const double delta = endVelocity.getValue() - start;
                if (std::abs(delta) < 1.0) return;
                const double normalized = juce::jlimit(0.001, 0.999, (velocity - start) / delta);
                const int type = curveSelector.getSelectedId() - 1;
                const double midShape = type == 1 ? 0.25 : type == 2 ? std::sqrt(0.5) : 0.5;
                const double exponent = std::log(normalized) / std::log(midShape);
                curvature.setValue(juce::jlimit(25.0, 200.0, exponent * 100.0), juce::sendNotification);
            }
            else
                (draggedVelocityEndpoint == 0 ? startVelocity : endVelocity).setValue(velocity, juce::sendNotification);
        }
        void mouseUp(const juce::MouseEvent&) override
        {
            draggedVelocityEndpoint = -1;
        }
        void resized() override
        {
            const int available = getWidth() - 52;
            const int tabWidth = (available - 48) / 9;
            for (int i = 0; i < 9; ++i)
                categoryButtons[(size_t)i].setBounds(26 + i * (tabWidth + 6), 127, tabWidth, 38);
            selectionCriterion.setBounds(26, 426, available, 32);
            selectionMode.setBounds(26, 574, available, 32);
            selectionMin.setBounds(160, 482, getWidth() - 186, 30);
            selectionMax.setBounds(160, 530, getWidth() - 186, 30);
            applySelection.setBounds(26, 624, available, 42);
            melodySteps.setBounds(230, 404, getWidth() - 256, 30);
            melodyRange.setBounds(230, 448, getWidth() - 256, 30);
            melodySeed.setBounds(230, 485, getWidth() - 256, 30);
            generateMelody.setBounds(26, 526, available, 38);
            noteProbability.setBounds(230, 590, getWidth() - 256, 32);
            applyProbability.setBounds(26, 630, available, 38);
            euclideanSteps.setBounds(230, 260, getWidth() - 256, 36);
            euclideanPulses.setBounds(230, 352, getWidth() - 256, 36);
            generateEuclidean.setBounds(26, 310, available, 38);
            expressiveAmount.setBounds(215, 208, getWidth() - 241, 32);
            for (int i = 0; i < 8; ++i)
                expressiveButtons[(size_t)i].setBounds(26 + (i % 2) * ((available + 10) / 2),
                    285 + (i / 2) * 83, (available - 10) / 2, 55);
            for (int i = 0; i < 6; ++i)
                rhythmButtons[(size_t)i].setBounds(26 + (i % 2) * ((available + 10) / 2),
                    235 + (i / 2) * 115, (available - 10) / 2, 64);
            recognizeChord.setBounds(26, 615, available, 32);
            for (int i = 0; i < 8; ++i)
            {
                const int col = i % 2;
                const int row = i / 2;
                chordButtons[(size_t)i].setBounds(26 + col * ((available + 10) / 2),
                    240 + row * 84, (available - 10) / 2, 58);
            }
            quantizeStrength.setBounds(235, 224, getWidth() - 261, 32);
            quantizeSwing.setBounds(235, 314, getWidth() - 261, 32);
            quantizeGroove.setBounds(235, 404, getWidth() - 261, 32);
            applyQuantize.setBounds(26, 545, available, 44);
            curveSelector.setBounds(145, 411, getWidth() - 171, 28);
            curvature.setBounds(145, 530, getWidth() - 239, 25);
            resetCurvature.setBounds(getWidth() - 85, 530, 59, 25);
            reverseVelocity.setBounds(26, 559, (getWidth() - 60) / 2, 27);
            resetVelocity.setBounds(34 + (getWidth() - 60) / 2, 559, (getWidth() - 60) / 2, 27);
            humanizeVelocity.setBounds(190, 235, getWidth() - 216, 32);
            humanizeTiming.setBounds(190, 310, getWidth() - 216, 32);
            applyHumanize.setBounds(26, 387, available, 40);
            regeneratePreview.setBounds(26, 654, (available - 8) / 2, 30);
            undoHumanize.setBounds(34 + (available - 8) / 2, 654, (available - 8) / 2, 30);
            startVelocity.setBounds(145, 448, getWidth() - 171, 28);
            endVelocity.setBounds(145, 495, getWidth() - 171, 28);
            int shown = 0;
            for (auto& button : buttons)
            {
                if (!button->isVisible()) continue;
                if (currentCategory == 1)
                {
                    // Twelve transformations fit in two columns without leaving the dialog.
                    const int columnWidth = (available - 12) / 2;
                    const int column = shown / 7;
                    const int row = shown % 7;
                    button->setBounds(26 + column * (columnWidth + 12),
                        184 + row * 65, columnWidth, 53);
                }
                else
                    button->setBounds(26, currentCategory == 2 ? 180 + shown * 44 : 184 + shown * 49,
                        available, currentCategory == 2 ? 38 : 42);
                ++shown;
            }
        }
    private:
        void refreshCategory()
        {
            for (int i = 0; i < 9; ++i)
            {
                categoryButtons[(size_t)i].setColour(juce::TextButton::buttonColourId,
                    i == currentCategory ? juce::Colour(0xff78603c) : juce::Colour(0xff263544));
                categoryButtons[(size_t)i].setColour(juce::TextButton::textColourOffId,
                    i == currentCategory ? juce::Colours::white : juce::Colour(0xffb9cbd7));
            }
            for (int i = 0; i < (int)buttons.size(); ++i)
                buttons[(size_t)i]->setVisible(currentCategory == 0 ? i < 4 :
                    (currentCategory == 1 ? ((i >= 4 && i < 8) || (i >= 13 && i < 21) || i >= 23) : (currentCategory == 2 && i >= 8 && i < 13)));
            for (auto& button : chordButtons) button.setVisible(currentCategory == 5);
            for (auto& button : rhythmButtons) button.setVisible(currentCategory == 6);
            for (auto& button : expressiveButtons) button.setVisible(currentCategory == 7);
            expressiveAmount.setVisible(currentCategory == 7);
            selectionCriterion.setVisible(currentCategory == 0);
            selectionMode.setVisible(currentCategory == 0);
            selectionMin.setVisible(currentCategory == 0);
            selectionMax.setVisible(currentCategory == 0);
            applySelection.setVisible(currentCategory == 0);
            noteProbability.setVisible(currentCategory == 8);
            applyProbability.setVisible(currentCategory == 8);
            melodySteps.setVisible(currentCategory == 8);
            melodyRange.setVisible(currentCategory == 8);
            melodySeed.setVisible(currentCategory == 8);
            generateMelody.setVisible(currentCategory == 8);
            euclideanSteps.setVisible(currentCategory == 8);
            euclideanPulses.setVisible(currentCategory == 8);
            generateEuclidean.setVisible(currentCategory == 8);
            recognizeChord.setVisible(currentCategory == 5);
            quantizeStrength.setVisible(currentCategory == 4);
            quantizeSwing.setVisible(currentCategory == 4);
            quantizeGroove.setVisible(currentCategory == 4);
            applyQuantize.setVisible(currentCategory == 4);
            humanizeVelocity.setVisible(currentCategory == 3);
            humanizeTiming.setVisible(currentCategory == 3);
            applyHumanize.setVisible(currentCategory == 3);
            regeneratePreview.setVisible(currentCategory == 3);
            undoHumanize.setVisible(currentCategory == 3);
            resetCurvature.setVisible(currentCategory == 2);
            curvature.setVisible(currentCategory == 2);
            reverseVelocity.setVisible(currentCategory == 2);
            resetVelocity.setVisible(currentCategory == 2);
            curveSelector.setVisible(currentCategory == 2);
            startVelocity.setVisible(currentCategory == 2);
            endVelocity.setVisible(currentCategory == 2);
            resized();
            repaint();
        }
        std::function<juce::String(int, int, int, int)> onAction;
        std::function<void(double, double, double)> onQuantize;
        std::function<void(int)> onChordTransform;
        std::array<juce::TextButton, 8> chordButtons;
        std::array<juce::TextButton, 6> rhythmButtons;
        std::array<juce::TextButton, 8> expressiveButtons;
        juce::Slider expressiveAmount, euclideanSteps, euclideanPulses, noteProbability, melodySteps, melodyRange, melodySeed;
        juce::ComboBox selectionCriterion, selectionMode;
        juce::Slider selectionMin, selectionMax;
        juce::TextButton applySelection;
        std::function<void(int, int, int, int)> onSelection;
        juce::TextButton generateMelody;
        std::function<void(int, int, int)> onMelody;
        juce::TextButton applyProbability;
        std::function<void(int)> onProbability;
        juce::TextButton generateEuclidean;
        std::function<void(int, int)> onEuclidean;
        std::function<void(int, int)> onExpressiveTool;
        std::function<void(int)> onRhythmTool;
        juce::TextButton recognizeChord;
        juce::String chordName;
        std::function<juce::String()> onRecognizeChord;
        std::vector<MidiEngine::NoteEvent> previewNotes;
        juce::String statusMessage;
        juce::Slider startVelocity, endVelocity, curvature, humanizeVelocity, humanizeTiming;
        juce::Slider quantizeStrength, quantizeSwing, quantizeGroove;
        juce::TextButton applyQuantize;
        juce::ComboBox curveSelector;
        juce::TextButton reverseVelocity, resetVelocity, resetCurvature, applyHumanize, regeneratePreview, undoHumanize;
        int previewSeed = 1;
        std::array<juce::TextButton, 9> categoryButtons;
        std::vector<std::unique_ptr<juce::TextButton>> buttons;
        int draggedVelocityEndpoint = -1;
        int currentCategory = 0;
    };

    int snapPitchToScale(int pitch) const noexcept
    {
        if (!scaleSnap) return pitch;
        static const int masks[5][12] = {
            { 1,0,1,0,1,1,0,1,0,1,0,1 },
            { 1,0,1,1,0,1,0,1,1,0,1,0 },
            { 1,0,1,0,1,0,0,1,0,1,0,0 },
            { 1,0,0,1,0,1,0,1,0,0,1,0 },
            { 1,0,0,1,0,1,1,1,0,0,1,0 }
        };
        pitch = juce::jlimit(0, 127, pitch);
        for (int distance = 0; distance < 12; ++distance)
            for (int candidate : { pitch - distance, pitch + distance })
                if (candidate >= 0 && candidate <= 127
                    && masks[scaleType][((candidate - scaleRoot) % 12 + 12) % 12])
                    return candidate;
        return pitch;
    }

    void frameClipPitches()
    {
        const auto notes = owner.getMidiEngine().getNotesCopy();
        if (notes.empty()) return;
        int minimum = 127, maximum = 0;
        for (const auto& note : notes)
        {
            minimum = juce::jmin(minimum, (int)note.pitch);
            maximum = juce::jmax(maximum, (int)note.pitch);
        }
        // Adapt the number of visible pitch rows, not just the scroll offset.
        // All pitch hit-tests and note rectangles use the same row count.
        const int span = maximum - minimum + 1;
        framedPitchRows = juce::jlimit(12, 128, span + 4);
        const int first = minimum - juce::jmax(0, (framedPitchRows - span) / 2);
        lowestVisibleKey = juce::jlimit(0, 128 - framedPitchRows, first);
        repaint();
    }

    void restoreClipPitchView()
    {
        lowestVisibleKey = juce::jlimit(0, 128 - visiblePitchRows(), pitchViewBeforeFrame);
        pitchViewFramed = false;
        framedPitchRows = 40;
        repaint();
    }

    void showScaleWindow()
    {
        struct ScaleContent final : juce::Component
        {
            juce::ComboBox tonic, scale;
            juce::ToggleButton highlight { juce::String::fromUTF8("Afficher la gamme sur le Piano Roll") };
            juce::ToggleButton snap { juce::String::fromUTF8("Snap à la gamme (notes dessinées et déplacées)") };
            juce::ToggleButton used { juce::String::fromUTF8("Repérer les notes utilisées dans le clip") };
            juce::ToggleButton focus { juce::String::fromUTF8("Mettre en avant les notes utilisées uniquement") };
            juce::TextButton frame { juce::String::fromUTF8("CADRER LES NOTES") };
            juce::TextButton restore { juce::String::fromUTF8("REVENIR À LA VUE INITIALE") };
            juce::Label title, help;
            std::function<void(int, int, bool, bool, bool, bool)> apply;
            std::function<void(bool)> viewAction;
            explicit ScaleContent(std::function<void(int, int, bool, bool, bool, bool)> callback,
                                  std::function<void(bool)> viewCallback,
                                  int root, int type, bool enabled, bool snapEnabled, bool usedEnabled, bool focusEnabled)
                : apply(std::move(callback)), viewAction(std::move(viewCallback))
            {
                static const char* roots[] = {
                    "Do", "Do#", "Ré", "Mi♭", "Mi", "Fa", "Fa#",
                    "Sol", "La♭", "La", "Si♭", "Si"
                };
                for (int i = 0; i < 12; ++i)
                    tonic.addItem(juce::String::fromUTF8(roots[i]), i + 1);
                tonic.setSelectedId(root + 1, juce::dontSendNotification);
                scale.addItem(juce::String::fromUTF8("Majeure"), 1);
                scale.addItem(juce::String::fromUTF8("Mineure naturelle"), 2);
                scale.addItem(juce::String::fromUTF8("Pentatonique majeure"), 3);
                scale.addItem(juce::String::fromUTF8("Pentatonique mineure"), 4);
                scale.addItem(juce::String::fromUTF8("Blues"), 5);
                scale.setSelectedId(type + 1, juce::dontSendNotification);
                highlight.setToggleState(enabled, juce::dontSendNotification);
                snap.setToggleState(snapEnabled, juce::dontSendNotification);
                used.setToggleState(usedEnabled, juce::dontSendNotification);
                focus.setToggleState(focusEnabled, juce::dontSendNotification);
                for (auto* combo : { &tonic, &scale })
                {
                    combo->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff263544));
                    combo->setColour(juce::ComboBox::textColourId, juce::Colour(0xfff3d48b));
                    addAndMakeVisible(*combo);
                }
                title.setText(juce::String::fromUTF8("COMPOSITION  /  GAMMES"),
                              juce::dontSendNotification);
                title.setFont(juce::Font(19.0f, juce::Font::bold));
                title.setColour(juce::Label::textColourId, juce::Colour(0xfff3d48b));
                help.setText(juce::String::fromUTF8("Repères visuels uniquement · aucun Snap modifié"),
                             juce::dontSendNotification);
                help.setColour(juce::Label::textColourId, juce::Colour(0xffb8c8d3));
                highlight.setColour(juce::ToggleButton::textColourId, juce::Colour(0xfff3d48b));
                addAndMakeVisible(title);
                addAndMakeVisible(help);
                addAndMakeVisible(highlight);
                snap.setColour(juce::ToggleButton::textColourId, juce::Colour(0xfff3d48b));
                addAndMakeVisible(snap);
                used.setColour(juce::ToggleButton::textColourId, juce::Colour(0xfff3d48b));
                addAndMakeVisible(used);
                focus.setColour(juce::ToggleButton::textColourId, juce::Colour(0xfff3d48b));
                addAndMakeVisible(focus);
                for (auto* button : { &frame, &restore })
                {
                    button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff263544));
                    button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xfff3d48b));
                    addAndMakeVisible(*button);
                }
                frame.onClick = [this] { if (viewAction) viewAction(true); };
                restore.onClick = [this] { if (viewAction) viewAction(false); };
                auto update = [this]
                {
                    if (apply)
                        apply(tonic.getSelectedId() - 1, scale.getSelectedId() - 1,
                              highlight.getToggleState(), snap.getToggleState(), used.getToggleState(), focus.getToggleState());
                };
                tonic.onChange = update;
                scale.onChange = update;
                highlight.onClick = update;
                snap.onClick = update;
                used.onClick = update;
                focus.onClick = update;
                setSize(440, 414);
            }
            void paint(juce::Graphics& g) override
            {
                g.fillAll(juce::Colour(0xff111923));
                g.setColour(juce::Colour(0xff344857));
                g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(8.0f), 9.0f, 1.0f);
            }
            void resized() override
            {
                title.setBounds(26, 18, getWidth() - 52, 32);
                help.setBounds(26, 52, getWidth() - 52, 24);
                tonic.setBounds(26, 94, 130, 34);
                scale.setBounds(168, 94, getWidth() - 194, 34);
                highlight.setBounds(26, 150, getWidth() - 52, 35);
                snap.setBounds(26, 193, getWidth() - 52, 35);
                used.setBounds(26, 236, getWidth() - 52, 35);
                focus.setBounds(26, 279, getWidth() - 52, 35);
                frame.setBounds(26, 337, 188, 32);
                restore.setBounds(222, 337, 192, 32);
            }
        };
        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = juce::String::fromUTF8("LIBERTY  ·  GAMMES");
        options.dialogBackgroundColour = juce::Colour(0xff111923);
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = false;
        options.resizable = false;
        options.content.setOwned(new ScaleContent(
            [safe = juce::Component::SafePointer<PianoRoll>(this)](int root, int type, bool enabled, bool snapEnabled, bool usedEnabled, bool focusEnabled)
            {
                if (safe == nullptr) return;
                safe->scaleRoot = juce::jlimit(0, 11, root);
                safe->scaleType = juce::jlimit(0, 4, type);
                safe->scaleHighlight = enabled;
                safe->scaleSnap = snapEnabled;
                safe->showUsedPitches = usedEnabled;
                safe->focusUsedPitches = focusEnabled;
                safe->repaint();
            }, [safe = juce::Component::SafePointer<PianoRoll>(this)](bool frame)
            {
                if (safe == nullptr) return;
                if (frame)
                {
                    if (!safe->pitchViewFramed)
                    {
                        safe->pitchViewBeforeFrame = safe->lowestVisibleKey;
                        safe->pitchViewFramed = true;
                    }
                    safe->frameClipPitches();
                }
                else if (safe->pitchViewFramed) safe->restoreClipPitchView();
            }, scaleRoot, scaleType, scaleHighlight, scaleSnap, showUsedPitches, focusUsedPitches));
        options.launchAsync();
    }

    void showProgressionWindow()
    {
        struct ProgressionContent final : juce::Component
        {
            juce::ComboBox key, pattern, octave, duration, mode;
            juce::ToggleButton smooth { juce::String::fromUTF8("Renversements automatiques") };
            juce::TextButton insertButton { juce::String::fromUTF8("INSÉRER LA PROGRESSION") };
            juce::Label heading, explanation;
            std::function<void(int, int, int, int, int, bool)> insert;
            explicit ProgressionContent(std::function<void(int, int, int, int, int, bool)> callback)
                : insert(std::move(callback))
            {
                static const char* roots[] = { "Do", "Do#", "Ré", "Mi♭", "Mi", "Fa",
                    "Fa#", "Sol", "La♭", "La", "Si♭", "Si" };
                for (int i = 0; i < 12; ++i)
                    key.addItem(juce::String::fromUTF8(roots[i]), i + 1);
                key.setSelectedId(1, juce::dontSendNotification);
                pattern.addItem("I - V - vi - IV", 1);
                pattern.addItem("I - IV - V - I", 2);
                pattern.addItem("vi - IV - I - V", 3);
                pattern.addItem("ii - V - I - I", 4);
                pattern.addItem("I - vi - IV - V", 5);
                pattern.addItem("I - iii - IV - V", 6);
                pattern.setSelectedId(1, juce::dontSendNotification);
                mode.addItem(juce::String::fromUTF8("Majeur"), 1);
                mode.addItem(juce::String::fromUTF8("Mineur naturel"), 2);
                mode.setSelectedId(1, juce::dontSendNotification);
                for (int oct = 2; oct <= 6; ++oct)
                    octave.addItem("Octave " + juce::String(oct), oct);
                octave.setSelectedId(4, juce::dontSendNotification);
                duration.addItem(juce::String::fromUTF8("1 mesure"), 1);
                duration.addItem(juce::String::fromUTF8("2 mesures"), 2);
                duration.addItem(juce::String::fromUTF8("2 temps"), 3);
                duration.setSelectedId(1, juce::dontSendNotification);
                for (auto* combo : { &key, &pattern, &octave, &duration, &mode })
                {
                    combo->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff263544));
                    combo->setColour(juce::ComboBox::textColourId, juce::Colour(0xfff3d48b));
                    addAndMakeVisible(*combo);
                }
                heading.setText(juce::String::fromUTF8("COMPOSITION  /  PROGRESSIONS"),
                    juce::dontSendNotification);
                heading.setFont(juce::Font(19.0f, juce::Font::bold));
                heading.setColour(juce::Label::textColourId, juce::Colour(0xfff3d48b));
                explanation.setText(juce::String::fromUTF8("4 accords successifs · durée et mode personnalisables"),
                    juce::dontSendNotification);
                explanation.setColour(juce::Label::textColourId, juce::Colour(0xffb8c8d3));
                addAndMakeVisible(heading);
                addAndMakeVisible(explanation);
                smooth.setColour(juce::ToggleButton::textColourId, juce::Colour(0xfff3d48b));
                addAndMakeVisible(smooth);
                insertButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff86652d));
                insertButton.onClick = [this]
                {
                    if (insert) insert(key.getSelectedId() - 1, pattern.getSelectedId() - 1,
                        octave.getSelectedId(), duration.getSelectedId(), mode.getSelectedId() - 1,
                        smooth.getToggleState());
                };
                addAndMakeVisible(insertButton);
                key.onChange = [this] { repaint(); };
                pattern.onChange = [this] { repaint(); };
                mode.onChange = [this] { repaint(); };
                octave.onChange = [this] { repaint(); };
                smooth.onClick = [this] { repaint(); };
                setSize(440, 594);
            }
            void paint(juce::Graphics& g) override
            {
                g.fillAll(juce::Colour(0xff111923));
                g.setColour(juce::Colour(0xff344857));
                g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(8.0f), 9.0f, 1.0f);
                static const int degrees[6][4] = {
                    { 0, 4, 5, 3 }, { 0, 3, 4, 0 }, { 5, 3, 0, 4 },
                    { 1, 4, 0, 0 }, { 0, 5, 3, 4 }, { 0, 2, 3, 4 }
                };
                static const int majorScale[7] = { 0, 2, 4, 5, 7, 9, 11 };
                static const int minorScale[7] = { 0, 2, 3, 5, 7, 8, 10 };
                static const char* noteNames[] = {
                    "Do", "Do#", "Ré", "Mi♭", "Mi", "Fa",
                    "Fa#", "Sol", "La♭", "La", "Si♭", "Si"
                };
                const bool minor = mode.getSelectedId() == 2;
                const int* scale = minor ? minorScale : majorScale;
                const int minorDegrees[6][4] = {
                    { 0, 5, 2, 6 }, { 0, 3, 6, 0 }, { 5, 3, 0, 6 },
                    { 1, 4, 0, 0 }, { 0, 5, 3, 6 }, { 0, 2, 3, 6 }
                };
                const int minorTriad[7] = { 1, 2, 0, 1, 1, 0, 0 };
                const int selectedPattern = juce::jlimit(0, 5, pattern.getSelectedId() - 1);
                const int selectedKey = juce::jlimit(0, 11, key.getSelectedId() - 1);
                g.setFont(juce::Font(12.0f));
                g.setColour(juce::Colour(0xffb8c8d3));
                g.drawText(juce::String::fromUTF8("PRÉVISUALISATION · 4 MESURES"),
                    26, 238, getWidth() - 52, 20, juce::Justification::centredLeft);
                std::array<int, 3> previous {};
                for (int bar = 0; bar < 4; ++bar)
                {
                    const int degree = minor ? minorDegrees[selectedPattern][bar] : degrees[selectedPattern][bar];
                    const int x = 26 + bar * 98;
                    g.setColour(juce::Colour(0xff263544));
                    g.fillRoundedRectangle((float)x, 267.0f, 92.0f, 44.0f, 5.0f);
                    g.setColour(juce::Colour(0xfff3d48b));
                    const juce::String chord = juce::String::fromUTF8(
                        noteNames[(selectedKey + scale[degree]) % 12])
                        + (minor ? (minorTriad[degree] == 1 ? "m" : minorTriad[degree] == 2 ? "dim" : "")
                            : ((degree == 1 || degree == 2 || degree == 5) ? "m" : ""));
                    g.drawFittedText(chord, x + 4, 271, 84, 35,
                        juce::Justification::centred, 1);
                    std::array<int, 3> original {};
                    const int basePitch = 60 + (juce::jlimit(2, 6, octave.getSelectedId()) - 4) * 12;
                    for (int third = 0; third < 3; ++third)
                    {
                        const int step = degree + third * 2;
                        original[(size_t)third] = basePitch + selectedKey
                            + scale[step % 7] + (step / 7) * 12;
                    }
                    auto pitches = original;
                    if (smooth.getToggleState() && bar > 0)
                    {
                        int bestMovement = 100000;
                        for (int inversion = 0; inversion < 3; ++inversion)
                        {
                            auto candidate = original;
                            for (int turn = 0; turn < inversion; ++turn)
                            {
                                const int moved = candidate[0] + 12;
                                candidate[0] = candidate[1];
                                candidate[1] = candidate[2];
                                candidate[2] = moved;
                            }
                            for (int shift = -2; shift <= 1; ++shift)
                            {
                                auto voiced = candidate;
                                int movement = 0;
                                for (int n = 0; n < 3; ++n)
                                {
                                    voiced[(size_t)n] += shift * 12;
                                    movement += std::abs(voiced[(size_t)n] - previous[(size_t)n]);
                                }
                                if (movement < bestMovement)
                                {
                                    bestMovement = movement;
                                    pitches = voiced;
                                }
                            }
                        }
                    }
                    previous = pitches;
                    g.setFont(juce::Font(11.0f));
                    g.setColour(juce::Colour(0xffb8c8d3));
                    for (int n = 0; n < 3; ++n)
                    {
                        const auto noteName = juce::MidiMessage::getMidiNoteName(
                            pitches[(size_t)n], true, true, 3);
                        g.drawFittedText(noteName, x + 2, 320 + n * 22, 88, 20,
                            juce::Justification::centred, 1);
                    }
                    // Read-only compact keyboard, centred on this chord's actual register.
                    int lowest = pitches[0];
                    for (int n = 1; n < 3; ++n)
                        lowest = juce::jmin(lowest, pitches[(size_t)n]);
                    const int first = (lowest / 12) * 12;
                    const float whiteWidth = 92.0f / 14.0f;
                    const float keyboardY = 405.0f;
                    static const int whites[7] = { 0, 2, 4, 5, 7, 9, 11 };
                    static const int blacks[7] = { 1, 3, -1, 6, 8, 10, -1 };
                    auto isTone = [&](int note)
                    {
                        for (int n = 0; n < 3; ++n)
                            if (pitches[(size_t)n] == note) return true;
                        return false;
                    };
                    for (int n = 0; n < 14; ++n)
                    {
                        const int pitch = first + (n / 7) * 12 + whites[n % 7];
                        g.setColour(isTone(pitch) ? juce::Colour(0xffe1b96f)
                            : juce::Colour(0xffd5dce0));
                        g.fillRoundedRectangle((float)x + n * whiteWidth + 0.4f,
                            keyboardY, whiteWidth - 0.8f, 51.0f, 1.5f);
                    }
                    for (int n = 0; n < 14; ++n)
                    {
                        if (blacks[n % 7] < 0) continue;
                        const int pitch = first + (n / 7) * 12 + blacks[n % 7];
                        g.setColour(isTone(pitch) ? juce::Colour(0xffdba952)
                            : juce::Colour(0xff293643));
                        g.fillRoundedRectangle((float)x + (n + 1) * whiteWidth - whiteWidth * 0.32f,
                            keyboardY, whiteWidth * 0.64f, 30.0f, 1.5f);
                    }
                    g.setFont(juce::Font(12.0f));
                }
            }
            void resized() override
            {
                heading.setBounds(26, 18, getWidth() - 52, 32);
                explanation.setBounds(26, 52, getWidth() - 52, 27);
                key.setBounds(26, 99, 130, 34);
                pattern.setBounds(168, 99, getWidth() - 194, 34);
                octave.setBounds(26, 145, 185, 32);
                duration.setBounds(219, 145, getWidth() - 245, 32);
                mode.setBounds(26, 192, getWidth() - 52, 32);
                smooth.setBounds(26, 332, getWidth() - 52, 28);
                insertButton.setBounds(26, 532, getWidth() - 52, 42);
            }
        };
        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = juce::String::fromUTF8("LIBERTY  ·  PROGRESSIONS");
        options.dialogBackgroundColour = juce::Colour(0xff111923);
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = false;
        options.resizable = false;
        options.content.setOwned(new ProgressionContent(
            [safe = juce::Component::SafePointer<PianoRoll>(this)](int key, int pattern, int octave, int duration, int mode, bool smooth)
            {
                if (safe != nullptr) safe->insertChordProgression(key, pattern, octave, duration, mode, smooth);
            }));
        options.launchAsync();
    }

    void insertChordProgression(int key, int pattern, int octave, int duration, int mode, bool smooth)
    {
        // Diatonic major-key progression, four bars, starting at the first
        // selected note (or the beginning of the clip). Existing notes remain.
        static const int progressions[6][4] = {
            { 0, 4, 5, 3 }, { 0, 3, 4, 0 }, { 5, 3, 0, 4 },
            { 1, 4, 0, 0 }, { 0, 5, 3, 4 }, { 0, 2, 3, 4 }
        };
        static const int majorScale[7] = { 0, 2, 4, 5, 7, 9, 11 };
        static const int minorScale[7] = { 0, 2, 3, 5, 7, 8, 10 };
        static const int minorProgressions[6][4] = {
            { 0, 5, 2, 6 }, { 0, 3, 6, 0 }, { 5, 3, 0, 6 },
            { 1, 4, 0, 0 }, { 0, 5, 3, 6 }, { 0, 2, 3, 6 }
        };
        const bool minor = mode == 1;
        const int* scale = minor ? minorScale : majorScale;
        const int which = juce::jlimit(0, 5, pattern);
        const int root = juce::jlimit(0, 11, key);
        auto& midi = owner.getMidiEngine();
        const auto selected = midi.getSelectedNotesCopy();
        const auto firstTick = selected.empty() ? 0 : selected.front().startTick;
        const auto barTicks = MidiEngine::ticksPerQuarterNote * 4;
        const auto chordTicks = duration == 2 ? barTicks * 2
            : duration == 3 ? barTicks / 2 : barTicks;
        const int basePitch = 60 + (juce::jlimit(2, 6, octave) - 4) * 12;
        bool changed = false;
        std::array<int, 3> previous {};
        for (int bar = 0; bar < 4; ++bar)
        {
            const int degree = minor ? minorProgressions[which][bar] : progressions[which][bar];
            std::array<int, 3> original {};
            for (int third = 0; third < 3; ++third)
            {
                const int scaleStep = degree + third * 2;
                original[(size_t)third] = basePitch + root
                    + scale[scaleStep % 7] + (scaleStep / 7) * 12;
            }
            std::array<int, 3> pitches = original;
            if (smooth && bar > 0)
            {
                int bestMovement = 100000;
                for (int inversion = 0; inversion < 3; ++inversion)
                {
                    auto candidate = original;
                    for (int turn = 0; turn < inversion; ++turn)
                    {
                        const int moved = candidate[0] + 12;
                        candidate[0] = candidate[1];
                        candidate[1] = candidate[2];
                        candidate[2] = moved;
                    }
                    for (int shift = -2; shift <= 1; ++shift)
                    {
                        auto voiced = candidate;
                        int movement = 0;
                        for (int n = 0; n < 3; ++n)
                        {
                            voiced[(size_t)n] += shift * 12;
                            movement += std::abs(voiced[(size_t)n] - previous[(size_t)n]);
                        }
                        if (movement < bestMovement)
                        {
                            bestMovement = movement;
                            pitches = voiced;
                        }
                    }
                }
            }
            for (int n = 0; n < 3; ++n)
                changed = midi.addNote(firstTick + bar * chordTicks, chordTicks,
                    pitches[(size_t)n], 100, 1) || changed;
            previous = pitches;
        }
        if (changed)
        {
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint();
            owner.repaint();
        }
    }

    void showCompositionWindow()
    {
        // Dedicated dialog is additive: the existing piano roll toolbar and
        // all note-editing gestures remain unchanged.
        struct CompositionContent final : juce::Component
        {
            juce::ComboBox root, quality, inversion, octave, noteLength;
            juce::TextButton create { juce::String::fromUTF8("INSÉRER L’ACCORD") };
            juce::Label heading, description;
            std::function<void(int, int, int)> insert;
            explicit CompositionContent(std::function<void(int, int, int)> callback) : insert(std::move(callback))
            {
                static const char* roots[] = { "Do", "Do#", "Ré", "Mi♭", "Mi", "Fa",
                    "Fa#", "Sol", "La♭", "La", "Si♭", "Si" };
                for (int i = 0; i < 12; ++i)
                    root.addItem(juce::String::fromUTF8(roots[i]), i + 1);
                root.setSelectedId(1, juce::dontSendNotification);
                quality.addItem(juce::String::fromUTF8("Majeur"), 1);
                quality.addItem(juce::String::fromUTF8("Mineur"), 2);
                quality.addItem(juce::String::fromUTF8("Septième dominante"), 3);
                quality.addItem(juce::String::fromUTF8("Sus2"), 4);
                quality.addItem(juce::String::fromUTF8("Sus4"), 5);
                quality.addItem(juce::String::fromUTF8("Diminué"), 6);
                quality.addItem(juce::String::fromUTF8("Augmenté"), 7);
                quality.addItem(juce::String::fromUTF8("Majeur 7"), 8);
                quality.addItem(juce::String::fromUTF8("Mineur 7"), 9);
                quality.addItem(juce::String::fromUTF8("Mineur 7♭5"), 10);
                quality.addItem(juce::String::fromUTF8("Add9"), 11);
                quality.addItem(juce::String::fromUTF8("Neuvième dominante"), 12);
                quality.setSelectedId(1, juce::dontSendNotification);
                inversion.addItem(juce::String::fromUTF8("Position fondamentale"), 1);
                inversion.addItem(juce::String::fromUTF8("Premier renversement"), 2);
                inversion.addItem(juce::String::fromUTF8("Deuxième renversement"), 3);
                inversion.addItem(juce::String::fromUTF8("Troisième renversement"), 4);
                inversion.addItem(juce::String::fromUTF8("Quatrième renversement"), 5);
                inversion.setSelectedId(1, juce::dontSendNotification);
                auto refreshInversions = [this]
                {
                    const int type = quality.getSelectedId() - 1;
                    const int notes = type == 11 ? 5
                        : (type == 2 || type == 7 || type == 8 || type == 9 || type == 10) ? 4 : 3;
                    const int previous = inversion.getSelectedId();
                    for (int id = 1; id <= 5; ++id)
                        inversion.setItemEnabled(id, id <= notes);
                    if (previous > notes)
                        inversion.setSelectedId(1, juce::dontSendNotification);
                };
                quality.onChange = [this, refreshInversions]
                {
                    refreshInversions();
                    repaint();
                };
                refreshInversions();
                for (int oct = 2; oct <= 6; ++oct)
                    octave.addItem("Octave " + juce::String(oct), oct);
                octave.setSelectedId(4, juce::dontSendNotification);
                noteLength.addItem(juce::String::fromUTF8("1/4 · Noire"), 1);
                noteLength.addItem(juce::String::fromUTF8("1/2 · Blanche"), 2);
                noteLength.addItem(juce::String::fromUTF8("1/1 · Ronde"), 3);
                noteLength.addItem(juce::String::fromUTF8("1/8 · Croche"), 4);
                noteLength.setSelectedId(1, juce::dontSendNotification);
                for (auto* combo : { &root, &quality, &inversion, &octave, &noteLength })
                {
                    combo->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff263544));
                    combo->setColour(juce::ComboBox::textColourId, juce::Colour(0xfff3d48b));
                    addAndMakeVisible(*combo);
                }
                heading.setText(juce::String::fromUTF8("COMPOSITION  /  ACCORDS"),
                    juce::dontSendNotification);
                heading.setColour(juce::Label::textColourId, juce::Colour(0xfff3d48b));
                heading.setFont(juce::Font(19.0f, juce::Font::bold));
                description.setText(juce::String::fromUTF8("Choisissez un accord et son renversement."),
                    juce::dontSendNotification);
                description.setColour(juce::Label::textColourId, juce::Colour(0xffb8c8d3));
                addAndMakeVisible(heading);
                addAndMakeVisible(description);
                create.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff86652d));
                create.onClick = [this]
                {
                    if (insert)
                        insert(((root.getSelectedId() - 1) * 12
                            + quality.getSelectedId() - 1) * 5 + inversion.getSelectedId() - 1,
                            octave.getSelectedId(), noteLength.getSelectedId());
                };
                addAndMakeVisible(create);
                for (auto* combo : { &root, &quality, &inversion, &octave, &noteLength })
                    if (combo != &quality) combo->onChange = [this] { repaint(); };
                setSize(430, 415);
            }
            void paint(juce::Graphics& g) override
            {
                g.fillAll(juce::Colour(0xff111923));
                g.setColour(juce::Colour(0xff344857));
                g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(8.0f), 9.0f, 1.0f);
                // Visual-only preview: no MIDI event, note mutation or audio playback.
                const int rootClass = juce::jlimit(0, 11, root.getSelectedId() - 1);
                const int type = juce::jlimit(0, 11, quality.getSelectedId() - 1);
                const int turns = juce::jlimit(0, 4, inversion.getSelectedId() - 1);
                static const int shapes[12][5] = {
                    { 0, 4, 7, -1, -1 }, { 0, 3, 7, -1, -1 },
                    { 0, 4, 7, 10, -1 }, { 0, 2, 7, -1, -1 },
                    { 0, 5, 7, -1, -1 }, { 0, 3, 6, -1, -1 },
                    { 0, 4, 8, -1, -1 }, { 0, 4, 7, 11, -1 },
                    { 0, 3, 7, 10, -1 }, { 0, 3, 6, 10, -1 },
                    { 0, 4, 7, 14, -1 }, { 0, 4, 7, 10, 14 }
                };
                std::array<int, 5> pitches {};
                int count = 0;
                for (int interval : shapes[type])
                    if (interval >= 0)
                        pitches[(size_t)count++] = 60 + (octave.getSelectedId() - 4) * 12 + rootClass + interval;
                for (int turn = 0; turn < turns; ++turn)
                {
                    const int moved = pitches[0] + 12;
                    for (int i = 1; i < count; ++i)
                        pitches[(size_t)(i - 1)] = pitches[(size_t)i];
                    pitches[(size_t)(count - 1)] = moved;
                }
                g.setColour(juce::Colour(0xffb8c8d3));
                g.setFont(juce::Font(12.0f));
                g.drawText(juce::String::fromUTF8("PRÉVISUALISATION DES NOTES"),
                    26, 190, getWidth() - 52, 20, juce::Justification::centredLeft);
                const int left = 26;
                const int gap = 8;
                const int available = getWidth() - 52 - gap * (count - 1);
                const int cellWidth = available / count;
                for (int i = 0; i < count; ++i)
                {
                    const int x = left + i * (cellWidth + gap);
                    g.setColour(juce::Colour(0xff384331));
                    g.fillRoundedRectangle((float)x, 215.0f, (float)cellWidth, 36.0f, 5.0f);
                    g.setColour(juce::Colour(0xfff3d48b));
                    g.drawFittedText(juce::MidiMessage::getMidiNoteName(pitches[(size_t)i], true, true, 3),
                        x + 5, 219, cellWidth - 10, 28, juce::Justification::centred, 1);
                }
                // Compact two-octave keyboard. Golden keys represent chord tones.
                const int keyboardLeft = 26;
                const int keyboardTop = 270;
                // Follow the actual voicing, not only the chosen root octave.
                // Expand to three octaves when a high inversion needs more room.
                int lowest = pitches[0];
                int highest = pitches[0];
                for (int i = 1; i < count; ++i)
                {
                    lowest = juce::jmin(lowest, pitches[(size_t)i]);
                    highest = juce::jmax(highest, pitches[(size_t)i]);
                }
                const int firstPitch = (lowest / 12) * 12;
                const int whiteCount = highest >= firstPitch + 24 ? 21 : 14;
                const float whiteWidth = (float)(getWidth() - 52) / (float)whiteCount;
                auto isChordPitch = [&](int midiPitch)
                {
                    for (int i = 0; i < count; ++i)
                        if (pitches[(size_t)i] == midiPitch) return true;
                    return false;
                };
                for (int i = 0; i < whiteCount; ++i)
                {
                    static const int whiteOffsets[] = { 0, 2, 4, 5, 7, 9, 11 };
                    const int midiPitch = firstPitch + (i / 7) * 12 + whiteOffsets[i % 7];
                    const float x = (float)keyboardLeft + i * whiteWidth;
                    g.setColour(isChordPitch(midiPitch) ? juce::Colour(0xffe1b96f)
                        : juce::Colour(0xffd5dce0));
                    g.fillRoundedRectangle(x + 1.0f, (float)keyboardTop, whiteWidth - 2.0f, 48.0f, 2.0f);
                }
                for (int i = 0; i < whiteCount; ++i)
                {
                    static const int blackOffsets[] = { 1, 3, -1, 6, 8, 10, -1 };
                    const int offset = blackOffsets[i % 7];
                    if (offset < 0) continue;
                    const int midiPitch = firstPitch + (i / 7) * 12 + offset;
                    const float x = (float)keyboardLeft + (i + 1) * whiteWidth - whiteWidth * 0.32f;
                    g.setColour(isChordPitch(midiPitch) ? juce::Colour(0xffdba952)
                        : juce::Colour(0xff293643));
                    g.fillRoundedRectangle(x, (float)keyboardTop, whiteWidth * 0.64f, 29.0f, 2.0f);
                }
            }
            void resized() override
            {
                heading.setBounds(26, 18, getWidth() - 52, 32);
                description.setBounds(26, 52, getWidth() - 52, 25);
                root.setBounds(26, 94, 110, 32);
                quality.setBounds(144, 94, 125, 32);
                inversion.setBounds(277, 94, 127, 32);
                octave.setBounds(26, 145, 176, 32);
                noteLength.setBounds(210, 145, 194, 32);
                create.setBounds(26, 341, getWidth() - 52, 42);
            }
        };
        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = juce::String::fromUTF8("LIBERTY  ·  COMPOSITION");
        options.dialogBackgroundColour = juce::Colour(0xff111923);
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = false;
        options.resizable = false;
        options.content.setOwned(new CompositionContent(
            [safe = juce::Component::SafePointer<PianoRoll>(this)](int choice, int oct, int length)
            {
                if (safe != nullptr) safe->insertCompositionChord(choice, oct, length);
            }));
        options.launchAsync();
    }

    void insertCompositionChord(int choice, int octave = 4, int noteLength = 1)
    {
        // Additive composition tool: existing MIDI editing behaviour is untouched.
        const int chordChoice = choice / 5;
        const int inversion = choice % 5;
        const int root = juce::jlimit(0, 11, chordChoice / 12);
        const int type = chordChoice % 12;
        static const int intervals[12][5] = {
            { 0, 4, 7, -1, -1 }, { 0, 3, 7, -1, -1 },
            { 0, 4, 7, 10, -1 }, { 0, 2, 7, -1, -1 },
            { 0, 5, 7, -1, -1 }, { 0, 3, 6, -1, -1 },
            { 0, 4, 8, -1, -1 }, { 0, 4, 7, 11, -1 },
            { 0, 3, 7, 10, -1 }, { 0, 3, 6, 10, -1 },
            { 0, 4, 7, 14, -1 }, { 0, 4, 7, 10, 14 }
        };
        auto& midi = owner.getMidiEngine();
        const auto selected = midi.getSelectedNotesCopy();
        const auto startTick = selected.empty() ? 0 : selected.front().startTick;
        const auto quarter = MidiEngine::ticksPerQuarterNote;
        const auto duration = noteLength == 2 ? quarter * 2
            : noteLength == 3 ? quarter * 4
            : noteLength == 4 ? juce::jmax((decltype(quarter))1, quarter / 2) : quarter;
        std::array<int, 5> pitches {};
        int noteCount = 0;
        for (int interval : intervals[type])
            if (interval >= 0)
                pitches[(size_t)noteCount++] = 60 + (juce::jlimit(2, 6, octave) - 4) * 12 + root + interval;
        // Move the lowest note up an octave for each inversion.
        for (int turn = 0; turn < juce::jmin(inversion, noteCount - 1); ++turn)
        {
            const int moved = pitches[0] + 12;
            for (int i = 1; i < noteCount; ++i)
                pitches[(size_t)(i - 1)] = pitches[(size_t)i];
            pitches[(size_t)(noteCount - 1)] = moved;
        }
        bool changed = false;
        for (int i = 0; i < noteCount; ++i)
            changed = midi.addNote(startTick, duration, pitches[(size_t)i], 100, 1) || changed;
        if (changed)
        {
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint();
            owner.repaint();
        }
    }

    // Transform simultaneous selected notes as independent chord voicings.
    // All edits are prepared before one undoable replacement.
    juce::String recognizeSelectedChord() const
    {
        auto notes = owner.getMidiEngine().getSelectedNotesCopy();
        if (notes.size() < 3) return juce::String::fromUTF8("Sélectionnez au moins trois notes d'un accord.");
        std::sort(notes.begin(), notes.end(), [](const auto& a, const auto& b)
        {
            if (a.startTick != b.startTick) return a.startTick < b.startTick;
            if (a.channel != b.channel) return a.channel < b.channel;
            return a.pitch < b.pitch;
        });
        const auto start = notes.front().startTick;
        const auto channel = notes.front().channel;
        std::array<bool, 12> present {};
        int bass = 127;
        int count = 0;
        for (const auto& n : notes)
            if (n.startTick == start && n.channel == channel)
            {
                present[(size_t)(n.pitch % 12)] = true;
                bass = std::min(bass, (int)n.pitch);
                ++count;
            }
        if (count < 3) return juce::String::fromUTF8("Accord incomplet : sélectionnez trois notes simultanées.");
        struct Quality { const char* name; std::array<int, 4> intervals; int count; };
        static const Quality qualities[] = {
            { "maj7", { 0, 4, 7, 11 }, 4 }, { "7", { 0, 4, 7, 10 }, 4 },
            { "m7", { 0, 3, 7, 10 }, 4 }, { "m7b5", { 0, 3, 6, 10 }, 4 },
            { "dim7", { 0, 3, 6, 9 }, 4 }, { "m(maj7)", { 0, 3, 7, 11 }, 4 },
            { "", { 0, 4, 7, 0 }, 3 }, { "m", { 0, 3, 7, 0 }, 3 },
            { "dim", { 0, 3, 6, 0 }, 3 }, { "aug", { 0, 4, 8, 0 }, 3 },
            { "sus2", { 0, 2, 7, 0 }, 3 }, { "sus4", { 0, 5, 7, 0 }, 3 }
        };
        static const char* pitchNames[] = { "Do", "Do#", "Ré", "Ré#", "Mi", "Fa",
            "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
        const int distinct = (int)std::count(present.begin(), present.end(), true);
        for (int root = 0; root < 12; ++root)
            for (const auto& q : qualities)
            {
                if (distinct != q.count) continue;
                bool match = true;
                for (int i = 0; i < q.count; ++i)
                    if (!present[(size_t)((root + q.intervals[(size_t)i]) % 12)])
                        match = false;
                if (match)
                {
                    juce::String label = juce::String::fromUTF8(pitchNames[root]) + q.name;
                    if (bass % 12 != root)
                        label += "/" + juce::String::fromUTF8(pitchNames[bass % 12]);
                    return label;
                }
            }
        return juce::String::fromUTF8("Accord non reconnu (ou enrichi).");
    }

    void transformSelectedChords(int mode)
    {
        auto& midi = owner.getMidiEngine();
        auto selected = midi.getSelectedNotesCopy();
        if (selected.size() < 2) return;
        auto transformed = midi.getNotesCopy();
        std::sort(selected.begin(), selected.end(), [](const auto& a, const auto& b)
        {
            if (a.startTick != b.startTick) return a.startTick < b.startTick;
            if (a.channel != b.channel) return a.channel < b.channel;
            return a.pitch < b.pitch;
        });
        std::vector<MidiEngine::NoteEvent> newSelection;
        bool changed = false;
        for (std::size_t begin = 0; begin < selected.size();)
        {
            std::size_t end = begin + 1;
            while (end < selected.size() && selected[end].startTick == selected[begin].startTick
                && selected[end].channel == selected[begin].channel) ++end;
            const auto count = end - begin;
            if (count >= 2)
            {
                for (std::size_t i = begin; i < end; ++i)
                {
                    auto note = selected[i];
                    int pitch = static_cast<int>(note.pitch);
                    if (mode == 0 && i == begin) pitch += 12; // first inversion
                    else if (mode == 1 && i + 1 == end) pitch -= 12; // reverse inversion
                    else if (mode == 2 && ((i - begin) % 2 == 1)) pitch += 12; // open voicing
                    else if (mode == 3 && ((i - begin) % 2 == 1)) pitch -= 12; // drop voicing
                    else if (mode == 4 && count >= 3 && i == end - 2) pitch -= 12; // drop 2
                    else if (mode == 5 && count >= 4 && i == end - 3) pitch -= 12; // drop 3
                    else if (mode == 6 && i > begin) pitch += 12; // octave spread
                    else if (mode == 7 && i > begin) pitch -= 12; // lower upper voices
                    if (pitch < 0 || pitch > 127) return;
                    const auto it = std::find_if(transformed.begin(), transformed.end(), [&note](const auto& n)
                    {
                        return n.startTick == note.startTick && n.pitch == note.pitch
                            && n.channel == note.channel;
                    });
                    if (it == transformed.end()) return;
                    it->pitch = static_cast<std::uint8_t>(pitch);
                    note.pitch = static_cast<std::uint8_t>(pitch);
                    newSelection.push_back(note);
                    changed = changed || pitch != static_cast<int>(selected[i].pitch);
                }
            }
            else newSelection.push_back(selected[begin]);
            begin = end;
        }
        if (!changed) return;
        std::sort(transformed.begin(), transformed.end(), [](const auto& a, const auto& b)
        {
            if (a.startTick != b.startTick) return a.startTick < b.startTick;
            if (a.channel != b.channel) return a.channel < b.channel;
            return a.pitch < b.pitch;
        });
        for (std::size_t i = 1; i < transformed.size(); ++i)
            if (transformed[i].startTick == transformed[i - 1].startTick
                && transformed[i].channel == transformed[i - 1].channel
                && transformed[i].pitch == transformed[i - 1].pitch) return;
        if (midi.replaceNotes(transformed))
        {
            midi.setSelectedNotes(newSelection);
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint(); owner.repaint();
        }
    }

    // FL Studio-inspired rhythm tools. Build the complete result first, then
    // commit as a single undoable MIDI operation with collision validation.
    void applyRhythmicNoteTool(int mode)
    {
        auto& midi = owner.getMidiEngine();
        auto selected = midi.getSelectedNotesCopy();
        if (selected.empty()) return;
        auto updated = midi.getNotesCopy();
        std::vector<MidiEngine::NoteEvent> outputSelection;
        const auto grid = juce::jmax<std::int64_t>(1, activePianoSnapTicks());
        std::sort(selected.begin(), selected.end(), [](const auto& a, const auto& b)
        {
            if (a.startTick != b.startTick) return a.startTick < b.startTick;
            if (a.channel != b.channel) return a.channel < b.channel;
            return a.pitch < b.pitch;
        });
        bool changed = false;
        if (mode <= 1) // ratchet: split each note into two or four repeats
        {
            const int divisions = mode == 0 ? 2 : 4;
            for (const auto& note : selected)
            {
                if (note.lengthTicks < divisions) return;
                const auto it = std::find_if(updated.begin(), updated.end(), [&note](const auto& n)
                {
                    return n.startTick == note.startTick && n.pitch == note.pitch && n.channel == note.channel;
                });
                if (it == updated.end()) return;
                updated.erase(it);
                for (int part = 0; part < divisions; ++part)
                {
                    auto copy = note;
                    const auto from = (note.lengthTicks / divisions) * part
                        + (note.lengthTicks % divisions) * part / divisions;
                    const auto to = (note.lengthTicks / divisions) * (part + 1)
                        + (note.lengthTicks % divisions) * (part + 1) / divisions;
                    copy.startTick += from;
                    copy.lengthTicks = to - from;
                    updated.push_back(copy);
                    outputSelection.push_back(copy);
                }
                changed = true;
            }
        }
        else if (mode <= 3) // arpeggiate simultaneous chord groups
        {
            for (std::size_t begin = 0; begin < selected.size();)
            {
                std::size_t end = begin + 1;
                while (end < selected.size() && selected[end].startTick == selected[begin].startTick
                    && selected[end].channel == selected[begin].channel) ++end;
                if (end - begin > 1)
                {
                    for (std::size_t i = begin; i < end; ++i)
                    {
                        const auto& note = selected[i];
                        const auto rank = mode == 2 ? i - begin : end - i - 1;
                        if (rank > static_cast<std::size_t>(
                            (std::numeric_limits<std::int64_t>::max() - note.startTick) / grid)) return;
                        const auto offset = static_cast<std::int64_t>(rank) * grid;
                        const auto it = std::find_if(updated.begin(), updated.end(), [&note](const auto& n)
                        {
                            return n.startTick == note.startTick && n.pitch == note.pitch && n.channel == note.channel;
                        });
                        if (it == updated.end()) return;
                        it->startTick += offset;
                        outputSelection.push_back(*it);
                        changed = changed || offset != 0;
                    }
                }
                else outputSelection.push_back(selected[begin]);
                begin = end;
            }
        }
        else // note length transformation
        {
            for (const auto& note : selected)
            {
                const auto it = std::find_if(updated.begin(), updated.end(), [&note](const auto& n)
                {
                    return n.startTick == note.startTick && n.pitch == note.pitch && n.channel == note.channel;
                });
                if (it == updated.end()) return;
                const auto target = mode == 4 ? juce::jmax<std::int64_t>(1, note.lengthTicks / 2)
                    : (note.lengthTicks <= std::numeric_limits<std::int64_t>::max() / 2
                        ? note.lengthTicks * 2 : note.lengthTicks);
                it->lengthTicks = target;
                outputSelection.push_back(*it);
                changed = changed || target != note.lengthTicks;
            }
        }
        if (!changed) return;
        std::sort(updated.begin(), updated.end(), [](const auto& a, const auto& b)
        {
            if (a.startTick != b.startTick) return a.startTick < b.startTick;
            if (a.channel != b.channel) return a.channel < b.channel;
            return a.pitch < b.pitch;
        });
        for (std::size_t i = 1; i < updated.size(); ++i)
            if (updated[i].startTick == updated[i - 1].startTick
                && updated[i].pitch == updated[i - 1].pitch
                && updated[i].channel == updated[i - 1].channel) return;
        if (midi.replaceNotes(updated))
        {
            midi.setSelectedNotes(outputSelection);
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint(); owner.repaint();
        }
    }

    // Expressive MIDI transformations, preserving pitch and start positions.
    // Apply all note changes in one undoable replacement.
    void applyExpressiveNotes(int mode, int amount)
    {
        auto& midi = owner.getMidiEngine();
        const auto selected = midi.getSelectedNotesCopy();
        if (selected.empty()) return;
        auto updated = midi.getNotesCopy();
        std::vector<MidiEngine::NoteEvent> newSelection;
        bool changed = false;
        for (const auto& note : selected)
        {
            const auto it = std::find_if(updated.begin(), updated.end(), [&note](const auto& n)
            {
                return n.startTick == note.startTick && n.pitch == note.pitch && n.channel == note.channel;
            });
            if (it == updated.end()) return;
            auto edited = note;
            if (mode == 0) edited.velocity = static_cast<std::uint8_t>(juce::jlimit(1, 127, amount));
            else if (mode == 1) edited.velocity = static_cast<std::uint8_t>(
                juce::jlimit(1, 127, (int)note.velocity + amount));
            else if (mode == 2) edited.lengthTicks = juce::jmax<std::int64_t>(1,
                (note.lengthTicks / 100) * amount + ((note.lengthTicks % 100) * amount) / 100);
            else if (mode == 3) edited.lengthTicks = juce::jmax<std::int64_t>(1,
                (note.lengthTicks / 100) * amount + ((note.lengthTicks % 100) * amount) / 100);
            changed = changed || edited.velocity != note.velocity || edited.lengthTicks != note.lengthTicks;
            *it = edited;
            newSelection.push_back(edited);
        }
        if (!changed) return;
        if (midi.replaceNotes(updated))
        {
            midi.setSelectedNotes(newSelection);
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint(); owner.repaint();
        }
    }

    void applyExpressiveController(int controller, int value)
    {
        auto& midi = owner.getMidiEngine();
        const auto selected = midi.getSelectedNotesCopy();
        if (selected.empty()) return;
        midi.beginGroupedEdit();
        bool changed = false;
        for (const auto& note : selected)
            changed = midi.setControllerEvent(note.startTick, controller, value, note.channel) || changed;
        midi.endGroupedEdit();
        if (changed)
        {
            synchronizeLibertyPianoRollPlayback(owner);
            repaint(); owner.repaint();
        }
    }

    // Deterministic MIDI generator: evenly distributed Euclidean hits
    // within the selected note's original duration.
    void generateEuclideanRhythm(int steps, int pulses)
    {
        auto& midi = owner.getMidiEngine();
        const auto selected = midi.getSelectedNotesCopy();
        if (selected.empty()) return;
        steps = juce::jlimit(2, 32, steps);
        pulses = juce::jlimit(1, steps, pulses);
        auto updated = midi.getNotesCopy();
        std::vector<MidiEngine::NoteEvent> generated;
        for (const auto& note : selected)
        {
            if (note.lengthTicks < steps) return;
            const auto it = std::find_if(updated.begin(), updated.end(), [&note](const auto& n)
            {
                return n.startTick == note.startTick && n.pitch == note.pitch && n.channel == note.channel;
            });
            if (it == updated.end()) return;
            updated.erase(it);
            for (int step = 0; step < steps; ++step)
            {
                if ((step * pulses) % steps >= pulses) continue;
                const auto from = (note.lengthTicks / steps) * step
                    + (note.lengthTicks % steps) * step / steps;
                const auto to = (note.lengthTicks / steps) * (step + 1)
                    + (note.lengthTicks % steps) * (step + 1) / steps;
                auto copy = note;
                copy.startTick += from;
                copy.lengthTicks = juce::jmax<std::int64_t>(1, to - from);
                updated.push_back(copy);
                generated.push_back(copy);
            }
        }
        std::sort(updated.begin(), updated.end(), [](const auto& a, const auto& b)
        {
            if (a.startTick != b.startTick) return a.startTick < b.startTick;
            if (a.channel != b.channel) return a.channel < b.channel;
            return a.pitch < b.pitch;
        });
        for (std::size_t i = 1; i < updated.size(); ++i)
            if (updated[i].startTick == updated[i - 1].startTick
                && updated[i].channel == updated[i - 1].channel
                && updated[i].pitch == updated[i - 1].pitch) return;
        if (midi.replaceNotes(updated))
        {
            midi.setSelectedNotes(generated);
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint(); owner.repaint();
        }
    }

    void setSelectedNoteProbability(int percentage)
    {
        auto& midi = owner.getMidiEngine();
        const auto selected = midi.getSelectedNotesCopy();
        if (selected.empty()) return;
        auto updated = midi.getNotesCopy();
        std::vector<MidiEngine::NoteEvent> nextSelection;
        bool changed = false;
        for (const auto& note : selected)
        {
            const auto it = std::find_if(updated.begin(), updated.end(), [&note](const auto& n)
            {
                return n.startTick == note.startTick && n.pitch == note.pitch && n.channel == note.channel;
            });
            if (it == updated.end()) return;
            auto edited = *it;
            edited.probability = static_cast<std::uint8_t>(juce::jlimit(0, 100, percentage));
            changed = changed || edited.probability != it->probability;
            *it = edited;
            nextSelection.push_back(edited);
        }
        if (!changed) return;
        if (midi.replaceNotes(updated))
        {
            midi.setSelectedNotes(nextSelection);
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint(); owner.repaint();
        }
    }

    // Generate a repeatable melodic pattern within the selected notes' time span.
    // Existing notes are retained; only free note positions are populated.
    void generateMelodicPattern(int steps, int pitchRange, int melodySeed)
    {
        auto& midi = owner.getMidiEngine();
        const auto selected = midi.getSelectedNotesCopy();
        if (selected.empty()) return;
        steps = juce::jlimit(2, 32, steps);
        pitchRange = juce::jlimit(1, 24, pitchRange);
        std::int64_t first = selected.front().startTick;
        std::int64_t last = first;
        int root = selected.front().pitch;
        int channel = selected.front().channel;
        int velocity = selected.front().velocity;
        for (const auto& note : selected)
        {
            first = juce::jmin(first, note.startTick);
            if (note.lengthTicks > 0 && note.startTick <= std::numeric_limits<std::int64_t>::max() - note.lengthTicks)
                last = juce::jmax(last, note.startTick + note.lengthTicks);
        }
        if (last <= first || last - first < steps) return;
        auto updated = midi.getNotesCopy();
        std::vector<MidiEngine::NoteEvent> created;
        const auto span = last - first;
        for (int i = 0; i < steps; ++i)
        {
            const auto from = (span / steps) * i + ((span % steps) * i) / steps;
            const auto to = (span / steps) * (i + 1) + ((span % steps) * (i + 1)) / steps;
            // Major scale degrees, octave wrapping, constrained by requested range.
            static constexpr int majorScale[] = { 0, 2, 4, 5, 7, 9, 11 };
            const std::uint32_t seed = static_cast<std::uint32_t>(melodySeed);
            std::uint32_t value = seed ^ (static_cast<std::uint32_t>(i + 1) * UINT32_C(0x9e3779b9));
            value ^= value >> 16;
            value *= UINT32_C(0x7feb352d);
            value ^= value >> 15;
            value *= UINT32_C(0x846ca68b);
            value ^= value >> 16;
            const int degree = static_cast<int>(value % 14);
            const int semitone = majorScale[degree % 7] + 12 * (degree / 7);
            const int pitch = juce::jlimit(0, 127, root + (semitone % (pitchRange + 1)));
            const auto tick = first + from;
            const auto collision = std::find_if(updated.begin(), updated.end(), [tick, pitch, channel](const auto& note)
            {
                return note.startTick == tick && note.pitch == pitch && note.channel == channel;
            });
            if (collision != updated.end()) continue;
            auto note = selected.front();
            note.startTick = tick;
            note.lengthTicks = juce::jmax<std::int64_t>(1, (to - from) * 9 / 10);
            note.pitch = static_cast<std::uint8_t>(pitch);
            note.velocity = static_cast<std::uint8_t>(juce::jlimit(1, 127, velocity - (i % 4) * 5));
            note.probability = 100;
            note.muted = false;
            updated.push_back(note);
            created.push_back(note);
        }
        if (created.empty()) return;
        if (midi.replaceNotes(updated))
        {
            midi.setSelectedNotes(created);
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint(); owner.repaint();
        }
    }

    void selectNotesByCriteria(int criterion, int minimum, int maximum, int selectionMode)
    {
        auto& midi = owner.getMidiEngine();
        const auto notes = midi.getNotesCopy();
        const auto controllers = criterion >= 15 ? midi.getControllerEventsCopy() : std::vector<MidiEngine::ControllerEvent>{};
        std::vector<MidiEngine::NoteEvent> matches;
        if (minimum > maximum) std::swap(minimum, maximum);
        const auto quarter = juce::jmax<std::int64_t>(1, MidiEngine::ticksPerQuarterNote);
        const auto sixteenth = juce::jmax<std::int64_t>(1, quarter / 4);
        for (const auto& note : notes)
        {
            bool selected = false;
            switch (criterion)
            {
                case 1: selected = note.pitch >= minimum && note.pitch <= maximum; break;
                case 2: selected = note.velocity >= minimum && note.velocity <= maximum; break;
                case 3:
                {
                    // Scale 0..127 represents up to four quarter notes.
                    const auto length = juce::jmax<std::int64_t>(0, note.lengthTicks);
                    const auto scaled = static_cast<int>(juce::jlimit<std::int64_t>(
                        0, 127, (length / (quarter * 4)) * 127
                            + ((length % (quarter * 4)) * 127) / (quarter * 4)));
                    selected = scaled >= minimum && scaled <= maximum;
                    break;
                }
                case 4:
                {
                    const int gridStep = static_cast<int>((note.startTick / sixteenth) % 128);
                    selected = gridStep >= minimum && gridStep <= maximum;
                    break;
                }
                case 5:
                {
                    static constexpr int scaleMasks[5][12] = {
                        { 1,0,1,0,1,1,0,1,0,1,0,1 },
                        { 1,0,1,1,0,1,0,1,1,0,1,0 },
                        { 1,0,1,0,1,0,0,1,0,1,0,0 },
                        { 1,0,0,1,0,1,0,1,0,0,1,0 },
                        { 1,0,0,1,0,1,1,1,0,0,1,0 }
                    };
                    selected = scaleMasks[juce::jlimit(0, 4, scaleType)]
                        [((static_cast<int>(note.pitch) - scaleRoot) % 12 + 12) % 12] != 0;
                    break;
                }
                case 6:
                    selected = note.startTick % quarter == 0;
                    break;
                case 7:
                    selected = note.startTick % quarter != 0;
                    break;
                case 8:
                    selected = std::any_of(notes.begin(), notes.end(), [&note](const auto& other)
                    {
                        return &other != &note && other.pitch == note.pitch && other.channel == note.channel;
                    });
                    break;
                case 9:
                    selected = note.probability >= minimum && note.probability <= maximum;
                    break;
                case 10:
                    selected = note.muted;
                    break;
                case 11:
                    selected = !note.muted;
                    break;
                case 12:
                    selected = std::any_of(notes.begin(), notes.end(), [&note](const auto& other)
                    {
                        return &other != &note && other.startTick == note.startTick
                            && other.pitch == note.pitch && other.channel == note.channel
                            && other.lengthTicks == note.lengthTicks;
                    });
                    break;
                case 13:
                    selected = note.velocity != 100;
                    break;
                case 14:
                    selected = note.probability != 100;
                    break;
                case 15:
                case 16:
                case 17:
                    selected = std::any_of(controllers.begin(), controllers.end(), [&note, criterion](const auto& event)
                    {
                        if (event.channel != note.channel || event.tick < note.startTick) return false;
                        if (event.tick - note.startTick >= note.lengthTicks) return false;
                        return criterion == 15 ? event.controller >= 0 && event.controller <= 127
                            : criterion == 16 ? event.controller == 128 : event.controller == 129;
                    });
                    break;
                default: return;
            }
            if (selected) matches.push_back(note);
        }
        if (selectionMode == 2 || selectionMode == 3 || selectionMode == 4)
        {
            const auto previous = midi.getSelectedNotesCopy();
            const auto sameNote = [](const auto& lhs, const auto& rhs)
            {
                return lhs.startTick == rhs.startTick && lhs.lengthTicks == rhs.lengthTicks
                    && lhs.pitch == rhs.pitch && lhs.channel == rhs.channel;
            };
            if (selectionMode == 2)
            {
                for (const auto& note : previous)
                    if (std::none_of(matches.begin(), matches.end(), [&](const auto& found) { return sameNote(note, found); }))
                        matches.push_back(note);
            }
            else
            {
                std::vector<MidiEngine::NoteEvent> remaining;
                remaining.reserve(previous.size());
                for (const auto& note : previous)
                    if ((selectionMode == 3 && std::none_of(matches.begin(), matches.end(), [&](const auto& found) { return sameNote(note, found); }))
                        || (selectionMode == 4 && std::any_of(matches.begin(), matches.end(), [&](const auto& found) { return sameNote(note, found); })))
                        remaining.push_back(note);
                matches.swap(remaining);
            }
        }
        midi.setSelectedNotes(matches);
        repaint(); owner.repaint();
    }

    void showEditWindow()
    {
        humanizePreviewSnapshot = owner.getMidiEngine().getSelectedNotesCopy();
        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = juce::String::fromUTF8("LIBERTY  ·  ÉDITION MIDI");
        options.dialogBackgroundColour = juce::Colour(0xff111923);
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = false;
        options.resizable = false;
        options.content.setOwned(new EditWindowContent(
            [safe = juce::Component::SafePointer<PianoRoll>(this)](int action, int start, int end, int curve) -> juce::String
            {
                if (safe == nullptr) return {};
                // Any other edit may advance MIDI undo history: never undo that edit
                // through the dedicated humanization button.
                if (action != 24 && action != 25 && action != 1)
                    safe->invalidateHumanizeUndo();
                switch (action)
                {
                    case 1: safe->selectAllNotes(); break;
                    case 2: safe->duplicateSelection(); break;
                    case 3: safe->makeSelectionLegato(); break;
                    case 4: safe->quantizeSelection(); break;
                    case 5: safe->setSelectedNoteLengthToSnap(); break;
                    case 6: safe->transposeSelectedNotes(12); break;
                    case 7: safe->transposeSelectedNotes(-12); break;
                    case 14: safe->transposeSelectedNotes(1); break;
                    case 15: safe->transposeSelectedNotes(-1); break;
                    case 16: safe->scaleSelectedNoteLengths(2.0); break;
                    case 17: safe->scaleSelectedNoteLengths(0.5); break;
                    case 18: safe->quantizeSelectionWithStrength(0.5); break;
                    case 19: safe->quantizeSelectionWithStrength(0.75); break;
                    case 20: safe->applySwingToSelection(0.55); break;
                    case 21: safe->applySwingToSelection(0.65); break;
                    case 22: safe->humanizeSelectedVelocity(); break;
                    case 23: safe->humanizeSelectedTiming(); break;
                    case 26: safe->applyAdvancedQuantize(1.0, 0.5, 0.25); break;
                    case 27: safe->applyAdvancedQuantize(1.0, 0.5, 0.50); break;
                    case 24: return safe->humanizeSelection(start, end, curve);
                    case 25: return safe->undoLastHumanization();
                    case 9: safe->adjustSelectedVelocity(100, true); break;
                    case 10: safe->adjustSelectedVelocity(10, false); break;
                    case 11: safe->adjustSelectedVelocity(-10, false); break;
                    case 12: safe->applyVelocityRamp(true, start, end, curve); break;
                    case 13: safe->applyVelocityRamp(false, start, end, curve); break;
                    case 8:
                        if (safe->owner.getMidiEngine().deleteSelectedNotes())
                        {
                            safe->owner.updateMidiClipTiming();
                            synchronizeLibertyPianoRollPlayback(safe->owner);
                            safe->repaint(); safe->owner.repaint();
                        }
                        break;
                    default: break;
                }
                return {};
            }, owner.getMidiEngine().hasSelectedNote(), owner.getMidiEngine().getSelectedNotesCopy(),
            [safe = juce::Component::SafePointer<PianoRoll>(this)](double strength, double swing, double groove)
            {
                if (safe != nullptr)
                {
                    safe->invalidateHumanizeUndo();
                    safe->applyAdvancedQuantize(strength, swing, groove);
                }
            },
            [safe = juce::Component::SafePointer<PianoRoll>(this)](int mode)
            {
                if (safe != nullptr)
                {
                    safe->invalidateHumanizeUndo();
                    safe->transformSelectedChords(mode);
                }
            },
            [safe = juce::Component::SafePointer<PianoRoll>(this)]() -> juce::String
            {
                return safe != nullptr ? safe->recognizeSelectedChord() : juce::String();
            },
            [safe = juce::Component::SafePointer<PianoRoll>(this)](int mode)
            {
                if (safe != nullptr)
                {
                    safe->invalidateHumanizeUndo();
                    safe->applyRhythmicNoteTool(mode);
                }
            },
            [safe = juce::Component::SafePointer<PianoRoll>(this)](int mode, int amount)
            {
                if (safe == nullptr) return;
                safe->invalidateHumanizeUndo();
                if (mode < 4)
                    safe->applyExpressiveNotes(mode, mode == 2 ? 50 : mode == 3 ? 95 : amount);
                else
                {
                    const int controller = mode == 4 ? 1 : mode == 5 ? 11 : mode == 6 ? 64 : 129;
                    safe->applyExpressiveController(controller, amount);
                }
            },
            [safe = juce::Component::SafePointer<PianoRoll>(this)](int steps, int pulses)
            {
                if (safe != nullptr)
                {
                    safe->invalidateHumanizeUndo();
                    safe->generateEuclideanRhythm(steps, pulses);
                }
            },
            [safe = juce::Component::SafePointer<PianoRoll>(this)](int percentage)
            {
                if (safe != nullptr)
                {
                    safe->invalidateHumanizeUndo();
                    safe->setSelectedNoteProbability(percentage);
                }
            },
            [safe = juce::Component::SafePointer<PianoRoll>(this)](int steps, int range, int seed)
            {
                if (safe != nullptr)
                {
                    safe->invalidateHumanizeUndo();
                    safe->generateMelodicPattern(steps, range, seed);
                }
            },
            [safe = juce::Component::SafePointer<PianoRoll>(this)](int criterion, int minimum, int maximum, int mode)
            {
                if (safe != nullptr) safe->selectNotesByCriteria(criterion, minimum, maximum, mode);
            }));
        options.launchAsync();
    }

    void showDurationPalette()
    {
        if (durationPalette != nullptr)
        {
            durationPalette->setVisible(false);
            durationPalette.reset();
        }
        auto* host = getTopLevelComponent();
        if (host == nullptr) return;
        durationPalette = std::make_unique<DurationPalette>(selectedDurationId,
            [safe = juce::Component::SafePointer<PianoRoll>(this)](int id)
            {
                if (safe == nullptr) return;
                safe->selectedDurationId = id;
                safe->durationValue.setButtonText(durationLabelForId(id));
                safe->applyDurationValue();
                // Avoid destroying a clicked button from inside its callback.
                juce::MessageManager::callAsync([safe]
                {
                    if (safe != nullptr)
                    {
                        if (safe->durationPalette != nullptr)
                        {
                            safe->durationPalette->setVisible(false);
                            safe->durationPalette.reset();
                        }
                    }
                });
            });
        durationPalette->setName("LibertyDurationPaletteOverlay");
        // A separate temporary desktop peer is not a child of the dock.
        // MainComponent's 30 Hz layout and other controllers cannot reorder
        // this palette relative to the piano roll.
        const auto anchor = durationValue.getScreenBounds();
        const auto screen = juce::Desktop::getInstance().getDisplays()
                                .getDisplayForRect(anchor)->userArea;
        const int x = juce::jlimit(screen.getX(),
                                   juce::jmax(screen.getX(), screen.getRight() - 462),
                                   anchor.getX());
        const int y = juce::jlimit(screen.getY(),
                                   juce::jmax(screen.getY(), screen.getBottom() - 360),
                                   anchor.getBottom() + 5);
        durationPalette->setBounds(x, y, 462, 360);
        durationPalette->addToDesktop(juce::ComponentPeer::windowIsTemporary
                                      | juce::ComponentPeer::windowHasDropShadow);
        durationPalette->setVisible(true);
        durationPalette->toFront(false);
    }

    static juce::String durationLabelForId(int id)
    {
        static const char* labels[] = {
            "Personnalisée", "Ronde", "Blanche", "Noire", "Croche", "Double-croche",
            "Triple-croche", "Ronde pointée", "Blanche pointée", "Noire pointée",
            "Croche pointée", "Double-croche pointée", "Triple-croche pointée",
            "Blanche triolet", "Noire triolet", "Croche triolet",
            "Double-croche triolet", "Triple-croche triolet"
        };
        return juce::String::fromUTF8(labels[juce::jlimit(0, 17, id)]);
    }

    static std::int64_t musicalDurationTicks(int id)
    {
        constexpr std::int64_t q = MidiEngine::ticksPerQuarterNote;
        static constexpr std::int64_t durations[] = {
            4*q, 2*q, q, q/2, q/4, q/8,
            6*q, 3*q, 3*q/2, 3*q/4, 3*q/8, 3*q/16,
            4*q/3, 2*q/3, q/3, q/6, q/12
        };
        return id >= 1 && id <= 17 ? durations[id - 1] : 0;
    }

    std::int64_t drawingDurationTicks() const
    {
        const auto chosen = musicalDurationTicks(selectedDurationId);
        return chosen > 0 ? chosen : MidiEngine::ticksPerQuarterNote;
    }

    void applyDurationValue()
    {
        auto& midi = owner.getMidiEngine();
        const auto selected = midi.getSelectedNotesCopy();
        const auto duration = musicalDurationTicks(selectedDurationId);
        if (selected.empty() || duration <= 0) return;
        auto notes = midi.getNotesCopy();
        for (auto& note : notes)
            if (midi.isNoteSelected(note)) note.lengthTicks = duration;
        if (midi.replaceNotes(notes))
        {
            auto updated = selected;
            for (auto& note : updated) note.lengthTicks = duration;
            midi.setSelectedNotes(updated);
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint(); owner.repaint();
        }
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

    void selectExtremePitches(bool highest)
    {
        auto& midi = owner.getMidiEngine();
        const auto notes = midi.getNotesCopy();
        if (notes.empty()) return;
        int extreme = (int)notes.front().pitch;
        for (const auto& note : notes)
            extreme = highest ? juce::jmax(extreme, (int)note.pitch)
                              : juce::jmin(extreme, (int)note.pitch);
        std::vector<MidiEngine::NoteEvent> matching;
        for (const auto& note : notes)
            if ((int)note.pitch == extreme)
                matching.push_back(note);
        midi.setSelectedNotes(matching);
        repaint();
    }

    void selectMutedNotes()
    {
        auto& midi = owner.getMidiEngine();
        std::vector<MidiEngine::NoteEvent> matching;
        for (const auto& note : midi.getNotesCopy())
            if (note.muted) matching.push_back(note);
        midi.setSelectedNotes(matching);
        repaint();
    }

    void selectEveryNthNote(int interval)
    {
        if (interval < 1) return;
        auto& midi = owner.getMidiEngine();
        auto notes = midi.getNotesCopy();
        std::stable_sort(notes.begin(), notes.end(), [](const auto& a, const auto& b)
        {
            if (a.startTick != b.startTick) return a.startTick < b.startTick;
            return a.pitch < b.pitch;
        });
        std::vector<MidiEngine::NoteEvent> matching;
        for (std::size_t i = 0; i < notes.size(); ++i)
            if (i % (std::size_t)interval == 0)
                matching.push_back(notes[i]);
        midi.setSelectedNotes(matching);
        repaint();
    }

    // Two-handle pitch range: the text fields remain editable and authoritative.
    class MidiPitchRangeSlider final : public juce::Component, private juce::Timer
    {
    public:
        MidiPitchRangeSlider()
        {
            setSize(420, 66);
            range.setSliderStyle(juce::Slider::TwoValueHorizontal);
            range.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            range.setRange(0.0, 127.0, 1.0);
            range.setMinAndMaxValues(0.0, 127.0, juce::dontSendNotification);
            range.setColour(juce::Slider::trackColourId, juce::Colour(0xff3575d9));
            range.setColour(juce::Slider::thumbColourId, juce::Colour(0xffedf4fc));
            range.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff344858));
            range.onValueChange = [this]
            {
                if (minimum != nullptr && maximum != nullptr)
                {
                    minimum->setText(juce::String((int)range.getMinValue()), juce::dontSendNotification);
                    maximum->setText(juce::String((int)range.getMaxValue()), juce::dontSendNotification);
                }
            };
            addAndMakeVisible(range);
        }

        void bind(juce::TextEditor* minEditor, juce::TextEditor* maxEditor)
        {
            minimum = minEditor;
            maximum = maxEditor;
            startTimerHz(15);
        }

        void resized() override { range.setBounds(12, 27, getWidth() - 24, 32); }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xff111923));
            g.setColour(juce::Colour(0xffc7d7e5));
            g.setFont(juce::FontOptions(12.0f));
            g.drawText(juce::String::fromUTF8("PLAGE DE HAUTEURS · MIDI 0–127"),
                       12, 4, getWidth() - 24, 22, juce::Justification::centredLeft);
        }

    private:
        void timerCallback() override
        {
            if (minimum == nullptr || maximum == nullptr) return;
            const int low = juce::jlimit(0, 127, minimum->getText().getIntValue());
            const int high = juce::jlimit(0, 127, maximum->getText().getIntValue());
            if (low > high) return;
            if ((int)range.getMinValue() != low || (int)range.getMaxValue() != high)
                range.setMinAndMaxValues((double)low, (double)high, juce::dontSendNotification);
        }

        juce::Slider range;
        juce::TextEditor* minimum = nullptr;
        juce::TextEditor* maximum = nullptr;
    };

    void showMidiSelectionFilter()
    {
        auto dialog = std::make_unique<juce::AlertWindow>(
            juce::String::fromUTF8("LIBERTY · FILTRES DE SÉLECTION MIDI"),
            juce::String::fromUTF8("Sélectionner les notes correspondant aux plages définies. Aucune note ne sera modifiée."),
            juce::MessageBoxIconType::NoIcon);
        dialog->setColour(juce::AlertWindow::backgroundColourId, juce::Colour(0xff111923));
        dialog->setColour(juce::AlertWindow::textColourId, juce::Colour(0xffe7edf5));
        const struct { const char* key; const char* label; int value; } fields[] = {
            { "pitchMin", "Hauteur minimale (0–127)", 0 },
            { "pitchMax", "Hauteur maximale (0–127)", 127 },
            { "velocityMin", "Vélocité minimale (1–127)", 1 },
            { "velocityMax", "Vélocité maximale (1–127)", 127 },
            { "durationMin", "Durée minimale (ticks)", 0 },
            { "durationMax", "Durée maximale (ticks)", 999999 }
        };
        for (const auto& field : fields)
        {
            dialog->addTextEditor(field.key, juce::String(field.value),
                                  juce::String::fromUTF8(field.label));
            if (auto* editor = dialog->getTextEditor(field.key))
            {
                editor->setInputRestrictions(9, "0123456789");
                editor->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff192634));
                editor->setColour(juce::TextEditor::textColourId, juce::Colour(0xffedf4fc));
                editor->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xff3575d9));
            }
        }
        // The range component owns no editor pointers or timer: its lifetime is
        // entirely managed by the AlertWindow custom-component collection.
        class SafePitchRangeSlider final : public juce::Component
        {
        public:
            SafePitchRangeSlider(juce::TextEditor* low, juce::TextEditor* high)
                : minimum(low), maximum(high)
            {
                setSize(420, 66);
                range.setSliderStyle(juce::Slider::TwoValueHorizontal);
                range.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
                range.setRange(0.0, 127.0, 1.0);
                range.setMinAndMaxValues(0.0, 127.0, juce::dontSendNotification);
                range.setColour(juce::Slider::trackColourId, juce::Colour(0xff3575d9));
                range.setColour(juce::Slider::thumbColourId, juce::Colour(0xffedf4fc));
                range.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff344858));
                range.onValueChange = [this]
                {
                    if (minimum != nullptr && maximum != nullptr)
                    {
                        minimum->setText(juce::String((int) range.getMinValue()), juce::dontSendNotification);
                        maximum->setText(juce::String((int) range.getMaxValue()), juce::dontSendNotification);
                    }
                };
                addAndMakeVisible(range);
            }
            void resized() override { range.setBounds(12, 27, getWidth() - 24, 32); }
            void paint(juce::Graphics& g) override
            {
                g.fillAll(juce::Colour(0xff111923));
                g.setColour(juce::Colour(0xffc7d7e5));
                g.setFont(juce::FontOptions(12.0f));
                g.drawText(juce::String::fromUTF8("PLAGE DE HAUTEURS · MIDI 0–127"),
                           12, 4, getWidth() - 24, 22, juce::Justification::centredLeft);
            }
        private:
            juce::Slider range;
            juce::TextEditor* minimum;
            juce::TextEditor* maximum;
        };
        dialog->addCustomComponent(new SafePitchRangeSlider(dialog->getTextEditor("pitchMin"),
                                                             dialog->getTextEditor("pitchMax")));
        dialog->addButton(juce::String::fromUTF8("SÉLECTIONNER"), 1,
                          juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton(juce::String::fromUTF8("ANNULER"), 0,
                          juce::KeyPress(juce::KeyPress::escapeKey));
        auto* window = dialog.get();
        dialog->enterModalState(false, juce::ModalCallbackFunction::create(
            [safe = juce::Component::SafePointer<PianoRoll>(this), window](int result)
            {
                std::unique_ptr<juce::AlertWindow> ownedDialog(window);
                if (safe == nullptr || result != 1) return;
                const auto get = [window](const char* key, int lo, int hi)
                {
                    return juce::jlimit(lo, hi, window->getTextEditorContents(key).getIntValue());
                };
                const int pMin = get("pitchMin", 0, 127), pMax = get("pitchMax", 0, 127);
                const int vMin = get("velocityMin", 1, 127), vMax = get("velocityMax", 1, 127);
                const auto dMin = (std::int64_t)get("durationMin", 0, 999999999);
                const auto dMax = (std::int64_t)get("durationMax", 0, 999999999);
                if (pMin > pMax || vMin > vMax || dMin > dMax) return;
                auto& midi = safe->owner.getMidiEngine();
                std::vector<MidiEngine::NoteEvent> matching;
                for (const auto& note : midi.getNotesCopy())
                    if ((int)note.pitch >= pMin && (int)note.pitch <= pMax
                        && (int)note.velocity >= vMin && (int)note.velocity <= vMax
                        && note.lengthTicks >= dMin && note.lengthTicks <= dMax)
                        matching.push_back(note);
                midi.setSelectedNotes(matching);
                safe->repaint();
            }));
        dialog.release();
    }

    void selectAllNotes()
    {
        auto& midi = owner.getMidiEngine();
        midi.setSelectedNotes(midi.getNotesCopy());
        repaint();
    }

    // Invert the dynamics of the selected MIDI notes (1 <-> 127) without
    // changing timing, pitch, duration, probability or mute state.
    void invertSelectedVelocities()
    {
        auto& midi = owner.getMidiEngine();
        const auto selection = midi.getSelectedNotesCopy();
        if (selection.empty()) return;
        auto notes = midi.getNotesCopy();
        std::vector<MidiEngine::NoteEvent> updatedSelection;
        updatedSelection.reserve(selection.size());
        bool changed = false;
        for (const auto& selected : selection)
        {
            const auto it = std::find_if(notes.begin(), notes.end(), [&](const auto& note)
            {
                return note.startTick == selected.startTick
                    && note.lengthTicks == selected.lengthTicks
                    && note.pitch == selected.pitch && note.channel == selected.channel;
            });
            if (it == notes.end()) return; // Reject stale selections atomically.
            const auto velocity = static_cast<std::uint8_t>(128 - juce::jlimit(1, 127, static_cast<int>(it->velocity)));
            changed = changed || it->velocity != velocity;
            it->velocity = velocity;
            updatedSelection.push_back(*it);
        }
        if (!changed || !midi.replaceNotes(notes)) return;
        midi.setSelectedNotes(updatedSelection);
        synchronizeLibertyPianoRollPlayback(owner);
        repaint();
        owner.repaint();
    }

    // Dynamic range processing around MIDI velocity 64. Ratio < 1 compresses,
    // ratio > 1 expands, while all note positions and expressive flags remain intact.
    void scaleSelectedVelocityDynamics(double ratio, int centre = 64, int offset = 0)
    {
        if (!std::isfinite(ratio) || ratio < 0.0 || ratio > 3.0 || centre < 1 || centre > 127 || offset < -63 || offset > 63) return;
        auto& midi = owner.getMidiEngine();
        const auto selection = midi.getSelectedNotesCopy();
        if (selection.empty()) return;
        auto notes = midi.getNotesCopy();
        std::vector<MidiEngine::NoteEvent> updatedSelection;
        updatedSelection.reserve(selection.size());
        bool changed = false;
        for (const auto& selected : selection)
        {
            const auto it = std::find_if(notes.begin(), notes.end(), [&](const auto& note)
            {
                return note.startTick == selected.startTick
                    && note.lengthTicks == selected.lengthTicks
                    && note.pitch == selected.pitch && note.channel == selected.channel;
            });
            if (it == notes.end()) return;
            const auto scaled = juce::jlimit(1, 127,
                juce::roundToInt(static_cast<double>(centre) + (static_cast<double>(it->velocity) - centre) * ratio + offset));
            changed = changed || it->velocity != static_cast<std::uint8_t>(scaled);
            it->velocity = static_cast<std::uint8_t>(scaled);
            updatedSelection.push_back(*it);
        }
        if (!changed || !midi.replaceNotes(notes)) return;
        midi.setSelectedNotes(updatedSelection);
        synchronizeLibertyPianoRollPlayback(owner);
        repaint();
        owner.repaint();
    }

    class MidiDynamicsContent final : public juce::Component
    {
    public:
        MidiDynamicsContent(std::vector<int> values, int initialRatio, int initialCentre,
                           std::function<void(double, int, int)> apply)
            : velocities(std::move(values)), onApply(std::move(apply))
        {
            initialIntensity = initialRatio;
            initialCentreValue = initialCentre;
            setSize(540, 566);
            configure(intensity, 0.0, 300.0, initialRatio, "%");
            configure(centre, 1.0, 127.0, initialCentre, "");
            configure(offset, -63.0, 63.0, 0.0, "");
            offset.onValueChange = [this]
            {
                if (!applyingPreset) syncPresetFromControls();
                repaint();
            };
            addAndMakeVisible(offset);
            intensity.onValueChange = [this]
            {
                if (!applyingPreset) syncPresetFromControls();
                repaint();
            };
            centre.onValueChange = [this]
            {
                if (!applyingPreset) syncPresetFromControls();
                repaint();
            };
            addAndMakeVisible(intensity);
            addAndMakeVisible(centre);
            presets.addItem(juce::String::fromUTF8("Personnalisé"), 1);
            presets.addItem(juce::String::fromUTF8("Douce"), 2);
            presets.addItem(juce::String::fromUTF8("Naturelle"), 3);
            presets.addItem(juce::String::fromUTF8("Expressive"), 4);
            presets.addItem(juce::String::fromUTF8("Accentuée"), 5);
            presets.setSelectedId(1, juce::dontSendNotification);
            syncPresetFromControls();
            presets.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff192634));
            presets.setColour(juce::ComboBox::textColourId, juce::Colour(0xffedf4fc));
            presets.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff34495b));
            presets.onChange = [this]
            {
                const int id = presets.getSelectedId();
                if (id < 2) return;
                const int ratios[] = { 65, 100, 135, 180 };
                applyingPreset = true;
                intensity.setValue(ratios[id - 2], juce::sendNotificationSync);
                centre.setValue(64, juce::sendNotificationSync);
                offset.setValue(0, juce::sendNotificationSync);
                applyingPreset = false;
                syncPresetFromControls();
                repaint();
            };
            addAndMakeVisible(presets);
            applyButton.setButtonText(juce::String::fromUTF8("Appliquer"));
            cancelButton.setButtonText(juce::String::fromUTF8("Annuler"));
            applyButton.onClick = [this]
            {
                if (onApply)
                    onApply(intensity.getValue() / 100.0,
                            juce::roundToInt(centre.getValue()),
                            juce::roundToInt(offset.getValue()));
                if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                    window->exitModalState(1);
            };
            cancelButton.onClick = [this]
            {
                if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
                    window->exitModalState(0);
            };
            resetButton.setButtonText(juce::String::fromUTF8("Réinitialiser"));
            resetButton.onClick = [this]
            {
                applyingPreset = true;
                intensity.setValue(initialIntensity, juce::sendNotificationSync);
                centre.setValue(initialCentreValue, juce::sendNotificationSync);
                offset.setValue(0, juce::sendNotificationSync);
                applyingPreset = false;
                syncPresetFromControls();
                repaint();
            };
            addAndMakeVisible(resetButton);
            addAndMakeVisible(applyButton);
            addAndMakeVisible(cancelButton);
        }

        void resized() override
        {
            presets.setBounds(184, 37, 325, 28);
            intensity.setBounds(184, 79, 325, 34);
            centre.setBounds(184, 122, 325, 34);
            offset.setBounds(184, 161, 325, 34);
            resetButton.setBounds(24, 516, 128, 32);
            cancelButton.setBounds(288, 516, 100, 32);
            applyButton.setBounds(400, 516, 110, 32);
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xff111923));
            g.setColour(juce::Colour(0xffe7edf5));
            g.setFont(juce::Font(17.0f, juce::Font::bold));
            g.drawText(juce::String::fromUTF8("DYNAMIQUE MIDI"), 24, 12, 490, 28, juce::Justification::centredLeft);
            g.setFont(juce::Font(13.0f));
            g.drawText(juce::String::fromUTF8("Préréglage"), 24, 37, 155, 28, juce::Justification::centredLeft);
            g.drawText(juce::String::fromUTF8("Intensité"), 24, 79, 155, 32, juce::Justification::centredLeft);
            g.drawText(juce::String::fromUTF8("Vélocité centrale"), 24, 122, 155, 32, juce::Justification::centredLeft);
            g.drawText(juce::String::fromUTF8("Décalage global"), 24, 161, 155, 32, juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff91a9bf));
            g.setFont(juce::Font(12.0f));
            g.drawText(juce::String::fromUTF8("APERÇU  ·  AVANT / APRÈS"), 24, 215, 205, 22, juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xffefaa92));
            g.fillRoundedRectangle(229.0f, 221.0f, 9.0f, 9.0f, 2.0f);
            g.setColour(juce::Colour(0xffa9bdd0));
            g.setFont(juce::Font(10.0f));
            g.drawText(juce::String::fromUTF8("Limite"), 241, 214, 60, 22, juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff587187));
            g.fillRoundedRectangle(305.0f, 221.0f, 10.0f, 10.0f, 2.0f);
            g.setColour(juce::Colour(0xffa9bdd0));
            g.drawText(juce::String::fromUTF8("Avant"), 320, 214, 62, 22, juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff4b9de0));
            g.fillRoundedRectangle(392.0f, 221.0f, 10.0f, 10.0f, 2.0f);
            g.setColour(juce::Colour(0xffa9bdd0));
            g.drawText(juce::String::fromUTF8("Après"), 407, 214, 70, 22, juce::Justification::centredLeft);
            const auto area = juce::Rectangle<float>(24.0f, 242.0f, 492.0f, 145.0f);
            g.setColour(juce::Colour(0xff192634));
            g.fillRoundedRectangle(area, 7.0f);
            g.setColour(juce::Colour(0xff34495b));
            g.drawRoundedRectangle(area, 7.0f, 1.0f);

            const auto previewCount = juce::jmin(velocities.size(), static_cast<std::size_t>(96));
            // Subtle reference lines make MIDI velocity levels easier to compare.
            g.setColour(juce::Colour(0xff304153).withAlpha(0.65f));
            for (const int level : { 32, 64, 96 })
            {
                const float y = area.getBottom() - 9.0f - (static_cast<float>(level) / 127.0f) * 118.0f;
                g.drawHorizontalLine(juce::roundToInt(y), area.getX() + 28.0f, area.getRight() - 8.0f);
                g.setColour(juce::Colour(0xff7890a6));
                g.setFont(juce::Font(10.0f));
                g.drawText(juce::String(level), 29, juce::roundToInt(y) - 9, 22, 18,
                           juce::Justification::centredRight);
                g.setColour(juce::Colour(0xff304153).withAlpha(0.65f));
            }
            if (velocities.empty()) return;
            int originalMin = 127, originalMax = 1, transformedMin = 127, transformedMax = 1;
            double originalSum = 0.0, transformedSum = 0.0;
            int clippedLow = 0, clippedHigh = 0;
            int newlyLimitedLow = 0, newlyLimitedHigh = 0;
            const double transformRatio = intensity.getValue() / 100.0;
            const double transformCentre = centre.getValue();
            for (const int value : velocities)
            {
                const int transformed = juce::jlimit(1, 127,
                    juce::roundToInt(transformCentre + (static_cast<double>(value) - transformCentre) * transformRatio + offset.getValue()));
                originalMin = juce::jmin(originalMin, value);
                originalMax = juce::jmax(originalMax, value);
                transformedMin = juce::jmin(transformedMin, transformed);
                transformedMax = juce::jmax(transformedMax, transformed);
                if (transformed == 1)
                {
                    ++clippedLow;
                    if (value != 1) ++newlyLimitedLow;
                }
                if (transformed == 127)
                {
                    ++clippedHigh;
                    if (value != 127) ++newlyLimitedHigh;
                }
                originalSum += value;
                transformedSum += transformed;
            }
            // Statistics cover every selected note, not just the graph sample.
            g.setColour(juce::Colour(0xff192634));
            g.fillRoundedRectangle(24.0f, 392.0f, 492.0f, 52.0f, 6.0f);
            g.setFont(juce::Font(12.0f));
            g.setColour(juce::Colour(0xffa9bdd0));
            g.drawText(juce::String::fromUTF8("AVANT    Min ") + juce::String(originalMin)
                + "  ·  Max " + juce::String(originalMax)
                + "  ·  Moy " + juce::String(originalSum / velocities.size(), 1),
                24, 395, 492, 20, juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff79b9ee));
            g.drawText(juce::String::fromUTF8("APRÈS    Min ") + juce::String(transformedMin)
                + "  ·  Max " + juce::String(transformedMax)
                + "  ·  Moy " + juce::String(transformedSum / velocities.size(), 1),
                24, 418, 492, 20, juce::Justification::centredLeft);
            const double meanDelta = (transformedSum - originalSum) / static_cast<double>(velocities.size());
            g.setFont(juce::Font(11.0f));
            g.setColour(juce::Colour(0xff91a9bf));
            g.drawText(juce::String::fromUTF8("Notes sélectionnées : ") + juce::String(velocities.size()),
                24, 455, 245, 20, juce::Justification::centredLeft);
            g.setColour(meanDelta > 0.05 ? juce::Colour(0xff7dcfa9)
                        : meanDelta < -0.05 ? juce::Colour(0xffefaa92)
                        : juce::Colour(0xff91a9bf));
            g.drawText(juce::String::fromUTF8("Écart moyen : ")
                + (meanDelta > 0.0 ? "+" : "") + juce::String(meanDelta, 1),
                270, 455, 246, 20, juce::Justification::centredRight);
            g.setFont(juce::Font(10.5f));
            g.setColour(juce::Colour(0xff7890a6));
            g.drawText(juce::String::fromUTF8("Aux limites MIDI : ")
                + juce::String(clippedLow) + " (1)  ·  " + juce::String(clippedHigh) + " (127)",
                24, 475, 492, 17, juce::Justification::centredLeft);
            g.setColour((newlyLimitedLow + newlyLimitedHigh) > 0 ? juce::Colour(0xffefaa92)
                        : juce::Colour(0xff7890a6));
            g.drawText(juce::String::fromUTF8("Nouvelles aux limites : ")
                + juce::String(newlyLimitedLow) + " (1)  ·  " + juce::String(newlyLimitedHigh) + " (127)",
                24, 493, 492, 17, juce::Justification::centredLeft);
            // Keep the full selection represented even when it exceeds 96 notes.
            const auto count = juce::jmin(velocities.size(), static_cast<std::size_t>(96));
            const float width = (area.getWidth() - 42.0f) / static_cast<float>(count);
            const double ratio = intensity.getValue() / 100.0;
            const double pivot = centre.getValue();
            for (int i = 0; i < count; ++i)
            {
                const auto sourceIndex = (static_cast<std::size_t>(i) * velocities.size()) / count;
                const int velocity = velocities[sourceIndex];
                const int processed = juce::jlimit(1, 127,
                    juce::roundToInt(pivot + (static_cast<double>(velocity) - pivot) * ratio + offset.getValue()));
                const float x = area.getX() + 32.0f + static_cast<float>(i) * width;
                const float originalHeight = static_cast<float>(velocity) / 127.0f * 118.0f;
                const float processedHeight = static_cast<float>(processed) / 127.0f * 118.0f;
                g.setColour(juce::Colour(0xff587187));
                g.fillRect(x, area.getBottom() - 9.0f - originalHeight, juce::jmax(1.0f, width * 0.38f), originalHeight);
                const bool newlyAtLimit = (processed == 1 && velocity != 1)
                                           || (processed == 127 && velocity != 127);
                g.setColour(newlyAtLimit ? juce::Colour(0xffefaa92) : juce::Colour(0xff4b9de0));
                g.fillRect(x + width * 0.43f, area.getBottom() - 9.0f - processedHeight,
                           juce::jmax(1.0f, width * 0.38f), processedHeight);
            }
            // Draw the sample count last so high-velocity bars cannot cover the label.
            g.setColour(juce::Colour(0xff192634).withAlpha(0.88f));
            g.fillRoundedRectangle(268.0f, 370.0f, 240.0f, 16.0f, 3.0f);
            g.setColour(juce::Colour(0xffa9bdd0));
            g.setFont(juce::Font(10.0f));
            g.drawText(juce::String(previewCount) + " / " + juce::String(velocities.size())
                           + juce::String::fromUTF8(" notes affichées"),
                       270, 371, 236, 14, juce::Justification::centredRight);
        }

    private:
        void syncPresetFromControls()
        {
            int matchingPreset = 1;
            if (juce::roundToInt(centre.getValue()) == 64 && juce::roundToInt(offset.getValue()) == 0)
            {
                const int ratios[] = { 65, 100, 135, 180 };
                for (int i = 0; i < 4; ++i)
                    if (juce::roundToInt(intensity.getValue()) == ratios[i])
                        matchingPreset = i + 2;
            }
            presets.setSelectedId(matchingPreset, juce::dontSendNotification);
        }
        double initialIntensity = 100.0;
        double initialCentreValue = 64.0;
        static void configure(juce::Slider& slider, double minimum, double maximum,
                              double value, const juce::String& suffix)
        {
            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 25);
            slider.setRange(minimum, maximum, 1.0);
            slider.setValue(value, juce::dontSendNotification);
            slider.setTextValueSuffix(suffix);
            slider.setColour(juce::Slider::trackColourId, juce::Colour(0xff3575d9));
            slider.setColour(juce::Slider::thumbColourId, juce::Colour(0xffedf4fc));
            slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff192634));
            slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffedf4fc));
        }
        std::vector<int> velocities;
        std::function<void(double, int, int)> onApply;
        juce::Slider intensity, centre, offset;
        juce::ComboBox presets;
        bool applyingPreset = false;
        juce::TextButton applyButton, cancelButton, resetButton;
    };

    void showMidiDynamicsWindow(int initialRatio = 100, int initialCentre = 64)
    {
        const auto selection = owner.getMidiEngine().getSelectedNotesCopy();
        if (selection.empty()) return;
        std::vector<int> velocities;
        velocities.reserve(selection.size());
        for (const auto& note : selection) velocities.push_back(static_cast<int>(note.velocity));
        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = juce::String::fromUTF8("LIBERTY  ·  DYNAMIQUE MIDI");
        options.dialogBackgroundColour = juce::Colour(0xff111923);
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = false;
        options.resizable = false;
        options.content.setOwned(new MidiDynamicsContent(std::move(velocities), initialRatio, initialCentre,
            [safe = juce::Component::SafePointer<PianoRoll>(this)](double ratio, int centre, int offset)
            {
                if (safe != nullptr) safe->scaleSelectedVelocityDynamics(ratio, centre, offset);
            }));
        options.launchAsync();
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

    // Atomic, non-destructive advanced quantization: selected notes are moved
    // together while unselected notes and note durations remain untouched.
    // Strength < 1 preserves a proportional amount of the original groove.
    void applyAdvancedQuantize(double strength, double swingRatio, double groovePreservation)
    {
        auto& midi = owner.getMidiEngine();
        const auto grid = activePianoSnapTicks();
        if (grid <= 0 || strength <= 0.0) return;
        const auto selected = midi.getSelectedNotesCopy();
        if (selected.empty()) return;
        const auto originals = midi.getNotesCopy();
        auto transformed = originals;
        std::vector<MidiEngine::NoteEvent> newSelection;
        newSelection.reserve(selected.size());
        bool changed = false;
        const double effectiveStrength = juce::jlimit(0.0, 1.0, strength)
            * (1.0 - juce::jlimit(0.0, 1.0, groovePreservation));
        for (const auto& note : selected)
        {
            const auto it = std::find_if(transformed.begin(), transformed.end(), [&note](const auto& n)
            {
                return n.startTick == note.startTick && n.pitch == note.pitch
                    && n.channel == note.channel;
            });
            if (it == transformed.end()) return;
            const auto step = static_cast<std::int64_t>(std::llround(
                static_cast<double>(note.startTick) / static_cast<double>(grid)));
            const double baseTarget = static_cast<double>(step) * static_cast<double>(grid);
            const double swungTarget = baseTarget + ((step % 2 != 0)
                ? (2.0 * swingRatio - 1.0) * static_cast<double>(grid) : 0.0);
            const auto target = static_cast<std::int64_t>(std::llround(
                static_cast<double>(note.startTick)
                + (swungTarget - static_cast<double>(note.startTick)) * effectiveStrength));
            if (target < 0) return;
            it->startTick = target;
            newSelection.push_back(*it);
            changed = changed || target != note.startTick;
        }
        if (!changed) return;
        std::sort(transformed.begin(), transformed.end(), [](const auto& a, const auto& b)
        {
            if (a.startTick != b.startTick) return a.startTick < b.startTick;
            if (a.channel != b.channel) return a.channel < b.channel;
            return a.pitch < b.pitch;
        });
        for (std::size_t i = 1; i < transformed.size(); ++i)
            if (transformed[i].startTick == transformed[i - 1].startTick
                && transformed[i].pitch == transformed[i - 1].pitch
                && transformed[i].channel == transformed[i - 1].channel) return;
        if (midi.replaceNotes(transformed))
        {
            midi.setSelectedNotes(newSelection);
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint(); owner.repaint();
        }
    }

    void quantizeSelectionWithStrength(double strength)
    {
        applyAdvancedQuantize(strength, 0.5, 0.0);
    }

    void applySwingToSelection(double ratio)
    {
        applyAdvancedQuantize(1.0, ratio, 0.0);
    }

    std::vector<MidiEngine::NoteEvent> humanizePreviewSnapshot;
    std::vector<MidiEngine::NoteEvent> lastHumanizeResult;
    bool humanizeUndoAvailable = false;
    std::uint64_t humanizeHistoryRevision = 0;

    juce::String humanizeSelection(int velocityRange, int timingRange, int seed)
    {
        auto& midi = owner.getMidiEngine();
        const auto selected = midi.getSelectedNotesCopy();
        if (selected.empty()) return juce::String::fromUTF8("Aucune note sélectionnée.");
        if (velocityRange == 0 && timingRange == 0) return juce::String::fromUTF8("Réglez une variation supérieure à zéro.");
        // Reject a stale preview if the selection or its notes changed after opening the editor.
        if (selected.size() != humanizePreviewSnapshot.size()) return juce::String::fromUTF8("Sélection modifiée : fermez et rouvrez ÉDITION MIDI.");
        for (size_t i = 0; i < selected.size(); ++i)
        {
            const auto& a = selected[i];
            const auto& b = humanizePreviewSnapshot[i];
            if (a.startTick != b.startTick || a.lengthTicks != b.lengthTicks
                || a.pitch != b.pitch || a.velocity != b.velocity || a.channel != b.channel
                || a.muted != b.muted) return juce::String::fromUTF8("Notes modifiées : fermez et rouvrez ÉDITION MIDI.");
        }
        auto notes = midi.getNotesCopy();
        bool changed = false;
        int index = 0;
        std::vector<MidiEngine::NoteEvent> transformed;
        for (const auto& note : selected)
        {
            auto edited = note;
            const int velocityVariation = (((index * 13 + seed * 7) % 13) - 6) * velocityRange / 6;
            const int timingVariation = (((index * 37 + seed * 19) % 17) - 8) * timingRange / 8;
            edited.velocity = (std::uint8_t)juce::jlimit(1, 127, (int)note.velocity + velocityVariation);
            edited.startTick = juce::jmax<std::int64_t>(0, note.startTick + timingVariation);
            changed = changed || edited.velocity != note.velocity || edited.startTick != note.startTick;
            transformed.push_back(edited);
            ++index;
        }
        if (!changed) return juce::String::fromUTF8("Aucune variation produite avec ces réglages.");
        for (size_t i = 0; i < selected.size(); ++i)
        {
            const auto& oldNote = selected[i];
            auto it = std::find_if(notes.begin(), notes.end(), [&](const auto& note)
            {
                return note.startTick == oldNote.startTick && note.pitch == oldNote.pitch
                    && note.channel == oldNote.channel;
            });
            if (it != notes.end()) *it = transformed[i];
        }
        if (midi.replaceNotes(notes))
        {
            midi.setSelectedNotes(transformed);
            humanizePreviewSnapshot = transformed;
            lastHumanizeResult = midi.getNotesCopy();
            humanizeUndoAvailable = true;
            humanizeHistoryRevision = midi.getHistoryRevision();
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint(); owner.repaint();
            return juce::String::fromUTF8("Humanisation appliquée. Annulation disponible.");
        }
        return juce::String::fromUTF8("Impossible d’appliquer l’humanisation.");
    }

    void invalidateHumanizeUndo() noexcept
    {
        humanizeUndoAvailable = false;
        lastHumanizeResult.clear();
    }

    juce::String undoLastHumanization()
    {
        auto& midi = owner.getMidiEngine();
        if (!humanizeUndoAvailable) return juce::String::fromUTF8("Aucune humanisation à annuler.");
        if (midi.getHistoryRevision() != humanizeHistoryRevision)
        {
            invalidateHumanizeUndo();
            return juce::String::fromUTF8("Historique MIDI modifié : annulation dédiée indisponible.");
        }
        const auto current = midi.getNotesCopy();
        if (current.size() != lastHumanizeResult.size()) return juce::String::fromUTF8("Notes modifiées : annulation impossible ici.");
        for (size_t i = 0; i < current.size(); ++i)
        {
            const auto& a = current[i];
            const auto& b = lastHumanizeResult[i];
            if (a.startTick != b.startTick || a.lengthTicks != b.lengthTicks
                || a.pitch != b.pitch || a.velocity != b.velocity || a.channel != b.channel
                || a.muted != b.muted) return juce::String::fromUTF8("Notes modifiées : annulation impossible ici.");
        }
        if (midi.undo())
        {
            humanizePreviewSnapshot = midi.getSelectedNotesCopy();
            humanizeUndoAvailable = false;
            lastHumanizeResult.clear();
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint(); owner.repaint();
            return juce::String::fromUTF8("Humanisation annulée.");
        }
        return juce::String::fromUTF8("Annulation indisponible.");
    }

    void humanizeSelectedVelocity()
    {
        auto& midi = owner.getMidiEngine();
        const auto selected = midi.getSelectedNotesCopy();
        bool changed = false;
        for (const auto& note : selected)
        {
            const int variation = juce::Random::getSystemRandom().nextInt(13) - 6;
            const int target = juce::jlimit(1, 127, (int)note.velocity + variation);
            if (target != (int)note.velocity)
                changed = midi.setNoteVelocity(note.startTick, note.pitch, note.channel, target) || changed;
        }
        if (changed)
        {
            synchronizeLibertyPianoRollPlayback(owner);
            repaint(); owner.repaint();
        }
    }

    void humanizeSelectedTiming()
    {
        auto& midi = owner.getMidiEngine();
        const auto selected = midi.getSelectedNotesCopy();
        const auto grid = juce::jmax<std::int64_t>(1, activePianoSnapTicks());
        const int range = (int)juce::jlimit<std::int64_t>(1, 24, grid / 12);
        bool changed = false;
        for (const auto& note : selected)
        {
            const int variation = juce::Random::getSystemRandom().nextInt(range * 2 + 1) - range;
            const auto target = juce::jmax<std::int64_t>(0, note.startTick + variation);
            if (target != note.startTick)
                changed = midi.moveNote(note.startTick, note.pitch, note.channel, target, note.pitch) || changed;
        }
        if (changed)
        {
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint(); owner.repaint();
        }
    }

    void duplicateSelection()
    {
        auto& midi = owner.getMidiEngine();
        const auto selected = midi.getSelectedNotesCopy();
        if (selected.empty()) return;
        std::int64_t first = selected.front().startTick;
        std::int64_t last = first;
        for (const auto& note : selected)
        {
            first = juce::jmin(first, note.startTick);
            last = juce::jmax(last, note.startTick + note.lengthTicks);
        }
        const auto grid = juce::jmax<std::int64_t>(1, activePianoSnapTicks());
        const auto duration = juce::jmax<std::int64_t>(grid, last - first);
        const auto offset = ((duration + grid - 1) / grid) * grid;
        if (midi.duplicateSelectedNotes(offset))
        {
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint(); owner.repaint();
        }
    }

    void transposeSelectedNotes(int semitones)
    {
        auto& midi = owner.getMidiEngine();
        if (!midi.hasSelectedNote() || semitones == 0) return;
        if (midi.moveSelectedNotesBy(0, semitones))
        {
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint(); owner.repaint();
        }
    }

    // First independent Liberty visual element: display-only, no MIDI mutations.
    class StrumVisualPreview final : public juce::Component, private juce::Timer
    {
    public:
        StrumVisualPreview(std::vector<int> notes, bool ascending, int percentage)
            : pitches(std::move(notes)), lowToHigh(ascending), spread(percentage)
        {
            setSize(420, 155);
            spacingSlider.setSliderStyle(juce::Slider::LinearHorizontal);
            spacingSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 24);
            spacingSlider.setRange(1.0, 400.0, 1.0);
            spacingSlider.setValue(percentage, juce::dontSendNotification);
            spacingSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xff3575d9));
            spacingSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xffedf4fc));
            spacingSlider.onValueChange = [this]()
            {
                if (spreadControl != nullptr)
                    spreadControl->setText(juce::String((int)spacingSlider.getValue()),
                                           juce::dontSendNotification);
                timerCallback();
            };
            addAndMakeVisible(spacingSlider);
            std::sort(pitches.begin(), pitches.end());
            if (!lowToHigh) std::reverse(pitches.begin(), pitches.end());
            setInterceptsMouseClicks(true, true);
        }

        void bindControls(juce::ComboBox* direction, juce::TextEditor* spacing)
        {
            directionControl = direction;
            spreadControl = spacing;
            spacingSlider.setValue(spacing->getText().getIntValue(), juce::dontSendNotification);
            startTimerHz(20);
        }

        void timerCallback() override
        {
            const bool nextDirection = directionControl == nullptr
                || directionControl->getSelectedId() != 2;
            const int nextSpread = spreadControl == nullptr ? spread
                : juce::jlimit(1, 400, spreadControl->getText().getIntValue());
            if ((int)spacingSlider.getValue() != nextSpread)
                spacingSlider.setValue(nextSpread, juce::dontSendNotification);
            if (nextDirection == lowToHigh && nextSpread == spread) return;
            lowToHigh = nextDirection;
            spread = nextSpread;
            std::sort(pitches.begin(), pitches.end());
            if (!lowToHigh) std::reverse(pitches.begin(), pitches.end());
            repaint();
        }

        void resized() override
        {
            spacingSlider.setBounds(120, 111, getWidth() - 138, 32);
        }

        void paint(juce::Graphics& g) override
        {
            const auto area = getLocalBounds().toFloat().reduced(1.0f);
            g.setColour(juce::Colour(0xff17232f));
            g.fillRoundedRectangle(area, 9.0f);
            g.setColour(juce::Colour(0xff344858));
            g.drawRoundedRectangle(area, 9.0f, 1.0f);
            g.setColour(juce::Colour(0xffc7d7e5));
            g.setFont(juce::FontOptions(12.0f));
            g.drawText(juce::String::fromUTF8("APERÇU DE L’ÉGRENAGE"),
                       15, 9, getWidth() - 30, 20, juce::Justification::centredLeft);
            g.drawText(juce::String::fromUTF8("ÉCARTEMENT"),
                       15, 115, 106, 24, juce::Justification::centredLeft);
            if (pitches.empty()) return;
            const auto limits = std::minmax_element(pitches.begin(), pitches.end());
            const float range = (float)juce::jmax(1, *limits.second - *limits.first);
            const float step = juce::jlimit(8.0f, 48.0f, 22.0f * (float)spread / 100.0f);
            const float total = step * (float)(pitches.size() - 1);
            const float scale = juce::jmin(1.0f, 350.0f / juce::jmax(1.0f, total));
            for (std::size_t i = 0; i < pitches.size(); ++i)
            {
                const float x = 22.0f + (float)i * step * scale;
                const float y = 85.0f - 38.0f * (float)(pitches[i] - *limits.first) / range;
                g.setColour(juce::Colour(0xff3575d9));
                g.fillRoundedRectangle(x, y, 21.0f, 7.0f, 3.0f);
            }
        }
    private:
        std::vector<int> pitches;
        bool lowToHigh = true;
        int spread = 25;
        juce::ComboBox* directionControl = nullptr;
        juce::TextEditor* spreadControl = nullptr;
        juce::Slider spacingSlider;
    };

    void showStrumWindow(int initialDirection = 1, int initialSpread = 25,
                         int initialVelocity = 0, int initialTiming = 0)
    {
        if (owner.getMidiEngine().getSelectedNotesCopy().size() < 2) return;
        auto dialog = std::make_unique<juce::AlertWindow>(
            juce::String::fromUTF8("LIBERTY  ·  STRUM MIDI"),
            juce::String::fromUTF8("Réglez le sens et l'écartement des notes sélectionnées."),
            juce::MessageBoxIconType::NoIcon);
        dialog->setColour(juce::AlertWindow::backgroundColourId, juce::Colour(0xff111923));
        dialog->setColour(juce::AlertWindow::textColourId, juce::Colour(0xffe7edf5));
        dialog->addComboBox("direction",
            { juce::String::fromUTF8("Grave vers aigu"), juce::String::fromUTF8("Aigu vers grave") },
            juce::String::fromUTF8("Direction"));
        if (auto* direction = dialog->getComboBoxComponent("direction"))
            direction->setSelectedId(initialDirection, juce::dontSendNotification);
        dialog->addTextEditor("spread", juce::String(initialSpread), juce::String::fromUTF8("Écartement (% de grille)"));
        dialog->addTextEditor("velocity", juce::String(initialVelocity), juce::String::fromUTF8("Humanisation vélocité (0–50)"));
        dialog->addTextEditor("timing", juce::String(initialTiming), juce::String::fromUTF8("Humanisation temporelle (0–50 %)"));
        // Reject non-numeric characters at entry time, rather than silently
        // accepting malformed values only when the user presses Apply.
        for (const char* key : { "spread", "velocity", "timing" })
        {
            if (auto* editor = dialog->getTextEditor(key))
            {
                editor->setInputRestrictions(3, "0123456789");
                editor->setSelectAllWhenFocused(true);
                editor->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff192634));
                editor->setColour(juce::TextEditor::textColourId, juce::Colour(0xffedf4fc));
                editor->setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff34495b));
                editor->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xffc59b5e));
            }
        }
        // Preview reads the current selection but never changes the MIDI clip.
        std::vector<int> previewPitches;
        for (const auto& note : owner.getMidiEngine().getSelectedNotesCopy())
            previewPitches.push_back((int)note.pitch);
        auto* preview = new StrumVisualPreview(
            std::move(previewPitches), initialDirection != 2, initialSpread);
        preview->bindControls(dialog->getComboBoxComponent("direction"),
                              dialog->getTextEditor("spread"));
        dialog->addCustomComponent(preview);
        // Quick musical preview of the selected chord without changing the MIDI
        // engine or consuming an Undo step.
        dialog->addButton(juce::String::fromUTF8("ÉCOUTER"), 2);
        dialog->addButton(juce::String::fromUTF8("RÉINITIALISER"), 3);
        dialog->addButton(juce::String::fromUTF8("APPLIQUER"), 1,
                          juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton(juce::String::fromUTF8("ANNULER"), 0,
                          juce::KeyPress(juce::KeyPress::escapeKey));
        auto* window = dialog.get();
        dialog->enterModalState(true,
            juce::ModalCallbackFunction::create(
                [safe = juce::Component::SafePointer<PianoRoll>(this), window](int result)
                {
                    // Keep the dialog alive while reading its controls, then
                    // release it on every exit path (including Cancel).
                    std::unique_ptr<juce::AlertWindow> ownedDialog(window);
                    if (safe == nullptr) return;
                    if (result == 3)
                    {
                        // Reset every parameter without touching the MIDI clip.
                        juce::MessageManager::callAsync([safe]()
                        {
                            if (safe != nullptr)
                                safe->showStrumWindow(1, 25, 0, 0);
                        });
                        return;
                    }
                    if (result != 1 && result != 2) return;
                    const bool lowToHigh = window->getComboBoxComponent("direction") == nullptr
                        || window->getComboBoxComponent("direction")->getSelectedId() != 2;
                    const int percentage = juce::jlimit(1, 400,
                        window->getTextEditorContents("spread").getIntValue());
                    const auto grid = juce::jmax<std::int64_t>(1, activePianoSnapTicks());
                    const auto spread = juce::jmax<std::int64_t>(1, grid * percentage / 100);
                    const int velocityVariation = juce::jlimit(0, 50,
                        window->getTextEditorContents("velocity").getIntValue());
                    const int timingVariation = juce::jlimit(0, 50,
                        window->getTextEditorContents("timing").getIntValue());
                    if (result == 2)
                    {
                        // Reopen with identical parameters after audition; the
                        // stock AlertWindow closes on any button press.
                        const int directionId = lowToHigh ? 1 : 2;
                        juce::MessageManager::callAsync(
                            [safe, directionId, percentage, velocityVariation, timingVariation]()
                            {
                                if (safe != nullptr)
                                    safe->showStrumWindow(directionId, percentage,
                                                          velocityVariation, timingVariation);
                            });
                        const int lane = safe->owner.getSelectedPianoInstrumentLane();
                        if (lane < 0 || lane >= safe->owner.getInstrumentTrackCount()) return;
                        auto notes = safe->owner.getMidiEngine().getSelectedNotesCopy();
                        if (notes.size() < 2) return;
                        std::sort(notes.begin(), notes.end(), [lowToHigh](const auto& a, const auto& b)
                        {
                            if (a.startTick != b.startTick) return a.startTick < b.startTick;
                            return lowToHigh ? a.pitch < b.pitch : a.pitch > b.pitch;
                        });
                        const auto base = notes.front().startTick;
                        for (std::size_t i = 0; i < notes.size() && i < 32; ++i)
                        {
                            const int pitch = (int)notes[i].pitch;
                            const auto relative = notes[i].startTick - base;
                            const auto offset = (std::int64_t)i * spread;
                            const int delay = (int)juce::jlimit<std::int64_t>(0, 3000,
                                (relative + offset) * 500 / MidiEngine::ticksPerQuarterNote);
                            juce::Timer::callAfterDelay(delay,
                                [safe, lane, pitch]()
                                {
                                    if (safe == nullptr) return;
                                    safe->owner.auditionPianoKey(lane, pitch, true);
                                    juce::Timer::callAfterDelay(180,
                                        [safe, lane, pitch]()
                                        {
                                            if (safe != nullptr)
                                                safe->owner.auditionPianoKey(lane, pitch, false);
                                        });
                                });
                        }
                        return;
                    }
                    if (safe->owner.getMidiEngine().strumSelectedNotes(spread, lowToHigh, velocityVariation, timingVariation))
                    {
                        safe->owner.updateMidiClipTiming();
                        synchronizeLibertyPianoRollPlayback(safe->owner);
                        safe->repaint();
                        safe->owner.repaint();
                    }
                }), false);
        dialog.release();
    }

    void strumSelectedChord(bool lowToHigh)
    {
        // A quarter of the current grid division gives a subtle strum.
        const auto spread = juce::jmax<std::int64_t>(1, activePianoSnapTicks() / 4);
        if (owner.getMidiEngine().strumSelectedNotes(spread, lowToHigh))
        {
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint();
            owner.repaint();
        }
    }

    void splitSelectedNotesToGrid()
    {
        if (owner.getMidiEngine().splitSelectedNotesToGrid(
                juce::jmax<std::int64_t>(1, activePianoSnapTicks())))
        {
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint();
            owner.repaint();
        }
    }

    void setSelectedNoteLengthToSnap()
    {
        auto& midi = owner.getMidiEngine();
        const auto selected = midi.getSelectedNotesCopy();
        const auto length = juce::jmax<std::int64_t>(1, activePianoSnapTicks());
        bool changed = false;
        for (const auto& note : selected)
            if (note.lengthTicks != length)
                changed = midi.setNoteLength(note.startTick, note.pitch, note.channel, length) || changed;
        if (changed)
        {
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint(); owner.repaint();
        }
    }

    void scaleSelectedNoteLengths(double factor)
    {
        auto& midi = owner.getMidiEngine();
        const auto selected = midi.getSelectedNotesCopy();
        bool changed = false;
        for (const auto& note : selected)
        {
            const auto scaled = juce::jmax<std::int64_t>(1,
                (std::int64_t)std::llround((double)note.lengthTicks * factor));
            if (scaled != note.lengthTicks)
                changed = midi.setNoteLength(note.startTick, note.pitch, note.channel, scaled) || changed;
        }
        if (changed)
        {
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint(); owner.repaint();
        }
    }

    void makeSelectionLegato()
    {
        auto& midi = owner.getMidiEngine();
        auto selected = midi.getSelectedNotesCopy();
        if (selected.size() < 2) return;
        // Legato is independent for each MIDI channel and pitch. Grouping by
        // pitch alone can interleave channels and miss the next note.
        std::sort(selected.begin(), selected.end(), [](const auto& a, const auto& b)
        {
            if (a.channel != b.channel) return a.channel < b.channel;
            if (a.pitch != b.pitch) return a.pitch < b.pitch;
            return a.startTick < b.startTick;
        });
        bool changed = false;
        for (std::size_t i = 0; i + 1 < selected.size(); ++i)
        {
            const auto& note = selected[i];
            const auto& next = selected[i + 1];
            if (note.pitch != next.pitch || note.channel != next.channel
                || next.startTick <= note.startTick) continue;
            const auto length = next.startTick - note.startTick;
            if (length != note.lengthTicks)
                changed = midi.setNoteLength(note.startTick, note.pitch, note.channel, length) || changed;
        }
        if (changed)
        {
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint(); owner.repaint();
        }
    }

    void applyMidiHistory(bool redoAction)
    {
        auto& midi = owner.getMidiEngine();
        const bool changed = redoAction ? midi.redo() : midi.undo();
        if (changed)
        {
            invalidateHumanizeUndo();
            owner.updateMidiClipTiming();
            synchronizeLibertyPianoRollPlayback(owner);
            repaint();
            owner.repaint();
        }
        undoMidiButton.setEnabled(midi.canUndo());
        redoMidiButton.setEnabled(midi.canRedo());
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        // Share the MIDI engine's history with all Piano Roll editing commands.
        // Command on macOS / Control on Windows and Linux.
        const auto modifiers = key.getModifiers();
        const bool primaryShortcut = modifiers.isCommandDown() || modifiers.isCtrlDown();
        if (primaryShortcut && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'Y'))
        {
            applyMidiHistory(key.getKeyCode() == 'Y' || modifiers.isShiftDown());
            return true;
        }
        // Cmd/Ctrl+Alt+G opens the detailed Strum settings.
        if (primaryShortcut && modifiers.isAltDown() && !modifiers.isShiftDown()
            && key.getKeyCode() == 'G')
        { showStrumWindow(); return true; }
        // Cmd/Ctrl+Shift+G: strum grave vers aigu; Alt inverse le sens.
        if (primaryShortcut && modifiers.isShiftDown() && key.getKeyCode() == 'G')
        { strumSelectedChord(!modifiers.isAltDown()); return true; }
        // Cmd/Ctrl+Alt+V: fenêtre professionnelle de dynamique MIDI.
        if (primaryShortcut && modifiers.isAltDown() && !modifiers.isShiftDown()
            && key.getKeyCode() == 'V')
        { showMidiDynamicsWindow(); return true; }
        // Cmd/Ctrl+Alt+C: compresser les vélocités autour de 64 (50 %).
        if (primaryShortcut && modifiers.isAltDown() && !modifiers.isShiftDown()
            && key.getKeyCode() == 'C')
        { scaleSelectedVelocityDynamics(0.5); return true; }
        // Cmd/Ctrl+Alt+E: étendre les vélocités autour de 64 (150 %).
        if (primaryShortcut && modifiers.isAltDown() && !modifiers.isShiftDown()
            && key.getKeyCode() == 'E')
        { scaleSelectedVelocityDynamics(1.5); return true; }
        // Cmd/Ctrl+Shift+V: inverser les vélocités de la sélection (1 ↔ 127).
        if (primaryShortcut && modifiers.isShiftDown() && !modifiers.isAltDown()
            && key.getKeyCode() == 'V')
        { invertSelectedVelocities(); return true; }
        // Cmd/Ctrl+Shift+K: découper les notes sélectionnées selon la grille.
        if (primaryShortcut && modifiers.isShiftDown() && key.getKeyCode() == 'K')
        { splitSelectedNotesToGrid(); return true; }
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'D')
        { duplicateSelection(); return true; }
        if (key.getModifiers().isAltDown() && key.getKeyCode() == 'L')
        { makeSelectionLegato(); return true; }
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'A')
        { selectAllNotes(); return true; }
        if (key.getKeyCode() == 'Q' && !key.getModifiers().isCommandDown())
        { quantizeSelection(); return true; }
        // Cubase-style keyboard editing: arrows nudge selected notes, Shift moves an octave.
        // Alt/Option + horizontal arrows shifts notes by the current SNAP step.
        const int code = key.getKeyCode();
        const bool vertical = code == juce::KeyPress::upKey || code == juce::KeyPress::downKey;
        const bool horizontal = code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey;
        if ((vertical || (horizontal && key.getModifiers().isAltDown()))
            && !key.getModifiers().isCommandDown() && !key.getModifiers().isCtrlDown())
        {
            auto& midi = owner.getMidiEngine();
            if (!midi.hasSelectedNote()) return false;
            const int pitchDelta = vertical
                ? (code == juce::KeyPress::upKey ? 1 : -1) * (key.getModifiers().isShiftDown() ? 12 : 1) : 0;
            const std::int64_t tickDelta = horizontal
                ? (code == juce::KeyPress::rightKey ? 1 : -1) * juce::jmax<std::int64_t>(1, activePianoSnapTicks()) : 0;
            if (midi.moveSelectedNotesBy(tickDelta, pitchDelta))
            {
                owner.updateMidiClipTiming();
                synchronizeLibertyPianoRollPlayback(owner);
                repaint(); owner.repaint();
            }
            return true;
        }
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
            lowestVisibleKey = juce::jlimit(0, 128 - visiblePitchRows(),
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
        // Highlight the overflow entry only when the active tool is hidden.
        const int active = getLibertyActiveTool();
        const bool activeHidden = active >= 0 && active < 9
            && !toolButtons[(size_t)active].isVisible();
        moreToolsButton.setColour(juce::TextButton::buttonColourId,
            activeHidden ? juce::Colour(0xff315b69) : juce::Colour(0xff263544));
        moreToolsButton.setColour(juce::TextButton::textColourOffId,
            activeHidden ? juce::Colour(0xfff3d48b) : juce::Colour(0xffdcb66f));
        static const char* toolNames[] = { "Sélection", "Crayon", "Ciseaux", "Gomme",
            "Redimensionner", "Étirer", "Vélocité", "Sélection rectangulaire", "Muet" };
        const juce::String activeName = active >= 0 && active < 9
            ? juce::String::fromUTF8(toolNames[active]) : juce::String::fromUTF8("Inconnu");
        moreToolsButton.setTooltip(juce::String::fromUTF8("Outils MIDI — actif : ")
            + activeName + (activeHidden ? juce::String::fromUTF8(" (dans la palette)") : juce::String()));
    }

    void resized() override
    {
        // Keep primary editing commands visible before secondary drawing tools.
        // Every hidden command remains reachable through the overflow palette.
        const int left = pianoKeyWidth + 8;
        const int right = getWidth() - 8;
        const int buttonHeight = editorToolbarHeight - 6;
        const int available = juce::jmax(0, right - left);
        const bool showOverflow = available >= 30;
        const int overflowX = right - 30;
        moreToolsButton.setVisible(showOverflow);
        moreToolsButton.setBounds(overflowX, 3, 30, buttonHeight);
        const int actionRight = showOverflow ? overflowX - 6 : right;
        const int usable = juce::jmax(0, actionRight - left);
        // Allocate one active-tool slot first, then fit complete action groups.
        // A group is either fully visible or entirely available in the ⋯ menu.
        const int step = 35;
        const int reservedToolWidth = usable >= step ? step : 0;
        const int actionsAvailable = juce::jmax(0, usable - reservedToolWidth);
        const bool showHistory = actionsAvailable >= 8 + 36 + 4 + 52 + 8 + 66;
        const bool showEdit = actionsAvailable >= 8 + 36 + 4 + 52;
        const bool showQuantize = actionsAvailable >= 8 + 36;
        const int actionWidth = showHistory ? 8 + 36 + 4 + 52 + 8 + 66
            : showEdit ? 8 + 36 + 4 + 52
            : showQuantize ? 8 + 36 : 0;
        const int toolCount = juce::jlimit(0, 9, (usable - actionWidth) / step);
        // If the selected tool falls outside the visible group, give it the
        // final visible slot so it remains directly accessible in narrow docks.
        const int activeTool = getLibertyActiveTool();
        for (int i = 0; i < 9; ++i)
        {
            auto& button = toolButtons[(size_t)i];
            const bool activeOverflow = activeTool >= toolCount && activeTool < 9;
            const bool visible = i < toolCount - (activeOverflow ? 1 : 0)
                || (activeOverflow && i == activeTool && toolCount > 0);
            const int slot = activeOverflow && i == activeTool ? toolCount - 1 : i;
            button.setVisible(visible);
            button.setBounds(left + slot * step, 3, 32, buttonHeight);
        }
        const int actionX = left + toolCount * step + 8;
        quantizeButton.setVisible(showQuantize);
        quantizeButton.setBounds(actionX, 3, 36, buttonHeight);
        const int editX = actionX + 40;
        selectAllButton.setVisible(showEdit);
        selectAllButton.setBounds(editX, 3, 52, buttonHeight);
        const int historyX = editX + 60;
        undoMidiButton.setVisible(showHistory);
        redoMidiButton.setVisible(showHistory);
        undoMidiButton.setBounds(historyX, 3, 30, buttonHeight);
        redoMidiButton.setBounds(historyX + 36, 3, 30, buttonHeight);
        refreshToolButtons();
        // Keep the editable value inside the visible information line, not at the far-right edge.
        const int velocityX = pianoKeyWidth + 390;
        durationValue.setBounds(pianoKeyWidth + 210, editorToolbarHeight + 3,
                                juce::jmax(0, juce::jmin(135, getWidth() - pianoKeyWidth - 218)),
                                juce::jmax(16, rulerHeight - editorToolbarHeight - 6));
        velocityValue.setBounds(velocityX, editorToolbarHeight + 3,
                                juce::jmax(0, juce::jmin(56, getWidth() - velocityX - 8)),
                                juce::jmax(16, rulerHeight - editorToolbarHeight - 6));
        const int lanesHeight = juce::jmax(0, juce::jmin(getHeight() - rulerHeight, dualLanes ? dualLaneTotalHeight : velocityLaneHeight));
        const int laneTop = getHeight() - lanesHeight;
        if (dualLanes)
        {
            const int firstHeight = juce::jlimit(36, juce::jmax(36, lanesHeight - 36), dualLaneFirstHeight);
            velocityLane.setBounds(0, laneTop, getWidth(), firstHeight);
            controllerLane.setBounds(0, laneTop + firstHeight, getWidth(), lanesHeight - firstHeight);
        }
        else
        {
            velocityLane.setBounds(0, laneTop, getWidth(), lanesHeight);
            controllerLane.setBounds(velocityLane.getBounds());
        }
        const int selectorWidth = juce::jmin(172, juce::jmax(0, getWidth() - pianoKeyWidth - 58));
        controllerSelector.setBounds(juce::jmax(pianoKeyWidth + 8, getWidth() - selectorWidth - 48), 3, selectorWidth, 27);
        dualLaneButton.setBounds(getWidth() - 40, 3, 32, 27);
        topLaneHandle.setBounds(0, laneTop - 5, getWidth(), 10);
        dividerLaneHandle.setBounds(0, laneTop + dualLaneFirstHeight - 5, getWidth(), 10);
        topLaneHandle.setVisible(dualLanes);
        dividerLaneHandle.setVisible(dualLanes);
        if (dualLanes)
        {
            topLaneHandle.toFront(false);
            dividerLaneHandle.toFront(false);
        }
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
            info = juce::String::fromUTF8("MIDI  |  Aucune note sélectionnée");
        else if (selectedNotes.size() > 1)
        {
            info = "MIDI  |  " + juce::String((int) selectedNotes.size())
                + juce::String::fromUTF8(" notes sélectionnées");
            // Read-only chord recognition: ignore octaves and inversions.
            // Never alter MIDI notes or existing editing tools.
            std::array<bool, 12> pitchClasses {};
            int uniqueCount = 0;
            int lowestPitch = 127;
            for (const auto& note : selectedNotes)
            {
                const int pc = ((int)note.pitch % 12 + 12) % 12;
                lowestPitch = juce::jmin(lowestPitch, (int)note.pitch);
                if (!pitchClasses[(size_t)pc])
                {
                    pitchClasses[(size_t)pc] = true;
                    ++uniqueCount;
                }
            }
            struct ChordShape { const char* suffix; std::array<int, 4> intervals; int count; };
            static const ChordShape shapes[] = {
                { "maj7", { 0, 4, 7, 11 }, 4 },
                { "m7",   { 0, 3, 7, 10 }, 4 },
                { "7",    { 0, 4, 7, 10 }, 4 },
                { "m7b5", { 0, 3, 6, 10 }, 4 },
                { "dim7", { 0, 3, 6, 9 }, 4 },
                { "",     { 0, 4, 7, 0 }, 3 },
                { "m",    { 0, 3, 7, 0 }, 3 },
                { "dim",  { 0, 3, 6, 0 }, 3 },
                { "aug",  { 0, 4, 8, 0 }, 3 },
                { "sus2", { 0, 2, 7, 0 }, 3 },
                { "sus4", { 0, 5, 7, 0 }, 3 }
            };
            static const char* rootNames[] = {
                "Do", "Do#", "Ré", "Mi♭", "Mi", "Fa",
                "Fa#", "Sol", "La♭", "La", "Si♭", "Si"
            };
            bool found = false;
            for (int root = 0; root < 12 && !found; ++root)
            {
                for (const auto& shape : shapes)
                {
                    if (shape.count != uniqueCount) continue;
                    bool matches = true;
                    for (int i = 0; i < shape.count; ++i)
                        if (!pitchClasses[(size_t)((root + shape.intervals[(size_t)i]) % 12)])
                            matches = false;
                    if (matches)
                    {
                        info += juce::String::fromUTF8("  |  Accord : ")
                            + juce::String::fromUTF8(rootNames[root])
                            + juce::String::fromUTF8(shape.suffix);
                        const int bassClass = lowestPitch % 12;
                        if (bassClass != root)
                            info += "/" + juce::String::fromUTF8(rootNames[bassClass]);
                        found = true;
                        break;
                    }
                }
            }
        }
        else
        {
            const auto& n = selectedNotes.front();
            const auto beat = MidiEngine::ticksPerQuarterNote;
            const auto measure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
            const auto barNumber = measure > 0 ? n.startTick / measure + 1 : 1;
            const auto beatNumber = measure > 0 ? (n.startTick % measure) / beat + 1 : 1;
            info = "Mes. " + juce::String(barNumber)
                 + "." + juce::String(beatNumber)
                 + "  Note " + juce::MidiMessage::getMidiNoteName((int)n.pitch, true, true, 3);
        }
        // Display detected harmony in its own amber badge, preserving the
        // existing duration, velocity and channel information fields.
        const int chordSeparator = info.indexOf(juce::String::fromUTF8("  |  Accord : "));
        const juce::String chordName = chordSeparator >= 0
            ? info.substring(chordSeparator + juce::String::fromUTF8("  |  Accord : ").length())
            : juce::String();
        const juce::String baseInfo = chordSeparator >= 0 ? info.substring(0, chordSeparator) : info;
        const int infoWidth = juce::jmax(0, juce::jmin(258, getWidth() - pianoKeyWidth - 14));
        g.setColour(juce::Colour(0xffd4e5ed));
        g.drawFittedText(baseInfo, pianoKeyWidth + 9, infoY, infoWidth,
                         rulerHeight - infoY - 1, juce::Justification::centredLeft, 1);
        if (chordName.isNotEmpty() && getWidth() > pianoKeyWidth + 290)
        {
            const int chordX = pianoKeyWidth + 265;
            const int chordWidth = juce::jmin(142, getWidth() - chordX - 9);
            const auto chordArea = juce::Rectangle<float>((float)chordX, (float)infoY + 1.0f,
                (float)chordWidth, (float)juce::jmax(0, rulerHeight - infoY - 3));
            g.setColour(juce::Colour(0xff384331));
            g.fillRoundedRectangle(chordArea, 4.0f);
            g.setColour(juce::Colour(0xfff3d48b));
            g.drawFittedText(chordName, chordX + 7, infoY + 1,
                juce::jmax(0, chordWidth - 14), rulerHeight - infoY - 3,
                juce::Justification::centred, 1);
        }
        if (!selectedNotes.empty() && getWidth() > pianoKeyWidth + 460)
        {
            g.setColour(juce::Colour(0xffffdf99));
            g.drawText(juce::String::fromUTF8("Durée"), pianoKeyWidth + 158, infoY, 48,
                       rulerHeight - infoY - 1, juce::Justification::centredRight);
            g.drawText(juce::String::fromUTF8("Vél."), pianoKeyWidth + 348, infoY, 38, rulerHeight - infoY - 1, juce::Justification::centredRight);
            g.setColour(juce::Colour(0xffd4e5ed));
            g.drawText("Canal " + juce::String((int)selectedNotes.front().channel)
                       + (selectedNotes.front().muted ? "  MUET" : ""),
                       pianoKeyWidth + 454, infoY, juce::jmax(0, getWidth() - pianoKeyWidth - 460),
                       rulerHeight - infoY - 1, juce::Justification::centredLeft);
        }
        const auto b = getLocalBounds();
        const auto velocityAreaY = juce::jmax(rulerHeight, b.getBottom() - (dualLanes ? dualLaneTotalHeight : velocityLaneHeight));
        const auto grid = juce::Rectangle<int>(pianoKeyWidth, rulerHeight, b.getWidth() - pianoKeyWidth, juce::jmax(1, velocityAreaY - rulerHeight));
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(juce::jmax<std::int64_t>(1, ticksPerMeasure), MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        const double pixelsPerTick = getPixelsPerTick();
        const int keyHeight = pitchRowHeight();

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
        std::array<bool, 128> usedPitches {};
        if (showUsedPitches || focusUsedPitches)
            for (const auto& midiNote : owner.getMidiEngine().getNotesCopy())
                if ((int)midiNote.pitch >= 0 && (int)midiNote.pitch < 128)
                    usedPitches[(size_t)midiNote.pitch] = true;
        for (int row = 0; row < visiblePitchRows(); ++row)
        {
            const int note = lowestVisibleKey + row;
            const int y = rulerHeight + (visiblePitchRows() - 1 - row) * keyHeight;
            const auto key = juce::Rectangle<int>(0, y, pianoKeyWidth, keyHeight);
            // Fender-inspired scale overlay: purely visual, no MIDI or Snap mutation.
            if (scaleHighlight && y >= rulerHeight && y < velocityAreaY)
            {
                static const int masks[5][12] = {
                    { 1,0,1,0,1,1,0,1,0,1,0,1 },
                    { 1,0,1,1,0,1,0,1,1,0,1,0 },
                    { 1,0,1,0,1,0,0,1,0,1,0,0 },
                    { 1,0,0,1,0,1,0,1,0,0,1,0 },
                    { 1,0,0,1,0,1,1,1,0,0,1,0 }
                };
                const int degree = ((note - scaleRoot) % 12 + 12) % 12;
                if (masks[scaleType][degree] != 0)
                {
                    g.setColour(degree == 0 ? juce::Colour(0xffb28a46).withAlpha(0.23f)
                        : juce::Colour(0xff7f9c79).withAlpha(0.13f));
                    g.fillRect(grid.getX(), y, grid.getWidth(), keyHeight);
                }
            }
            if (showUsedPitches && note >= 0 && note < 128 && usedPitches[(size_t)note])
            {
                g.setColour(juce::Colour(0xff54d6dc).withAlpha(0.18f));
                g.fillRect(grid.getX(), y, grid.getWidth(), keyHeight);
            }
            // Focus mode dims unused rows but never removes their geometry.
            // This preserves scrolling, pitch hit-testing and MIDI editing.
            if (focusUsedPitches && note >= 0 && note < 128 && !usedPitches[(size_t)note])
            {
                g.setColour(juce::Colour(0xff050a10).withAlpha(0.67f));
                g.fillRect(grid.getX(), y, grid.getWidth(), keyHeight);
            }
            const bool black = isBlackKey(note);
            g.setColour(auditionPitch == note ? juce::Colour(0xff54d6dc) : (black ? juce::Colour(0xff111922) : juce::Colour(0xffdbe3ea))); g.fillRect(key);
            // Obsidian keyboard edges: clearer black-key outlines, softer white-key dividers.
            g.setColour(black ? juce::Colour(0xff48606d) : juce::Colour(0xff91a4b0)); g.drawRect(key, 1);
            // Darker Obsidian note labels stay legible on light piano keys.
            if (!black && keyHeight >= 13) { g.setColour(juce::Colour(0xff17242e)); g.setFont(juce::Font(9.0f)); g.drawText(juce::String(noteName(note)) + juce::String(note / 12 - 1), key.reduced(5, 0), juce::Justification::centredLeft); }
            if (showUsedPitches && keyHeight >= 8 && note >= 0 && note < 128 && usedPitches[(size_t)note])
            {
                g.setColour(juce::Colour(0xff54d6dc));
                g.fillRoundedRectangle((float)pianoKeyWidth - 9.0f, (float)y + 3.0f,
                    5.0f, (float)juce::jmax(3, juce::jmax(1, keyHeight - 6)), 2.0f);
            }
            // One horizontal pitch separator per piano key, aligned exactly
            // with the note row in the dock and in the floating editor.
            if (y >= rulerHeight && y < velocityAreaY)
            {
                g.setColour(juce::Colour(0xff344452));
                g.fillRect(grid.getX(), y, grid.getWidth(), 1);
            }
        }
        // Bottom boundary of the lowest pitch row.
        const int lowestRowBottom = rulerHeight + visiblePitchRows() * keyHeight;
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
            if (n.pitch < lowestVisibleKey || n.pitch >= lowestVisibleKey + visiblePitchRows() || n.startTick >= clipLengthTicks) continue;
            const int y = rulerHeight + (visiblePitchRows() - 1 - ((int)n.pitch - lowestVisibleKey)) * keyHeight + (keyHeight >= 8 ? 2 : 0);
            const int x = grid.getX() + (int)std::llround((double)n.startTick * pixelsPerTick);
            const auto visibleLengthTicks = juce::jmin(n.lengthTicks, clipLengthTicks - n.startTick);
            const int w = juce::jmax(8, (int)std::llround((double)visibleLengthTicks * pixelsPerTick));
            const auto r = juce::Rectangle<int>(x + 1, y, w - 2, juce::jmax(1, keyHeight - (keyHeight >= 8 ? 4 : 1)));
            if (r.getRight() <= grid.getX() || r.getX() >= grid.getRight()) continue;
            const auto noteEnd = n.startTick + n.lengthTicks;
            const bool active = playbackTick >= n.startTick && playbackTick < noteEnd && owner.getMidiEngine().isPlaying();
            // Velocity heatmap: low = blue, medium = green, high = amber, maximum = red.
            // Selection and playback use outlines so the velocity colour remains visible.
            const bool selected = owner.getMidiEngine().isNoteSelected(n);
            const juce::Colour velocityColour = colourForMidiVelocity((int)n.velocity);
            g.setColour(n.muted ? juce::Colour(0xff46505a) : velocityColour);
            g.fillRoundedRectangle(r.toFloat(), juce::jmin(3.0f, (float)r.getHeight() * 0.35f));
            // Subtle Obsidian highlight adds definition to idle MIDI notes.
            g.setColour(juce::Colour(0xffe8eff7).withAlpha(active ? 0.24f : 0.16f));
            g.fillRoundedRectangle(r.toFloat().withHeight(juce::jmin(4.0f, (float)r.getHeight())), 2.0f);
            // Defined idle-note outlines improve separation without changing active playback styling.
            g.setColour(n.muted ? juce::Colour(0xff75808b) : selected ? juce::Colour(0xffffe4ac) : active ? juce::Colour(0xffffffff) : velocityColour.brighter(0.35f));
            g.drawRoundedRectangle(r.toFloat(), juce::jmin(3.0f, (float)r.getHeight() * 0.35f),
                juce::jmin((float)r.getHeight() * 0.5f, selected ? 2.2f : active ? 1.5f : 1.0f));
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        if (dualLanes)
        {
            const int top = getHeight() - dualLaneTotalHeight;
            if (std::abs(e.y - top) <= 6 || std::abs(e.y - (top + dualLaneFirstHeight)) <= 6)
            {
                setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
                return;
            }
        }

        if (draggingNote) return;
        if (isInVelocityLane(e.y)) { setMouseCursor(juce::MouseCursor::UpDownResizeCursor); return; }
        const auto side = findResizeSide(e.position);
        if (side != ResizeSide::none) { setMouseCursor(juce::MouseCursor::LeftRightResizeCursor); return; }
        if (findNoteAt(e.position)) { setMouseCursor(juce::MouseCursor::DraggingHandCursor); return; }
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
    }
    void mouseExit(const juce::MouseEvent&) override { if (!draggingNote) setMouseCursor(juce::MouseCursor::CrosshairCursor); }

    // Both the embedded dock and the floating editor use this PianoRoll.
    // Handle note insertion on the editor itself rather than relying on a
    // separate selection overlay that may only exist in floating windows.
    void paintOverChildren(juce::Graphics& g) override
    {
        // Never retain a selection marquee while the user is drawing notes.
        if (!selectingRectangle || drawingNote || getLibertyActiveTool() != 7) return;
        const auto area = juce::Rectangle<int>::leftTopRightBottom(juce::jmin(rectangleStart.x, rectangleEnd.x), juce::jmin(rectangleStart.y, rectangleEnd.y), juce::jmax(rectangleStart.x, rectangleEnd.x), juce::jmax(rectangleStart.y, rectangleEnd.y));
        g.setColour(juce::Colour(0xff56bfd9).withAlpha(0.14f)); g.fillRect(area);
        g.setColour(juce::Colour(0xff56bfd9)); g.drawRect(area, 1);
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (!e.mods.isLeftButtonDown() || e.x < pianoKeyWidth
            || !isWithinPitchRows(e.y))
            return;
        // Double-click inserts a note regardless of the active editing tool,
        // both in the dock and in the floating editor (same PianoRoll instance).
        const int pitch = snapPitchToScale(pitchFromY(e.y));
        const auto tick = tickFromX(e.x);
        if (owner.getMidiEngine().addNote(tick, drawingDurationTicks(), pitch, 100, 1))
        {
            owner.updateMidiClipTiming(); synchronizeLibertyPianoRollPlayback(owner);
            repaint();
            owner.repaint();
        }
    }

    void visibilityChanged() override
    {
        // A dock/editor can be hidden while the mouse is still down.
        // Never leave the MIDI engine in grouped-undo mode afterwards.
        if (!isVisible() && (draggingNote || draggingGroup || drawingNote))
        {
            owner.getMidiEngine().endGroupedEdit();
            draggingNote = false;
            draggingGroup = false;
            drawingNote = false;
            dragMoved = false;
            resizeSide = ResizeSide::none;
            clickSelectionPending = false;
            originalLengthTicks = 0;
            dragEndTick = 0;
            setMouseCursor(juce::MouseCursor::CrosshairCursor);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        // Recover from a missing mouseUp before beginning a new gesture.
        if (draggingNote || draggingGroup || drawingNote)
        {
            owner.getMidiEngine().endGroupedEdit();
            draggingNote = false;
            draggingGroup = false;
            drawingNote = false;
            resizeSide = ResizeSide::none;
            dragMoved = false;
            originalLengthTicks = 0;
            dragEndTick = 0;
            clickSelectionPending = false;
        }
        if (dualLanes && e.mods.isLeftButtonDown())
        {
            const int top = getHeight() - dualLaneTotalHeight;
            const int divider = top + dualLaneFirstHeight;
            if (std::abs(e.y - top) <= 6) { resizingLaneTop = true; return; }
            if (std::abs(e.y - divider) <= 6) { resizingLaneDivider = true; return; }
        }
        if (e.mods.isLeftButtonDown() && e.x >= 0 && e.x < pianoKeyWidth
            && isWithinPitchRows(e.y))
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
        const int keyHeight = pitchRowHeight();
        auto& midi = owner.getMidiEngine();
        for (const auto& n : midi.getNotesCopy())
        {
            if (n.pitch < lowestVisibleKey || n.pitch >= lowestVisibleKey + visiblePitchRows()) continue;
            const int x = pianoKeyWidth + (int)std::llround((double)n.startTick * getPixelsPerTick());
            const int y = rulerHeight + (visiblePitchRows() - 1 - ((int)n.pitch - lowestVisibleKey)) * keyHeight + (keyHeight >= 8 ? 2 : 0);
            const int w = juce::jmax(8, (int)std::llround((double)n.lengthTicks * getPixelsPerTick()));
            const auto noteRect = juce::Rectangle<int>(x + 1, y, w - 2, juce::jmax(1, keyHeight - (keyHeight >= 8 ? 4 : 1)));
            if (n.pitch == pitch && noteRect.contains(e.getPosition()))
            {
                if (tool == 8) // Non-destructive per-note MIDI mute.
                {
                    auto notes = midi.getNotesCopy();
                    const bool targetMute = !n.muted;
                    const bool group = midi.getNumSelectedNotes() > 1 && midi.isNoteSelected(n);
                    for (auto& note : notes)
                        if (group ? midi.isNoteSelected(note) :
                            (note.startTick == n.startTick && note.lengthTicks == n.lengthTicks
                                && note.pitch == n.pitch && note.channel == n.channel))
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
                            if (part.startTick == n.startTick && part.lengthTicks == n.lengthTicks
                                && part.pitch == n.pitch && part.channel == n.channel)
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

                if (tool == 0 || tool == 1)
                {
                    // Click toggles a note without altering its MIDI data.
                    // Keep dragging available with the selection tool.
                    clickSelectionPending = true;
                    clickWasSelected = midi.isNoteSelected(n);
                    clickNote = n;
                    if (tool == 1) return;
                }

                // Detect notes sharing the same onset, pitch and channel so that
                // adjacent-boundary resizing does not accidentally change a neighbour.
                const auto allNotes = midi.getNotesCopy();
                const bool ambiguousNote = std::any_of(allNotes.begin(), allNotes.end(),
                    [&n](const auto& other)
                    {
                        return other.startTick == n.startTick && other.pitch == n.pitch
                            && other.channel == n.channel && other.lengthTicks != n.lengthTicks;
                    });

                if (tool == 0 && midi.isNoteSelected(n) && midi.getNumSelectedNotes() > 1)
                {
                    draggingGroup = true;
                    midi.beginGroupedEdit();
                    groupDragTick = tickFromX(e.x);
                    groupDragPitch = pitchFromY(e.y);
                    return;
                }
                if (tool == 4) // RESIZE / REDIMENSIONNER: edge only
                {
                    const auto side = findResizeSide(e.position);
                    if (side == ResizeSide::none) return;
                    // Both edges now use exact-identity operations for overlapping notes.
                    draggingNote = true; dragMoved = false; resizeSide = side;
                }
                else if (tool == 0) // SELECT / SELECTION: move without deleting on click
                {
                    draggingNote = true; dragMoved = false; resizeSide = ResizeSide::none;
                }
                else
                    return;

                midi.beginGroupedEdit();
                dragStartTick = n.startTick; dragEndTick = n.startTick + n.lengthTicks;
                dragPitch = n.pitch; dragChannel = n.channel; originalLengthTicks = n.lengthTicks;
                resizingAdjacent = false;
                if (resizeSide == ResizeSide::right && !ambiguousNote)
                {
                    const auto boundary = n.startTick + n.lengthTicks;
                    for (const auto& next : midi.getNotesCopy())
                        if (next.startTick == boundary && next.pitch == n.pitch && next.channel == n.channel)
                        { resizingAdjacent = true; break; }
                }
                setMouseCursor(resizeSide == ResizeSide::none ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::LeftRightResizeCursor);
                return;
            }
        }

        if (tool == 1) // DRAW / DESSINER
        {
            // A previous marquee must not survive into a note-creation gesture.
            selectingRectangle = false;
            const auto start = tickFromX(e.x);
            const auto length = drawingDurationTicks();
            const int notePitch = snapPitchToScale(pitch);
            if (midi.addNote(start, length, notePitch, 100, 1))
            {
                drawingNote = true;
                drawingStartTick = start;
                drawingLengthTicks = length;
                drawingPitch = notePitch;
                drawingStartY = e.y;
                drawingVelocity = 100;
                midi.beginGroupedEdit();
                owner.updateMidiClipTiming();
                synchronizeLibertyPianoRollPlayback(owner);
                repaint(); owner.repaint();
            }
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (drawingNote)
        {
            auto& midi = owner.getMidiEngine();
            const auto snap = juce::jmax<std::int64_t>(1, activePianoSnapTicks());
            const auto end = tickFromX(e.x);
            const auto newLength = juce::jmax(snap, end - drawingStartTick);
            // Horizontal motion sets duration; vertical motion sets velocity.
            const int newVelocity = juce::jlimit(1, 127,
                100 + (drawingStartY - e.y) / 3);
            bool changed = false;
            if (newLength != drawingLengthTicks
                && midi.setNoteLength(drawingStartTick, drawingPitch, 1, newLength))
            {
                drawingLengthTicks = newLength;
                changed = true;
            }
            if (newVelocity != drawingVelocity
                && midi.setNoteVelocity(drawingStartTick, drawingPitch, 1, newVelocity))
            {
                drawingVelocity = newVelocity;
                changed = true;
            }
            if (changed)
            {
                owner.updateMidiClipTiming();
                synchronizeLibertyPianoRollPlayback(owner);
                repaint(); owner.repaint();
            }
            return;
        }
        if (resizingLaneTop)
        {
            const int oldHeight = dualLaneTotalHeight;
            dualLaneTotalHeight = juce::jlimit(96, juce::jmax(96, getHeight() - rulerHeight - 40), getHeight() - e.y);
            dualLaneFirstHeight = juce::jlimit(36, dualLaneTotalHeight - 36,
                dualLaneFirstHeight + dualLaneTotalHeight - oldHeight);
            resized(); repaint(); return;
        }
        if (resizingLaneDivider)
        {
            dualLaneFirstHeight = juce::jlimit(36, dualLaneTotalHeight - 36,
                e.y - (getHeight() - dualLaneTotalHeight));
            resized(); repaint(); return;
        }
        if (draggingGroup)
        {
            const auto tick = tickFromX(e.x);
            const int pitch = pitchFromY(e.y);
            const auto dt = tick - groupDragTick;
            const int dp = pitch - groupDragPitch;
            // Move the whole selection by one common interval. Preserve the
            // original chord voicing instead of quantizing each voice separately.
            int snappedDp = dp;
            if (scaleSnap && dp != 0)
            {
                const auto selected = owner.getMidiEngine().getSelectedNotesCopy();
                if (!selected.empty())
                {
                    const auto anchor = std::min_element(selected.begin(), selected.end(),
                        [](const auto& a, const auto& b) { return a.pitch < b.pitch; });
                    const int anchorPitch = (int)anchor->pitch;
                    snappedDp = snapPitchToScale(anchorPitch + dp) - anchorPitch;
                }
            }
            // Clamp both axes against the *entire* selection before applying
            // one uniform interval. The MIDI engine remains responsible for
            // collision checks, undo and the final validity decision.
            const auto selected = owner.getMidiEngine().getSelectedNotesCopy();
            if (!selected.empty())
            {
                int minimumPitch = 127, maximumPitch = 0;
                std::int64_t minimumTick = selected.front().startTick;
                for (const auto& note : selected)
                {
                    minimumPitch = juce::jmin(minimumPitch, (int)note.pitch);
                    maximumPitch = juce::jmax(maximumPitch, (int)note.pitch);
                    minimumTick = juce::jmin(minimumTick, note.startTick);
                }
                snappedDp = juce::jlimit(-minimumPitch, 127 - maximumPitch, snappedDp);
                const auto safeDt = juce::jmax(dt, -minimumTick);
                // A collision on a diagonal destination must not freeze
                // both axes. Try the complete move first; if rejected,
                // independently attempt the horizontal and vertical parts.
                // Each attempt still passes through the engine's collision
                // checks, so unselected notes are never overwritten.
                std::int64_t appliedTicks = 0;
                int appliedPitch = 0;
                auto& midi = owner.getMidiEngine();
                if ((safeDt != 0 || snappedDp != 0)
                    && midi.moveSelectedNotesBy(safeDt, snappedDp))
                {
                    appliedTicks = safeDt;
                    appliedPitch = snappedDp;
                }
                else
                {
                    if (safeDt != 0 && midi.moveSelectedNotesBy(safeDt, 0))
                        appliedTicks = safeDt;
                    if (snappedDp != 0 && midi.moveSelectedNotesBy(0, snappedDp))
                        appliedPitch = snappedDp;
                }
                if (appliedTicks != 0 || appliedPitch != 0)
                {
                    groupDragTick += appliedTicks;
                    // Only consume mouse movement actually applied to MIDI.
                    groupDragPitch += appliedPitch;
                    owner.updateMidiClipTiming();
                    synchronizeLibertyPianoRollPlayback(owner);
                    repaint();
                    owner.repaint();
                }
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
                const int y = rulerHeight + (visiblePitchRows() - 1 - ((int)n.pitch - lowestVisibleKey)) * pitchRowHeight() + 2;
                const int w = juce::jmax(8, (int) std::llround(n.lengthTicks * getPixelsPerTick()));
                if (area.intersects(juce::Rectangle<int>(x, y, w, juce::jmax(1, pitchRowHeight() - 3)))) selected.push_back(n);
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
            const int next = e.x >= 0 && e.x < pianoKeyWidth && isWithinPitchRows(e.y)
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
            if (newLength != originalLengthTicks && (resizingAdjacent
                ? owner.getMidiEngine().resizeAdjacentNoteBoundary(dragStartTick, dragPitch, dragChannel, newLength)
                : owner.getMidiEngine().setNoteLengthExact(dragStartTick, dragPitch, dragChannel, originalLengthTicks, newLength))) { originalLengthTicks = newLength; dragEndTick = dragStartTick + newLength; dragMoved = true; owner.updateMidiClipTiming(); synchronizeLibertyPianoRollPlayback(owner); repaint(); owner.repaint(); }
            return;
        }
        if (resizeSide == ResizeSide::left)
        {
            const auto maxStart = juce::jmax<std::int64_t>(0, dragEndTick - juce::jmax<std::int64_t>(1, activePianoSnapTicks()));
            const auto newStart = juce::jlimit<std::int64_t>(0, maxStart, mouseTick);
            const auto newLength = dragEndTick - newStart;
            if (newStart != dragStartTick && newLength >= gridTicks && owner.getMidiEngine().resizeNoteLeftExact(dragStartTick, dragPitch, dragChannel, originalLengthTicks, newStart)) { dragStartTick = newStart; originalLengthTicks = newLength; dragMoved = true; owner.updateMidiClipTiming(); synchronizeLibertyPianoRollPlayback(owner); repaint(); owner.repaint(); }
            return;
        }
        const auto newTick = tickFromX(e.x); const int newPitch = snapPitchToScale(pitchFromY(e.y));
        if (newTick == dragStartTick && newPitch == dragPitch) return;
        if (owner.getMidiEngine().moveNoteExact(dragStartTick, dragPitch, dragChannel, originalLengthTicks, newTick, newPitch)) { dragStartTick = newTick; dragPitch = newPitch; dragEndTick = newTick + originalLengthTicks; dragMoved = true; owner.updateMidiClipTiming(); synchronizeLibertyPianoRollPlayback(owner); repaint(); owner.repaint(); }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (drawingNote)
        {
            owner.getMidiEngine().endGroupedEdit();
            drawingNote = false;
            // Explicitly invalidate the floating editor after note creation.
            // Otherwise the last transient drawing frame can remain visible
            // until the next note triggers another repaint.
            repaint();
            return;
        }
        if (resizingLaneTop || resizingLaneDivider)
        {
            resizingLaneTop = false;
            resizingLaneDivider = false;
            repaint();
            return;
        }
        if (draggingGroup) { owner.getMidiEngine().endGroupedEdit(); draggingGroup = false; clickSelectionPending = false; repaint(); return; }
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
        if (clickSelectionPending && !dragMoved)
        {
            auto& midi = owner.getMidiEngine();
            auto selected = midi.getSelectedNotesCopy();
            const auto sameNote = [this](const MidiEngine::NoteEvent& note)
            {
                return note.startTick == clickNote.startTick
                    && note.lengthTicks == clickNote.lengthTicks
                    && note.pitch == clickNote.pitch && note.channel == clickNote.channel;
            };
            if (clickWasSelected)
                selected.erase(std::remove_if(selected.begin(), selected.end(), sameNote), selected.end());
            else
                selected.push_back(clickNote);
            midi.setSelectedNotes(selected);
            repaint();
            owner.repaint();
        }
        clickSelectionPending = false;
        if (!draggingNote) return;
        owner.getMidiEngine().endGroupedEdit();
        owner.updateMidiClipTiming(); synchronizeLibertyPianoRollPlayback(owner); draggingNote = false; dragMoved = false; resizeSide = ResizeSide::none; originalLengthTicks = 0; dragEndTick = 0;
        setMouseCursor(juce::MouseCursor::CrosshairCursor); repaint(); owner.repaint();
    }

private:
    int scaleRoot = 0, scaleType = 0;
    int pitchViewBeforeFrame = lowestKey;
    int framedPitchRows = 40;
    int visiblePitchRows() const noexcept { return framedPitchRows; }
    int pitchRowHeight() const noexcept
    {
        if (!pitchViewFramed) return currentKeyHeight();
        // Fit the framed MIDI pitch range to the space above controller lanes.
        // One common height is used by painting, hit-testing and note handles.
        const int laneHeight = dualLanes ? dualLaneTotalHeight : velocityLaneHeight;
        const int available = juce::jmax(1, getHeight() - rulerHeight - laneHeight);
        // In a short dock, allow sub-10px rows but keep the exact same
        // pitch geometry for rendering and hit-testing.
        return juce::jmax(1, juce::jmin(currentKeyHeight() * 3,
            available / juce::jmax(1, visiblePitchRows())));
    }
    bool pitchViewFramed = false;
    bool scaleHighlight = false, scaleSnap = false, showUsedPitches = false, focusUsedPitches = false;
    std::array<juce::TextButton, 9> toolButtons;
    juce::TextButton quantizeButton, selectAllButton, undoMidiButton, redoMidiButton, moreToolsButton;
    juce::TextEditor velocityValue;
    juce::TextButton durationValue;
    int selectedDurationId = 0;
    bool clickSelectionPending = false;
    bool clickWasSelected = false;
    MidiEngine::NoteEvent clickNote {};
    std::unique_ptr<DurationPalette> durationPalette;
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
    bool isInVelocityLane(int y) const noexcept { return y >= juce::jmax(rulerHeight, getHeight() - (dualLanes ? dualLaneTotalHeight : velocityLaneHeight)) && y < getHeight(); }

    bool findNoteAt(juce::Point<float> position) const
    {
        if (position.x < pianoKeyWidth || !isWithinPitchRows((int)position.y)) return false;
        const auto pixelsPerTick = getPixelsPerTick();
        const int keyHeight = pitchRowHeight();
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(juce::jmax<std::int64_t>(1, ticksPerMeasure), MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        for (const auto& n : owner.getMidiEngine().getNotesCopy())
        {
            if (n.pitch < lowestVisibleKey || n.pitch >= lowestVisibleKey + visiblePitchRows() || n.startTick >= clipLengthTicks) continue;
            const int x = pianoKeyWidth + (int)std::llround((double)n.startTick * pixelsPerTick);
            const int y = rulerHeight + (visiblePitchRows() - 1 - ((int)n.pitch - lowestVisibleKey)) * keyHeight + (keyHeight >= 8 ? 2 : 0);
            const auto visibleLengthTicks = juce::jmin(n.lengthTicks, clipLengthTicks - n.startTick);
            const int w = juce::jmax(8, (int)std::llround((double)visibleLengthTicks * pixelsPerTick));
            const auto noteRect = juce::Rectangle<int>(x + 1, y, w - 2, juce::jmax(1, keyHeight - (keyHeight >= 8 ? 4 : 1)));
            if (n.pitch == pitchFromY((int)position.y) && noteRect.contains((int)position.x, (int)position.y)) return true;
        }
        return false;
    }

    ResizeSide findResizeSide(juce::Point<float> position) const
    {
        if (position.x < pianoKeyWidth || !isWithinPitchRows((int)position.y)) return ResizeSide::none;
        const auto pixelsPerTick = getPixelsPerTick();
        const int keyHeight = pitchRowHeight();
        const auto ticksPerMeasure = MidiEngine::ticksPerMeasure(owner.getTimeSignatureNumerator(), owner.getTimeSignatureDenominator());
        const auto clipLengthTicks = juce::jmax<std::int64_t>(juce::jmax<std::int64_t>(1, ticksPerMeasure), MidiEngine::secondsToTick(owner.getMidiClipLengthSeconds(), owner.getTempoBpm()));
        ResizeSide bestSide = ResizeSide::none; float bestDistance = (float)resizeEdgePixels + 1.0f;
        for (const auto& n : owner.getMidiEngine().getNotesCopy())
        {
            if (n.pitch < lowestVisibleKey || n.pitch >= lowestVisibleKey + visiblePitchRows() || n.startTick >= clipLengthTicks) continue;
            const int x = pianoKeyWidth + (int)std::llround((double)n.startTick * pixelsPerTick);
            const int y = rulerHeight + (visiblePitchRows() - 1 - ((int)n.pitch - lowestVisibleKey)) * keyHeight + (keyHeight >= 8 ? 2 : 0);
            const auto visibleLengthTicks = juce::jmin(n.lengthTicks, clipLengthTicks - n.startTick);
            const int w = juce::jmax(8, (int)std::llround((double)visibleLengthTicks * pixelsPerTick));
            const auto noteRect = juce::Rectangle<int>(x + 1, y, w - 2, juce::jmax(1, keyHeight - (keyHeight >= 8 ? 4 : 1)));
            if (n.pitch != pitchFromY((int)position.y) || !noteRect.contains((int)position.x, (int)position.y)) continue;
            const float leftDistance = std::abs(position.x - (float)noteRect.getX()); const float rightDistance = std::abs(position.x - (float)noteRect.getRight());
            // On very short notes, reserve a centre zone for moving the note
            // rather than letting both resize handles consume its full width.
            const float edgeZone = juce::jmin((float)resizeEdgePixels,
                juce::jmax(1.0f, (float)noteRect.getWidth() * 0.25f));
            if (leftDistance <= edgeZone && leftDistance <= bestDistance)
            {
                bestDistance = leftDistance;
                bestSide = ResizeSide::left;
            }
            if (rightDistance <= edgeZone && rightDistance < bestDistance)
            {
                bestDistance = rightDistance;
                bestSide = ResizeSide::right;
            }
        }
        return bestSide;
    }
    void timerCallback() override {
        auto& midi = owner.getMidiEngine(); midi.setPlaybackPositionSeconds(owner.getAudioCurrentTimeSeconds()); midi.setPlaying(owner.isAudioPlaying());
        const auto selected = midi.getSelectedNotesCopy();
        const bool hasSelection = !selected.empty();
        if (undoMidiButton.isEnabled() != midi.canUndo()) undoMidiButton.setEnabled(midi.canUndo());
        if (redoMidiButton.isEnabled() != midi.canRedo()) redoMidiButton.setEnabled(midi.canRedo());
        // Keep the compact duration selector continuously visible while it
        // fits in the toolbar. MIDI selection can briefly fluctuate during
        // 30 Hz synchronization; toggling visibility each frame makes this
        // small button flash even though the note duration works correctly.
        const bool shouldShowDuration = durationValue.getWidth() >= 95;
        if (durationValue.isVisible() != shouldShowDuration)
            durationValue.setVisible(shouldShowDuration);
        // The selection poll must never overwrite the duration explicitly
        // chosen by the user. The click handler updates the compact label
        // immediately; repeatedly deriving it from a transient note snapshot
        // caused visible label oscillation at 30 Hz.
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
    bool isWithinPitchRows(int y) const noexcept
    {
        const int laneTop = juce::jmax(rulerHeight,
            getHeight() - (dualLanes ? dualLaneTotalHeight : velocityLaneHeight));
        const int rowsBottom = rulerHeight + visiblePitchRows() * pitchRowHeight();
        return y >= rulerHeight && y < juce::jmin(laneTop, rowsBottom);
    }
    int pitchFromY(int y) const noexcept
    {
        const int keyHeight = pitchRowHeight();
        const int row = juce::jlimit(0, visiblePitchRows() - 1,
            (y - rulerHeight) / juce::jmax(1, keyHeight));
        return juce::jlimit(0, 127,
            lowestVisibleKey + visiblePitchRows() - 1 - row);
    }
    std::int64_t tickFromX(int x) const noexcept { const auto raw = (std::int64_t)std::llround((x - pianoKeyWidth) / getPixelsPerTick()); const auto tick = juce::jmax<std::int64_t>(0, raw); const auto snap = activePianoSnapTicks(); return snap > 0 ? MidiEngine::quantizeTick(tick, snap) : tick; }

    MainComponent& owner;
    VelocityLane velocityLane;
    int activeController = -1;
    ControllerLane controllerLane;
    juce::ComboBox controllerSelector;
    class LaneResizeHandle : public juce::Component
    {
    public:
        std::function<void(int)> onDrag;
        LaneResizeHandle()
        {
            setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
            setInterceptsMouseClicks(true, false);
        }
        void paint(juce::Graphics& g) override
        {
            g.setColour(juce::Colour(0xff384955));
            g.fillRect(0, getHeight() / 2 - 1, getWidth(), 2);
            g.setColour(juce::Colour(0xff7c9ca8));
            g.fillRoundedRectangle((float)getWidth() / 2.0f - 18.0f,
                (float)getHeight() / 2.0f - 2.0f, 36.0f, 4.0f, 2.0f);
        }
        void mouseDrag(const juce::MouseEvent& e) override
        {
            if (onDrag != nullptr)
                onDrag(e.getEventRelativeTo(getParentComponent()).y);
        }
    };
    LaneResizeHandle topLaneHandle, dividerLaneHandle;
    class DualLaneLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) override
        {
            auto r = button.getLocalBounds().toFloat().reduced(8.0f, 6.0f);
            g.setColour(juce::Colour(0xffd8edf3));
            g.drawRoundedRectangle(r, 2.0f, 1.5f);
            g.drawLine(r.getX() + 2.0f, r.getCentreY(), r.getRight() - 2.0f, r.getCentreY(), 1.4f);
        }
    };
    DualLaneLookAndFeel dualLaneLookAndFeel;
    juce::TextButton dualLaneButton;
    bool dualLanes = false;
    int dualLaneTotalHeight = 184;
    int dualLaneFirstHeight = 92;
    bool resizingLaneTop = false;
    bool resizingLaneDivider = false;
    bool drawingNote = false;
    std::int64_t drawingStartTick = 0, drawingLengthTicks = 0;
    int drawingPitch = 60, drawingStartY = 0, drawingVelocity = 100;
    bool resizingAdjacent = false;
    bool draggingNote = false, dragMoved = false;
    ResizeSide resizeSide = ResizeSide::none;
    std::int64_t dragStartTick = 0, dragEndTick = 0, originalLengthTicks = 0;
    int dragPitch = 60, dragChannel = 1;
};

class MidiEditorWindow final : public juce::DocumentWindow
{
public:
    explicit MidiEditorWindow(juce::Component& editor)
        : DocumentWindow("Liberty - Piano Roll", juce::Colour(0xff0b0d10),
                         DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        // The Piano Roll belongs to dockPianoRoll, not to this window.
        // Never delete it when closing the floating window.
        setContentNonOwned(&editor, true);
        setResizable(true, true);
        setResizeLimits(900, 500, 1800, 1000);
        centreWithSize(1200, 760);
        setVisible(true);
        toFront(true);
    }
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
        // Child components can receive the arranger's double-click instead
        // of MainComponent. Route audio clips through the same shared action.
        if (handleLibertyClipNameDoubleClick(*main,p)) return;
        if (main->openAudioClipInDockAt(p)) return;
        // A global mouse listener must never use a legacy fixed MIDI-lane Y.
        // Only an actual MIDI track inside the visible arranger may open this editor.
        if (p.x < main->trackHeaderWidth || p.y < main->getArrangeTop()
            || p.y >= main->getMixerTop()) return;
        const int track = main->getArrangeTrackAtY(p.y);
        const int midiFirst = main->getAudioTrackCount();
        if (track >= midiFirst && track < midiFirst + main->getMidiTrackCount())
            openLibertyMidiEditorInDock(*main);
    }
private: bool registered = true;
};

MidiEditorMouseListener globalMidiEditorMouseListener;
std::vector<std::unique_ptr<MidiEditorWindow>> midiEditorWindows;
std::unique_ptr<PianoRoll> dockPianoRoll;
MainComponent* dockPianoRollOwner = nullptr;
}

void openLibertyMidiEditorInDock(MainComponent& owner)
{
    // Only the explicit detach button may create a floating editor.
    midiEditorWindows.clear();
    owner.activateDockTab(1);
    owner.resized();
    owner.repaint();
}

void openLibertyMidiEditor(MainComponent& owner)
{
    if (!dockPianoRoll || dockPianoRollOwner != &owner)
    {
        midiEditorWindows.clear();
        dockPianoRoll.reset();
        dockPianoRollOwner = &owner;
        dockPianoRoll = std::make_unique<PianoRoll>(owner);
    }
    for (auto& w : midiEditorWindows)
        if (w != nullptr && w->isVisible()) { w->toFront(true); return; }

    // Reuse the dock's editor and preserve its state when detaching.
    midiEditorWindows.clear();
    dockPianoRoll->setVisible(true);
    midiEditorWindows.push_back(std::make_unique<MidiEditorWindow>(*dockPianoRoll));
}

void shutdownLibertyMidiEditor()
{
    // The non-owned floating content must be detached before its owner dies.
    midiEditorWindows.clear();
    dockPianoRoll.reset();
    dockPianoRollOwner = nullptr;
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
    // Reattach the same component when the floating window has been closed.
    const bool floating = !midiEditorWindows.empty() && midiEditorWindows.front() != nullptr
                          && midiEditorWindows.front()->isVisible();
    if (floating)
        return;
    if (!midiEditorWindows.empty())
        midiEditorWindows.clear();
    if (dockPianoRoll->getParentComponent() != &owner)
        owner.addAndMakeVisible(*dockPianoRoll);
    dockPianoRoll->setBounds(bounds);
    dockPianoRoll->setVisible(visible && bounds.getWidth() > 80 && bounds.getHeight() > 120);
    // This layout runs at 30 Hz. Raising the piano roll while its popup is
    // open covers the popup; the two surfaces then alternate in front.
    bool durationPaletteOpen = false;
    for (int i = 0; i < owner.getNumChildComponents(); ++i)
        if (auto* child = owner.getChildComponent(i))
            if (child->getName() == "LibertyDurationPaletteOverlay" && child->isVisible())
            {
                durationPaletteOpen = true;
                break;
            }
    if (dockPianoRoll->isVisible() && !durationPaletteOpen)
        dockPianoRoll->toFront(false);
}
