#include "MainComponent.h"
#include "MidiEngine.h"
#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
constexpr int rulerHeight = 30;
constexpr int pianoKeyWidth = 72;
constexpr int velocityLaneHeight = 92;

class VelocityMouseListener final : public juce::MouseListener,
                                    private juce::Timer
{
public:
    VelocityMouseListener()
    {
        juce::Desktop::getInstance().addGlobalMouseListener(this);
    }

    ~VelocityMouseListener() override
    {
        stopTimer();
        juce::Desktop::getInstance().removeGlobalMouseListener(this);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown())
            return;

        auto* piano = findPianoRoll(e.getScreenPosition());
        if (piano == nullptr)
            return;

        auto* main = findMainComponent(piano);
        if (main == nullptr)
            return;

        const auto p = piano->getLocalPoint(nullptr, e.getScreenPosition());
        if (!inVelocityLane(*piano, p.y) || p.x < pianoKeyWidth)
            return;

        if (!selectNearestNote(*main, *piano, p.x))
            return;

        updateVelocity(*main, *piano, p.y);

        // Do not depend on JUCE delivering every mouseDrag callback.
        // Poll the real mouse position while the button is physically held.
        startTimerHz(60);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        editing = false;
        pianoRoll = nullptr;
        stopTimer();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!editing || pianoRoll == nullptr)
            return;

        if (auto* main = findMainComponent(pianoRoll))
        {
            const auto p = pianoRoll->getLocalPoint(nullptr, e.getScreenPosition());
            updateVelocity(*main, *pianoRoll, p.y);
        }
    }

private:
    static juce::Component* findPianoRoll(juce::Point<int> screenPosition) noexcept
    {
        auto* component = juce::Desktop::getInstance().findComponentAt(screenPosition);

        while (component != nullptr)
        {
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(component))
            {
                if (window->getName() == "Liberty - MIDI 1")
                    return window->getContentComponent();
            }
            component = component->getParentComponent();
        }

        return nullptr;
    }

    static MainComponent* findMainComponent(juce::Component* component) noexcept
    {
        while (component != nullptr)
        {
            if (auto* main = dynamic_cast<MainComponent*>(component))
                return main;
            component = component->getParentComponent();
        }

        // The MIDI editor is a separate DocumentWindow, so walk desktop
        // windows when the content component has no MainComponent parent.
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
        {
            if (auto* main = findMainInTree(desktop.getComponent(i)))
                return main;
        }

        return nullptr;
    }

    static MainComponent* findMainInTree(juce::Component* component) noexcept
    {
        if (component == nullptr)
            return nullptr;

        if (auto* main = dynamic_cast<MainComponent*>(component))
            return main;

        for (int i = 0; i < component->getNumChildComponents(); ++i)
            if (auto* main = findMainInTree(component->getChildComponent(i)))
                return main;

        return nullptr;
    }

    static bool inVelocityLane(const juce::Component& piano, int y) noexcept
    {
        const auto laneTop = juce::jmax(rulerHeight, piano.getHeight() - velocityLaneHeight);
        return y >= laneTop && y < piano.getHeight();
    }

    static double pixelsPerTick(const MainComponent& main,
                                const juce::Component& piano)
    {
        const auto clipTicks = juce::jmax<std::int64_t>(
            juce::jmax<std::int64_t>(1, MidiEngine::ticksPerMeasure(
                main.getTimeSignatureNumerator(),
                main.getTimeSignatureDenominator())),
            MidiEngine::secondsToTick(main.getMidiClipLengthSeconds(),
                                      main.getTempoBpm()));

        return static_cast<double>(juce::jmax(1, piano.getWidth() - pianoKeyWidth) - 2)
             / static_cast<double>(clipTicks);
    }

    static bool selectNearestNote(MainComponent& main,
                                  const juce::Component& piano,
                                  int x)
    {
        const auto ppt = pixelsPerTick(main, piano);
        if (ppt <= 0.0)
            return false;

        const auto clipTicks = juce::jmax<std::int64_t>(
            1, MidiEngine::secondsToTick(main.getMidiClipLengthSeconds(),
                                         main.getTempoBpm()));

        const MidiEngine::NoteEvent* best = nullptr;
        double bestDistance = std::numeric_limits<double>::max();

        for (const auto& note : main.getMidiEngine().getNotesCopy())
        {
            if (note.startTick < 0 || note.startTick >= clipTicks)
                continue;

            const double noteX = static_cast<double>(pianoKeyWidth)
                               + static_cast<double>(note.startTick) * ppt;
            const double distance = std::abs(static_cast<double>(x) - noteX);

            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = &note;
            }
        }

        if (best == nullptr)
            return false;

        velocityStartTick = best->startTick;
        velocityPitch = best->pitch;
        velocityChannel = best->channel;
        editing = true;
        pianoRoll = const_cast<juce::Component*>(&piano);
        return true;
    }

    static void updateVelocity(MainComponent& main,
                               const juce::Component& piano,
                               int mouseY)
    {
        const auto laneTop = juce::jmax(rulerHeight,
                                        piano.getHeight() - velocityLaneHeight);
        const auto laneBottom = piano.getHeight() - 8;
        const auto usableHeight = juce::jmax(1, laneBottom - laneTop - 16);
        const auto clampedY = juce::jlimit(laneTop + 8, laneBottom, mouseY);

        const auto velocity = juce::jlimit(
            1,
            127,
            static_cast<int>(std::llround(
                static_cast<double>(laneBottom - clampedY)
                / static_cast<double>(usableHeight) * 127.0)));

        if (main.getMidiEngine().setNoteVelocity(
                velocityStartTick,
                velocityPitch,
                velocityChannel,
                velocity))
        {
            main.updateMidiClipTiming();
            if (auto* mutablePiano = const_cast<juce::Component*>(&piano))
                mutablePiano->repaint();
            main.repaint();
        }
    }

    void timerCallback() override
    {
        if (!editing || pianoRoll == nullptr)
        {
            stopTimer();
            return;
        }

        auto& desktop = juce::Desktop::getInstance();
        if (desktop.getNumDraggingMouseSources() <= 0)
        {
            editing = false;
            pianoRoll = nullptr;
            stopTimer();
            return;
        }

        if (auto* main = findMainComponent(pianoRoll))
        {
            const auto screenPosition = desktop.getMainMouseSource().getScreenPosition();
            const auto p = pianoRoll->getLocalPoint(nullptr, screenPosition);
            updateVelocity(*main, *pianoRoll, p.y);
        }
    }

    inline static std::int64_t velocityStartTick = 0;
    inline static int velocityPitch = 60;
    inline static int velocityChannel = 1;
    inline static bool editing = false;
    inline static juce::Component* pianoRoll = nullptr;
};

VelocityMouseListener velocityMouseListener;
}
