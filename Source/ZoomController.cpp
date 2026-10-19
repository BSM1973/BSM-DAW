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

// Floating instrument windows own their wheel interactions. In particular,
// Drum Sampler waveform zoom must never modify the arrange timeline zoom.
bool isInsideDrumSampler(juce::Component* component)
{
    while (component != nullptr)
    {
        if (auto* window = dynamic_cast<juce::DocumentWindow*>(component))
            return window->getName().startsWithIgnoreCase("Liberty - Drum Sampler");
        component = component->getParentComponent();
    }
    return false;
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

    void mouseMove(const juce::MouseEvent& event) override
    {
        auto* component = event.eventComponent;
        MainComponent* owner = nullptr;
        while (component != nullptr)
        {
            if ((owner = dynamic_cast<MainComponent*>(component)) != nullptr) break;
            component = component->getParentComponent();
        }
        if (owner == nullptr) return;
        const auto p = event.getEventRelativeTo(owner).getPosition();
        const int rh = getLibertyTrackRowHeight();
        if (p.x >= 202 && p.x <= 218 && p.y >= 76 && p.y < owner->getMixerTop())
            owner->setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        else if (p.x >= 0 && p.x < 202 && p.y >= 108 && p.y < owner->getMixerTop()
                 && ((p.y - 108) % juce::jmax(1,rh)) >= rh - 6)
            owner->setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        else
            owner->setMouseCursor(juce::MouseCursor::NormalCursor);
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        auto* component = event.eventComponent;
        MainComponent* owner = nullptr;
        while (component != nullptr)
        {
            if ((owner = dynamic_cast<MainComponent*>(component)) != nullptr) break;
            component = component->getParentComponent();
        }
        if (owner == nullptr || event.mods.isPopupMenu()) return;
        const auto p = event.getEventRelativeTo(owner).getPosition();
        const int rowH = getLibertyTrackRowHeight();
        if (p.x >= 0 && p.x < 210 && p.y >= 108 && p.y < owner->getMixerTop()
            && ((p.y - 108) % juce::jmax(1,rowH)) >= rowH - 6)
        {
            dragMode = 1; dragStart = p.y; initialZoom = timelineVerticalZoom.load();
        }
        else if (p.x >= 202 && p.x <= 218 && p.y >= 76 && p.y < owner->getMixerTop())
        {
            dragMode = 2; dragStart = p.x; initialZoom = timelineHorizontalZoom.load();
        }
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (dragMode == 0) return;
        auto* component = event.eventComponent;
        MainComponent* owner = nullptr;
        while (component != nullptr)
        {
            if ((owner = dynamic_cast<MainComponent*>(component)) != nullptr) break;
            component = component->getParentComponent();
        }
        if (owner == nullptr) return;
        const auto p = event.getEventRelativeTo(owner).getPosition();
        if (dragMode == 1)
            timelineVerticalZoom.store(juce::jlimit(1.0f, 2.5f, initialZoom + (p.y - dragStart) / 96.0f));
        else
            timelineHorizontalZoom.store(juce::jlimit(0.08f, 5.0f, initialZoom + (p.x - dragStart) / 160.0f));
        repaintRelevantWindows();
    }
    void mouseUp(const juce::MouseEvent&) override { dragMode = 0; }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        if (isInsideDrumSampler(event.eventComponent)
            || isInsideScrollableBrowserContent(event.eventComponent))
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
                timelineVerticalZoom.store(applyWheelZoom(timelineVerticalZoom.load(), delta, 1.0f, 2.5f));
            else
                timelineHorizontalZoom.store(applyWheelZoom(timelineHorizontalZoom.load(), delta, 0.08f, 5.0f));
        }
        repaintRelevantWindows();
    }

private:
    bool registered = true;
    int dragMode = 0, dragStart = 0;
    float initialZoom = 1.0f;
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
    return juce::jlimit(96, 240, (int) std::lround(96.0f * timelineVerticalZoom.load(std::memory_order_relaxed)));
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
