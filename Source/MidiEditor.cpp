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
            for (int i = 0; i < 23; ++i)
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
        explicit EditWindowContent(std::function<juce::String(int, int, int, int)> action, bool hasSelection, std::vector<MidiEngine::NoteEvent> notes)
            : onAction(std::move(action)), previewNotes(std::move(notes))
        {
            static const char* tabs[] = { "NOTES", "TRANSFORMATIONS", "VÉLOCITÉ", "HUMANISER" };
            for (int i = 0; i < 4; ++i)
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
                "HUMANISER VÉLOCITÉ", "HUMANISER TIMING"
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
                "Variation douce des attaques MIDI", "Variation subtile du placement rythmique"
            };
            static const int actionIds[] = { 1, 2, 3, 8, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 };
            for (int i = 0; i < 23; ++i)
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
            const int tabWidth = (available - 18) / 4;
            for (int i = 0; i < 4; ++i)
                categoryButtons[(size_t)i].setBounds(26 + i * (tabWidth + 6), 127, tabWidth, 38);
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
                    const int column = shown / 6;
                    const int row = shown % 6;
                    button->setBounds(26 + column * (columnWidth + 12),
                        184 + row * 68, columnWidth, 56);
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
            for (int i = 0; i < 4; ++i)
            {
                categoryButtons[(size_t)i].setColour(juce::TextButton::buttonColourId,
                    i == currentCategory ? juce::Colour(0xff78603c) : juce::Colour(0xff263544));
                categoryButtons[(size_t)i].setColour(juce::TextButton::textColourOffId,
                    i == currentCategory ? juce::Colours::white : juce::Colour(0xffb9cbd7));
            }
            for (int i = 0; i < (int)buttons.size(); ++i)
                buttons[(size_t)i]->setVisible(currentCategory == 0 ? i < 4 :
                    (currentCategory == 1 ? ((i >= 4 && i < 8) || (i >= 13 && i < 21)) : (currentCategory == 2 && i >= 8 && i < 13)));
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
        std::vector<MidiEngine::NoteEvent> previewNotes;
        juce::String statusMessage;
        juce::Slider startVelocity, endVelocity, curvature, humanizeVelocity, humanizeTiming;
        juce::ComboBox curveSelector;
        juce::TextButton reverseVelocity, resetVelocity, resetCurvature, applyHumanize, regeneratePreview, undoHumanize;
        int previewSeed = 1;
        std::array<juce::TextButton, 4> categoryButtons;
        std::vector<std::unique_ptr<juce::TextButton>> buttons;
        int draggedVelocityEndpoint = -1;
        int currentCategory = 0;
    };

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
            }, owner.getMidiEngine().hasSelectedNote(), owner.getMidiEngine().getSelectedNotesCopy()));
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

    void quantizeSelectionWithStrength(double strength)
    {
        auto& midi = owner.getMidiEngine();
        const auto grid = activePianoSnapTicks();
        if (grid <= 0) return;
        const auto selected = midi.getSelectedNotesCopy();
        bool changed = false;
        for (const auto& note : selected)
        {
            const auto nearest = (std::int64_t)std::llround((double)note.startTick / (double)grid) * grid;
            const auto offset = (std::int64_t)std::llround((double)(nearest - note.startTick) * strength);
            const auto target = juce::jmax<std::int64_t>(0, note.startTick + offset);
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

    void applySwingToSelection(double ratio)
    {
        auto& midi = owner.getMidiEngine();
        const auto grid = activePianoSnapTicks();
        if (grid <= 0) return;
        const auto selected = midi.getSelectedNotesCopy();
        bool changed = false;
        const auto pairTicks = grid * 2;
        for (const auto& note : selected)
        {
            const auto step = (std::int64_t)std::llround((double)note.startTick / (double)grid);
            if (step % 2 == 0) continue;
            const auto pairStart = (step / 2) * pairTicks;
            const auto target = juce::jmax<std::int64_t>(0,
                pairStart + (std::int64_t)std::llround((double)pairTicks * ratio));
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
        std::sort(selected.begin(), selected.end(), [](const auto& a, const auto& b)
        {
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
        g.setColour(juce::Colour(0xffd4e5ed));
        g.drawFittedText(info, pianoKeyWidth + 9, infoY, juce::jmax(0, juce::jmin(258, getWidth() - pianoKeyWidth - 14)),
                         rulerHeight - infoY - 1, juce::Justification::centredLeft, 1);
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
            // Velocity heatmap: low = blue, medium = green, high = amber, maximum = red.
            // Selection and playback use outlines so the velocity colour remains visible.
            const bool selected = owner.getMidiEngine().isNoteSelected(n);
            const juce::Colour velocityColour = colourForMidiVelocity((int)n.velocity);
            g.setColour(n.muted ? juce::Colour(0xff46505a) : velocityColour);
            g.fillRoundedRectangle(r.toFloat(), 3.0f);
            // Subtle Obsidian highlight adds definition to idle MIDI notes.
            g.setColour(juce::Colour(0xffe8eff7).withAlpha(active ? 0.24f : 0.16f));
            g.fillRoundedRectangle(r.toFloat().withHeight(juce::jmin(4.0f, (float)r.getHeight())), 2.0f);
            // Defined idle-note outlines improve separation without changing active playback styling.
            g.setColour(n.muted ? juce::Colour(0xff75808b) : selected ? juce::Colour(0xffffe4ac) : active ? juce::Colour(0xffffffff) : velocityColour.brighter(0.35f));
            g.drawRoundedRectangle(r.toFloat(), 3.0f, selected ? 2.2f : active ? 1.5f : 1.0f);
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
        if (owner.getMidiEngine().addNote(tick, drawingDurationTicks(), pitch, 100, 1))
        {
            owner.updateMidiClipTiming(); synchronizeLibertyPianoRollPlayback(owner);
            repaint();
            owner.repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (dualLanes && e.mods.isLeftButtonDown())
        {
            const int top = getHeight() - dualLaneTotalHeight;
            const int divider = top + dualLaneFirstHeight;
            if (std::abs(e.y - top) <= 6) { resizingLaneTop = true; return; }
            if (std::abs(e.y - divider) <= 6) { resizingLaneDivider = true; return; }
        }
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

                if (tool == 0 || tool == 1)
                {
                    // Click toggles a note without altering its MIDI data.
                    // Keep dragging available with the selection tool.
                    clickSelectionPending = true;
                    clickWasSelected = midi.isNoteSelected(n);
                    clickNote = n;
                    if (tool == 1) return;
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
            if (midi.addNote(tickFromX(e.x), drawingDurationTicks(), pitch, 100, 1))
            { owner.updateMidiClipTiming(); synchronizeLibertyPianoRollPlayback(owner); repaint(); owner.repaint(); }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
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
        if (resizingLaneTop || resizingLaneDivider)
        {
            resizingLaneTop = false;
            resizingLaneDivider = false;
            repaint();
            return;
        }
        if (draggingGroup) { draggingGroup = false; clickSelectionPending = false; repaint(); return; }
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
        owner.updateMidiClipTiming(); synchronizeLibertyPianoRollPlayback(owner); draggingNote = false; dragMoved = false; resizeSide = ResizeSide::none; originalLengthTicks = 0; dragEndTick = 0;
        setMouseCursor(juce::MouseCursor::CrosshairCursor); repaint(); owner.repaint();
    }

private:
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
        if (position.y < rulerHeight || position.y >= getHeight() - (dualLanes ? dualLaneTotalHeight : velocityLaneHeight) || position.x < pianoKeyWidth) return ResizeSide::none;
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
    int pitchFromY(int y) const noexcept { const int keyHeight = currentKeyHeight(); const int row = juce::jlimit(0, visibleKeys - 1, (y - rulerHeight) / juce::jmax(1, keyHeight)); return lowestVisibleKey + visibleKeys - 1 - row; }
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
