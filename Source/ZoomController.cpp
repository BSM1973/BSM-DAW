#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <cmath>
#define private public
#include "MainComponent.h"
#undef private

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

bool isInsideScrollableBrowserContent(juce::Component* component)
{
    while (component != nullptr)
    {
        if (dynamic_cast<juce::ListBox*>(component) != nullptr
            || dynamic_cast<juce::TreeView*>(component) != nullptr)
            return true;

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
        if (isInsideScrollableBrowserContent(event.eventComponent))
            return;

        const float delta = std::abs(wheel.deltaY) >= std::abs(wheel.deltaX) ? wheel.deltaY : wheel.deltaX;
        if (std::abs(delta) < 0.0001f)
            return;

        if (event.mods.isCtrlDown() && !isInsideMidiEditor(event.eventComponent))
        {
            auto* component = event.eventComponent;
            MainComponent* owner = nullptr;
            while (component != nullptr)
            {
                if ((owner = dynamic_cast<MainComponent*>(component)) != nullptr) break;
                component = component->getParentComponent();
            }
            if (owner != nullptr)
            {
                const int rowH = getLibertyTrackRowHeight();
                const int available = juce::jmax(1, owner->getArrangeRowsBounds().getHeight());
                const int visible = juce::jmax(1, (available + rowH - 1) / rowH);
                const int maxStart = juce::jmax(0, owner->getTotalArrangeTrackCount() - visible);
                owner->setTrackScrollRows(juce::jlimit(0, maxStart, owner->getTrackScrollRows() + (delta > 0.0f ? -1 : 1)));
            }
            return;
        }

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
                timelineVerticalZoom.store(applyWheelZoom(timelineVerticalZoom.load(), delta, 1.0f, 1.17f));
            else
                timelineHorizontalZoom.store(applyWheelZoom(timelineHorizontalZoom.load(), delta, 0.08f, 5.0f));
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
    // Hard minimum: title/recording row + insert row + mix row, all separated.
    // No UI element is allowed to share the same vertical pixels.
    return juce::jlimit(96, 112, (int) std::lround(96.0f * timelineVerticalZoom.load(std::memory_order_relaxed)));
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
