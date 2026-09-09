#include "MainComponent.h"
#include "MidiEngine.h"
#include <cmath>
#include <cstdint>

namespace
{
constexpr int rulerHeight = 30;
constexpr int pianoKeyWidth = 72;
constexpr int velocityLaneHeight = 92;
constexpr int velocityHitTolerance = 16;
constexpr std::int64_t gridTicks = MidiEngine::ticksPerQuarterNote / 4;

class VelocityMouseListener final : public juce::MouseListener
{
public:
    VelocityMouseListener() { juce::Desktop::getInstance().addGlobalMouseListener(this); }
    ~VelocityMouseListener() override { juce::Desktop::getInstance().removeGlobalMouseListener(this); }

    void mouseDown(const juce::MouseEvent& e) override
    {
        auto* piano = getPianoRoll(e);
        auto* main = findMainComponent();
        if (piano == nullptr || main == nullptr) return;
        const auto p = piano->getLocalPoint(nullptr, e.getScreenPosition());
        if (p.x < pianoKeyWidth || !inVelocityLane(*piano, p.y) || e.mods.isRightButtonDown()) return;
        if (selectNote(*main, *piano, p.x)) updateVelocity(*main, *piano, p.y);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!editing) return;
        auto* piano = getPianoRoll(e);
        auto* main = findMainComponent();
        if (piano == nullptr || main == nullptr) return;
        const auto p = piano->getLocalPoint(nullptr, e.getScreenPosition());
        updateVelocity(*main, *piano, p.y);
    }

    void mouseUp(const juce::MouseEvent&) override { editing = false; }

private:
    static juce::Component* getPianoRoll(const juce::MouseEvent& e) noexcept
    {
        auto* top = e.eventComponent != nullptr ? e.eventComponent->getTopLevelComponent() : nullptr;
        auto* window = dynamic_cast<juce::DocumentWindow*>(top);
        if (window == nullptr || window->getName() != "Liberty - MIDI 1") return nullptr;
        return window->getContentComponent();
    }

    static MainComponent* findMainComponent() noexcept
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* main = dynamic_cast<MainComponent*>(desktop.getComponent(i))) return main;
        return nullptr;
    }

    static bool inVelocityLane(const juce::Component& piano, int y) noexcept
    {
        return y >= juce::jmax(rulerHeight, piano.getHeight() - velocityLaneHeight) && y < piano.getHeight();
    }

    static double pixelsPerTick(const MainComponent& main, const juce::Component& piano)
    {
        const auto clipTicks = juce::jmax<std::int64_t>(
            juce::jmax<std::int64_t>(1, MidiEngine::ticksPerMeasure(main.getTimeSignatureNumerator(), main.getTimeSignatureDenominator())),
            MidiEngine::secondsToTick(main.getMidiClipLengthSeconds(), main.getTempoBpm()));
        return static_cast<double>(juce::jmax(1, piano.getWidth() - pianoKeyWidth) - 2) / static_cast<double>(clipTicks);
    }

    static bool selectNote(MainComponent& main, const juce::Component& piano, int x)
    {
        const auto ppt = pixelsPerTick(main, piano);
        const auto clipTicks = MidiEngine::secondsToTick(main.getMidiClipLengthSeconds(), main.getTempoBpm());
        const auto clickTick = MidiEngine::quantizeTick(
            juce::jmax<std::int64_t>(0, static_cast<std::int64_t>(std::llround((x - pianoKeyWidth) / ppt))), gridTicks);
        const MidiEngine::NoteEvent* best = nullptr;
        double bestDistance = static_cast<double>(velocityHitTolerance + 1);

        for (const auto& n : main.getMidiEngine().getNotesCopy())
        {
            if (n.startTick >= clipTicks) continue;
            const auto barX = static_cast<double>(pianoKeyWidth) + static_cast<double>(n.startTick) * ppt;
            const auto barWidth = static_cast<double>(juce::jmax(4, juce::jmin(18, static_cast<int>(std::llround(static_cast<double>(n.lengthTicks) * ppt)))));
            const auto distance = std::abs(static_cast<double>(x) - (barX + barWidth * 0.5));
            const auto tickDistance = std::llabs(clickTick - n.startTick);
            if (distance <= velocityHitTolerance && tickDistance <= MidiEngine::ticksPerQuarterNote && distance < bestDistance)
            {
                bestDistance = distance;
                best = &n;
            }
        }

        if (best == nullptr) return false;
        velocityStartTick = best->startTick;
        velocityPitch = best->pitch;
        velocityChannel = best->channel;
        editing = true;
        return true;
    }

    static void updateVelocity(MainComponent& main, const juce::Component& piano, int y)
    {
        const auto laneTop = juce::jmax(rulerHeight, piano.getHeight() - velocityLaneHeight);
        const auto laneBottom = piano.getHeight() - 8;
        const auto usable = juce::jmax(1, laneBottom - laneTop - 16);
        const auto clampedY = juce::jlimit(laneTop + 8, laneBottom, y);
        const auto velocity = juce::jlimit(1, 127, static_cast<int>(std::llround(
            static_cast<double>(laneBottom - clampedY) / static_cast<double>(usable) * 127.0)));
        if (main.getMidiEngine().setNoteVelocity(velocityStartTick, velocityPitch, velocityChannel, velocity))
        {
            main.updateMidiClipTiming();
            main.repaint();
        }
    }

    inline static std::int64_t velocityStartTick = 0;
    inline static int velocityPitch = 60;
    inline static int velocityChannel = 1;
    inline static bool editing = false;
};

VelocityMouseListener velocityMouseListener;
}
