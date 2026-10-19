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
std::atomic<int> trackHeightPreset { 1 }; // 0 compact, 1 normal, 2 large
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
        const int track = owner->getArrangeTrackAtY(p.y);
        const int boundary = track >= 0 ? owner->getArrangeTrackTop(track) + owner->getArrangeTrackHeight(track) : -1000;
        if (p.x >= 202 && p.x <= 218 && p.y >= 76 && p.y < owner->getMixerTop())
            owner->setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        else if (p.x >= 0 && p.x < 202 && track >= 0
                 && std::abs(p.y - boundary) <= 7)
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
        const int track = owner->getArrangeTrackAtY(p.y);
        const int boundary = track >= 0 ? owner->getArrangeTrackTop(track) + owner->getArrangeTrackHeight(track) : -1000;
        if (p.x >= 0 && p.x < 202 && track >= 0 && std::abs(p.y - boundary) <= 7)
        {
            dragMode = 1;
            draggedTrack = track;
            dragStart = p.y;
            initialTrackHeight = owner->getArrangeTrackHeight(track);
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
        if (dragMode == 1 && draggedTrack >= 0)
            owner->setArrangeTrackHeight(draggedTrack, initialTrackHeight + p.y - dragStart);
        else
            timelineHorizontalZoom.store(juce::jlimit(0.08f, 5.0f, initialZoom + (p.x - dragStart) / 160.0f));
        repaintRelevantWindows();
    }
    void mouseUp(const juce::MouseEvent&) override { dragMode = 0; draggedTrack = -1; }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        if (isInsideDrumSampler(event.eventComponent)
            || isInsideScrollableBrowserContent(event.eventComponent))
            return;

        const float delta = std::abs(wheel.deltaY) >= std::abs(wheel.deltaX) ? wheel.deltaY : wheel.deltaX;
        if (std::abs(delta) < 0.0001f)
            return;

        // Plain wheel over the track headers scrolls track rows only.
        // Leave timeline wheel zoom/scroll and modifier shortcuts unchanged.
        if (!isInsideMidiEditor(event.eventComponent) && !event.mods.isCtrlDown()
            && !event.mods.isCommandDown() && !event.mods.isAltDown()
            && !event.mods.isShiftDown())
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
                const auto p = event.getEventRelativeTo(owner).getPosition();
                if (p.x >= 0 && p.x < MainComponent::trackHeaderWidth
                    && p.y >= owner->getArrangeTop() && p.y < owner->getMixerTop())
                {
                    const int rowH = juce::jmax(1, getLibertyTrackRowHeight());
                    const int visible = juce::jmax(1, owner->getArrangeRowsBounds().getHeight() / rowH);
                    const int maxStart = juce::jmax(0, owner->getTotalArrangeTrackCount() - visible);
                    const int step = juce::jmax(1, (int) std::lround(std::abs(wheel.deltaY) * 3.0f));
                    const int next = owner->getTrackScrollRows() + (wheel.deltaY > 0.0f ? -step : step);
                    owner->setTrackScrollRows(juce::jlimit(0, maxStart, next));
                    return;
                }
            }
        }

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
                timelineVerticalZoom.store(applyWheelZoom(timelineVerticalZoom.load(), delta, 0.58f, 2.5f));
            else
                timelineHorizontalZoom.store(applyWheelZoom(timelineHorizontalZoom.load(), delta, 0.08f, 5.0f));
        }
        repaintRelevantWindows();
    }

private:
    bool registered = true;
    int dragMode = 0, dragStart = 0;
    int draggedTrack = -1, initialTrackHeight = 0;
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
    return juce::jlimit(144, 240, (int) std::lround(144.0f * timelineVerticalZoom.load(std::memory_order_relaxed)));
}

int getLibertyTrackHeightPreset() noexcept { return trackHeightPreset.load(); }

void cycleLibertyTrackHeightPreset()
{
    const int next = (trackHeightPreset.load() + 1) % 3;
    trackHeightPreset.store(next);
    constexpr float zoom[] = { 0.625f, 1.0f, 1.75f };
    timelineVerticalZoom.store(zoom[next]);
    repaintRelevantWindows();
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
