#define private public
#include "MainComponent.h"
#undef private

#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>

double getLibertyTimelinePixelsPerSecond() noexcept;
int getLibertyTrackRowHeight() noexcept;

namespace
{
constexpr int headerWidth = 210;
constexpr int transportHeight = 76;
constexpr int rulerHeight = 32;
constexpr int totalTrackRows = AudioEngine::maxAudioTracks + 2;

std::atomic<int> snapIndex { 3 };

juce::String snapLabel(int index)
{
    switch (index)
    {
        case 0: return "OFF";
        case 1: return "1/4";
        case 2: return "1/8";
        case 3: return "1/16";
        case 4: return "1/32";
        case 5: return "1/8T";
        case 6: return "1/16T";
        default: return "1/16";
    }
}

double snapSecondsFor(const MainComponent& owner)
{
    const auto index = snapIndex.load(std::memory_order_relaxed);
    if (index == 0) return 0.0;
    const double quarter = 60.0 / juce::jmax(1.0, owner.tempoBpm);
    switch (index)
    {
        case 1: return quarter;
        case 2: return quarter * 0.5;
        case 3: return quarter * 0.25;
        case 4: return quarter * 0.125;
        case 5: return quarter / 3.0;
        case 6: return quarter / 6.0;
        default: return quarter * 0.25;
    }
}

double snapTime(const MainComponent& owner, double seconds)
{
    seconds = juce::jmax(0.0, seconds);
    const auto step = snapSecondsFor(owner);
    return step > 0.0 ? std::round(seconds / step) * step : seconds;
}

class GridSnapController final : public juce::Component, private juce::Timer
{
public:
    explicit GridSnapController(MainComponent& ownerIn) : owner(ownerIn)
    {
        setInterceptsMouseClicks(true, false);
        owner.addAndMakeVisible(this);
        startTimerHz(5);
    }

    ~GridSnapController() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        setVisible(false);
    }

    bool hitTest(int x, int y) override
    {
        if (snapButton().contains(x, y)) return true;
        if (snapIndex.load(std::memory_order_relaxed) == 0) return false;
        for (int track = 0; track < AudioEngine::maxAudioTracks; ++track)
            if (audioClipBounds(track).contains(x, y)) return true;
        return false;
    }

    void paint(juce::Graphics& g) override
    {
        drawGrid(g);
        drawSnapButton(g);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (snapButton().contains(e.getPosition()))
        {
            showSnapMenu();
            return;
        }

        const int track = audioTrackAt(e.getPosition());
        if (track >= 0 && audioClipBounds(track).contains(e.getPosition()))
        {
            owner.selectedTrack = track;
            draggingAudio = true;
            draggedTrack = track;
            dragMouseX = e.position.x;
            dragStartSeconds = owner.audioEngine.getTrackStartSeconds(track);
            owner.repaint();
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!draggingAudio || draggedTrack < 0) return;
        const double pixelsPerSecond = getLibertyTimelinePixelsPerSecond();
        const double delta = ((double)e.position.x - (double)dragMouseX) / pixelsPerSecond;
        owner.audioEngine.setTrackStartSeconds(draggedTrack, snapTime(owner, dragStartSeconds + delta));
        repaint();
        owner.repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        draggingAudio = false;
        draggedTrack = -1;
    }

private:
    juce::Rectangle<int> snapButton() const { return { 1055, 10, 128, 24 }; }

    juce::Rectangle<int> audioClipBounds(int track) const
    {
        if (track < 0 || track >= AudioEngine::maxAudioTracks || !owner.audioEngine.hasAudioFile(track)) return {};
        const int rowHeight = getLibertyTrackRowHeight();
        const double pixelsPerSecond = getLibertyTimelinePixelsPerSecond();
        const int y = transportHeight + rulerHeight + track * rowHeight + 4;
        const int x = headerWidth + (int)std::round(owner.audioEngine.getTrackStartSeconds(track) * pixelsPerSecond);
        const int w = juce::jmax(1, (int)std::round(owner.audioEngine.getAudioFileLengthSeconds(track) * pixelsPerSecond));
        return { x, y, w, rowHeight - 8 };
    }

    int audioTrackAt(juce::Point<int> p) const
    {
        const int rowHeight = getLibertyTrackRowHeight();
        const int relativeY = p.y - transportHeight - rulerHeight;
        if (relativeY < 0) return -1;
        const int track = relativeY / rowHeight;
        return track >= 0 && track < AudioEngine::maxAudioTracks ? track : -1;
    }

    void drawGrid(juce::Graphics& g)
    {
        const int rowHeight = getLibertyTrackRowHeight();
        const double pixelsPerSecond = getLibertyTimelinePixelsPerSecond();
        const int top = transportHeight + rulerHeight;
        const int bottom = juce::jmin(owner.getHeight() - 210, top + totalTrackRows * rowHeight);
        if (bottom <= top) return;

        for (int row = 0; row <= totalTrackRows; ++row)
        {
            const int y = top + row * rowHeight;
            g.setColour(juce::Colour(0xff343a43).withAlpha(0.78f));
            g.drawHorizontalLine(y, (float)headerWidth, (float)owner.getWidth());
            if (row < totalTrackRows)
            {
                g.setColour(juce::Colour(0xff2a3038).withAlpha(0.55f));
                g.drawHorizontalLine(y + rowHeight / 2, (float)headerWidth, (float)owner.getWidth());
            }
        }

        auto step = snapSecondsFor(owner);
        if (step <= 0.0) step = 60.0 / juce::jmax(1.0, owner.tempoBpm);

        const double measureSeconds = (60.0 / juce::jmax(1.0, owner.tempoBpm))
            * (4.0 / (double)juce::jmax(1, owner.timeSignatureDenominator))
            * (double)juce::jmax(1, owner.timeSignatureNumerator);

        for (int n = 0; n < 2000; ++n)
        {
            const double seconds = n * step;
            const int x = headerWidth + (int)std::round(seconds * pixelsPerSecond);
            if (x > owner.getWidth()) break;
            const bool major = measureSeconds > 0.0 && std::abs(std::fmod(seconds, measureSeconds)) < step * 0.15;
            g.setColour(major ? juce::Colour(0xff59616d).withAlpha(0.90f)
                              : juce::Colour(0xff343b45).withAlpha(0.62f));
            g.drawVerticalLine(x, (float)top, (float)bottom);
        }
    }

    void drawSnapButton(juce::Graphics& g)
    {
        const auto r = snapButton();
        const bool enabled = snapIndex.load(std::memory_order_relaxed) != 0;
        g.setColour(enabled ? juce::Colour(0xff31506a) : juce::Colour(0xff252a31));
        g.fillRoundedRectangle(r.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff59616d));
        g.drawRoundedRectangle(r.toFloat(), 5.0f, 1.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText("SNAP  " + snapLabel(snapIndex.load(std::memory_order_relaxed)), r, juce::Justification::centred);
    }

    void showSnapMenu()
    {
        juce::PopupMenu menu;
        const int current = snapIndex.load(std::memory_order_relaxed);
        const juce::StringArray labels { "OFF", "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" };
        for (int i = 0; i < labels.size(); ++i) menu.addItem(i + 1, labels[i], true, current == i);

        const auto target = owner.localAreaToGlobal(snapButton());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(target), [this](int result)
        {
            if (result <= 0) return;
            snapIndex.store(result - 1, std::memory_order_relaxed);
            repaint();
            owner.repaint();
        });
    }

    void timerCallback() override
    {
        if (stopped.load()) return;
        const auto bounds = owner.getLocalBounds();
        if (getBounds() != bounds) setBounds(bounds);
        repaint();
    }

    MainComponent& owner;
    std::atomic<bool> stopped { false };
    bool draggingAudio = false;
    int draggedTrack = -1;
    float dragMouseX = 0.0f;
    double dragStartSeconds = 0.0;
};

std::map<MainComponent*, std::unique_ptr<GridSnapController>> controllers;

class Bootstrap final : private juce::Timer
{
public:
    Bootstrap() { startTimerHz(10); }
    ~Bootstrap() override { shutdown(); }
    void shutdown()
    {
        stopTimer();
        for (auto& pair : controllers) if (pair.second) pair.second->shutdown();
        controllers.clear();
    }
private:
    void timerCallback() override
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i)))
                if (auto* main = dynamic_cast<MainComponent*>(window->getContentComponent()))
                    if (controllers.find(main) == controllers.end())
                        controllers.emplace(main, std::make_unique<GridSnapController>(*main));
    }
};

Bootstrap bootstrap;
}

bool isLibertySnapEnabled() noexcept
{
    return snapIndex.load(std::memory_order_relaxed) != 0;
}

double getLibertySnapSeconds(double tempoBpm) noexcept
{
    const auto index = snapIndex.load(std::memory_order_relaxed);
    if (index == 0) return 0.0;
    const double quarter = 60.0 / juce::jmax(1.0, tempoBpm);
    switch (index)
    {
        case 1: return quarter;
        case 2: return quarter * 0.5;
        case 3: return quarter * 0.25;
        case 4: return quarter * 0.125;
        case 5: return quarter / 3.0;
        case 6: return quarter / 6.0;
        default: return quarter * 0.25;
    }
}

void shutdownLibertyGridSnapController()
{
    bootstrap.shutdown();
}
