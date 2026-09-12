#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <cmath>

namespace
{
std::atomic<float> timelineHorizontalZoom { 1.0f };
std::atomic<float> timelineVerticalZoom { 1.0f };
std::atomic<float> pianoHorizontalZoom { 1.0f };
std::atomic<float> pianoVerticalZoom { 1.0f };

float applyWheelZoom(float current, float delta, float minimum, float maximum)
{
    if (std::abs(delta) < 0.0001f)
        return current;
    const float factor = delta > 0.0f ? 1.12f : (1.0f / 1.12f);
    return juce::jlimit(minimum, maximum, current * factor);
}

bool isInsideMidiEditor(juce::Component* component)
{
    while (component != nullptr)
    {
        if (auto* window = dynamic_cast<juce::DocumentWindow*>(component))
            return window->getName().startsWithIgnoreCase("Liberty - MIDI");
        component = component->getParentComponent();
    }
    return false;
}

void repaintRelevantWindows()
{
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* component = desktop.getComponent(i))
            component->repaint();
}

class ZoomMouseListener final : public juce::MouseListener
{
public:
    ZoomMouseListener() { juce::Desktop::getInstance().addGlobalMouseListener(this); }
    ~ZoomMouseListener() override { shutdown(); }

    void shutdown()
    {
        if (!registered)
            return;
        juce::Desktop::getInstance().removeGlobalMouseListener(this);
        registered = false;
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        const float delta = std::abs(wheel.deltaY) >= std::abs(wheel.deltaX) ? wheel.deltaY : wheel.deltaX;
        if (std::abs(delta) < 0.0001f)
            return;

        const bool vertical = event.mods.isCommandDown();
        if (isInsideMidiEditor(event.eventComponent))
        {
            if (vertical)
                pianoVerticalZoom.store(applyWheelZoom(pianoVerticalZoom.load(), delta, 0.55f, 2.5f));
            else
                pianoHorizontalZoom.store(applyWheelZoom(pianoHorizontalZoom.load(), delta, 0.40f, 5.0f));
        }
        else
        {
            if (vertical)
                timelineVerticalZoom.store(applyWheelZoom(timelineVerticalZoom.load(), delta, 0.85f, 1.17f));
            else
                timelineHorizontalZoom.store(applyWheelZoom(timelineHorizontalZoom.load(), delta, 0.40f, 5.0f));
        }
        repaintRelevantWindows();
    }

private:
    bool registered = true;
};

ZoomMouseListener zoomMouseListener;
}

double getLibertyTimelinePixelsPerSecond() noexcept
{
    return 80.0 * (double) timelineHorizontalZoom.load(std::memory_order_relaxed);
}

float getLibertyTimelineVerticalZoom() noexcept
{
    return timelineVerticalZoom.load(std::memory_order_relaxed);
}

int getLibertyTrackRowHeight() noexcept
{
    return juce::jlimit(60, 82, (int) std::lround(70.0f * timelineVerticalZoom.load(std::memory_order_relaxed)));
}

float getLibertyPianoHorizontalZoom() noexcept
{
    return pianoHorizontalZoom.load(std::memory_order_relaxed);
}

float getLibertyPianoVerticalZoom() noexcept
{
    return pianoVerticalZoom.load(std::memory_order_relaxed);
}

void shutdownLibertyZoomController()
{
    zoomMouseListener.shutdown();
}
