#define private public
#include "MainComponent.h"
#undef private

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>

bool commitLibertyAudioClipResize(MainComponent& owner,
                                  int trackIndex,
                                  double requestedStartSeconds,
                                  double requestedLengthSeconds,
                                  bool preservePitchStretch,
                                  bool resizeLeft,
                                  juce::String& error);

double getLibertyTimelinePixelsPerSecond() noexcept;
int getLibertyTrackRowHeight() noexcept;
bool isLibertySnapEnabled() noexcept;
double getLibertySnapSeconds(double tempoBpm) noexcept;

namespace
{
constexpr int headerWidth = 210;
constexpr int rulerHeight = 32;
constexpr int handleWidth = 12;

class AudioClipResizeController final : public juce::Component,
                                        private juce::Timer
{
public:
    enum class Side { left, right };

    class Handle final : public juce::Component
    {
    public:
        Handle(AudioClipResizeController& c, int t, Side s)
            : controller(c), track(t), side(s)
        {
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            setInterceptsMouseClicks(true, false);
        }

        void paint(juce::Graphics& g) override
        {
            const bool active = controller.dragTrack == track && controller.dragSide == side;
            g.setColour(active ? juce::Colour(0xffffc857) : juce::Colour(0xff78d8f5));
            const float x = (float)getWidth() * 0.5f - 1.5f;
            g.fillRoundedRectangle(x, 4.0f, 3.0f, (float)juce::jmax(1, getHeight() - 8), 1.5f);
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            controller.beginResize(track, side, e.mods.isCommandDown());
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            const auto relative = e.getEventRelativeTo(&controller);
            controller.updateResize(relative.position.x);
        }

        void mouseUp(const juce::MouseEvent&) override
        {
            controller.finishResize();
        }

    private:
        AudioClipResizeController& controller;
        int track = -1;
        Side side = Side::right;
    };

    explicit AudioClipResizeController(MainComponent& o) : owner(o)
    {
        setInterceptsMouseClicks(false, true);
        owner.addAndMakeVisible(this);

        for (int t = 0; t < AudioEngine::maxAudioTracks; ++t)
        {
            leftHandles[(size_t)t] = std::make_unique<Handle>(*this, t, Side::left);
            rightHandles[(size_t)t] = std::make_unique<Handle>(*this, t, Side::right);
            addAndMakeVisible(*leftHandles[(size_t)t]);
            addAndMakeVisible(*rightHandles[(size_t)t]);
        }
        startTimerHz(30);
    }

    ~AudioClipResizeController() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        setVisible(false);
    }

    void paint(juce::Graphics& g) override
    {
        if (dragTrack < 0 || previewLength <= 0.0) return;

        const double pps = getLibertyTimelinePixelsPerSecond();
        const int rowH = getLibertyTrackRowHeight();
        const int x = headerWidth + (int)std::llround(previewStart * pps);
        const int y = 76 + rulerHeight + dragTrack * rowH + 4;
        const int w = juce::jmax(2, (int)std::llround(previewLength * pps));
        const auto r = juce::Rectangle<int>(x, y, w, rowH - 8);

        g.setColour(stretchMode ? juce::Colour(0x5539d98a) : juce::Colour(0x5545a6df));
        g.fillRoundedRectangle(r.toFloat(), 5.0f);
        g.setColour(stretchMode ? juce::Colour(0xff55e6a5) : juce::Colour(0xff78d8f5));
        g.drawRoundedRectangle(r.toFloat(), 5.0f, 2.0f);

        const double secondsPerBeat = 60.0 / juce::jmax(1.0, owner.tempoBpm)
            * (4.0 / (double)juce::jmax(1, owner.timeSignatureDenominator));
        const double secondsPerMeasure = secondsPerBeat * (double)juce::jmax(1, owner.timeSignatureNumerator);
        const double measures = previewLength / juce::jmax(0.000001, secondsPerMeasure);
        const juce::String label = stretchMode
            ? "CMD TIME STRETCH  " + juce::String(measures, 2) + " mesures  •  hauteur conservée"
            : "RESIZE  " + juce::String(measures, 2) + " mesures";

        auto labelBounds = juce::Rectangle<int>(juce::jmax(headerWidth, r.getX()), juce::jmax(76, r.getY() - 22),
                                                juce::jmin(330, juce::jmax(150, getWidth() - juce::jmax(headerWidth, r.getX()) - 8)), 20);
        g.setColour(juce::Colour(0xdd11161c));
        g.fillRoundedRectangle(labelBounds.toFloat(), 4.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText(label, labelBounds.reduced(6, 0), juce::Justification::centredLeft, true);
    }

private:
    void beginResize(int track, Side side, bool commandDown)
    {
        if (stopped.load() || !owner.audioEngine.hasAudioFile(track)) return;
        owner.selectedTrack = track;
        owner.draggingClip = false;
        owner.draggedTrack = -1;
        dragTrack = track;
        dragSide = side;
        stretchMode = commandDown;
        dragStartMouseX = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().x;
        dragStart = owner.audioEngine.getTrackStartSeconds(track);
        dragLength = owner.audioEngine.getAudioFileLengthSeconds(track);
        previewStart = dragStart;
        previewLength = dragLength;
        repaint();
        owner.repaint();
    }

    double snapSeconds(double seconds) const
    {
        seconds = juce::jmax(0.0, seconds);
        if (!isLibertySnapEnabled()) return seconds;
        const double step = getLibertySnapSeconds(owner.tempoBpm);
        return step > 0.0 ? std::round(seconds / step) * step : seconds;
    }

    void updateResize(float localMouseX)
    {
        if (dragTrack < 0) return;
        const double pps = getLibertyTimelinePixelsPerSecond();
        if (pps <= 0.0) return;

        const double mouseSeconds = juce::jmax(0.0, ((double)localMouseX - headerWidth) / pps);
        const double minLength = 0.01;
        const double originalRight = dragStart + dragLength;

        if (dragSide == Side::left)
        {
            double newStart = snapSeconds(mouseSeconds);
            newStart = juce::jlimit(0.0, originalRight - minLength, newStart);
            previewStart = newStart;
            previewLength = originalRight - newStart;
        }
        else
        {
            double newRight = snapSeconds(mouseSeconds);
            newRight = juce::jmax(dragStart + minLength, newRight);
            previewStart = dragStart;
            previewLength = newRight - dragStart;
        }

        repaint();
    }

    void finishResize()
    {
        if (dragTrack < 0) return;
        const int track = dragTrack;
        const bool left = dragSide == Side::left;
        juce::String error;
        const bool ok = commitLibertyAudioClipResize(owner, track, previewStart, previewLength,
                                                      stretchMode, left, error);
        dragTrack = -1;
        previewLength = 0.0;
        repaint();
        if (!ok)
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Liberty - Audio Resize",
                                                   error,
                                                   "OK");
    }

    void timerCallback() override
    {
        if (stopped.load()) return;
        const auto wanted = owner.getLocalBounds();
        if (getBounds() != wanted) setBounds(wanted);

        const double pps = getLibertyTimelinePixelsPerSecond();
        const int rowH = getLibertyTrackRowHeight();
        for (int t = 0; t < AudioEngine::maxAudioTracks; ++t)
        {
            const bool show = owner.audioEngine.hasAudioFile(t) && owner.selectedTrack == t && dragTrack < 0;
            auto& left = *leftHandles[(size_t)t];
            auto& right = *rightHandles[(size_t)t];
            left.setVisible(show);
            right.setVisible(show);
            if (!show) continue;

            const int clipX = headerWidth + (int)std::llround(owner.audioEngine.getTrackStartSeconds(t) * pps);
            const int clipW = juce::jmax(2, (int)std::llround(owner.audioEngine.getAudioFileLengthSeconds(t) * pps));
            const int y = 76 + rulerHeight + t * rowH + 4;
            const int h = rowH - 8;
            left.setBounds(clipX - handleWidth / 2, y, handleWidth, h);
            right.setBounds(clipX + clipW - handleWidth / 2, y, handleWidth, h);
        }
        toFront(false);
    }

    MainComponent& owner;
    std::array<std::unique_ptr<Handle>, AudioEngine::maxAudioTracks> leftHandles;
    std::array<std::unique_ptr<Handle>, AudioEngine::maxAudioTracks> rightHandles;
    std::atomic<bool> stopped { false };
    int dragTrack = -1;
    Side dragSide = Side::right;
    bool stretchMode = false;
    float dragStartMouseX = 0.0f;
    double dragStart = 0.0;
    double dragLength = 0.0;
    double previewStart = 0.0;
    double previewLength = 0.0;
};

std::map<MainComponent*, std::unique_ptr<AudioClipResizeController>> controllers;

class Bootstrap final : private juce::Timer
{
public:
    Bootstrap() { startTimerHz(10); }
    ~Bootstrap() override { shutdown(); }

    void shutdown()
    {
        stopTimer();
        for (auto& item : controllers)
            if (item.second) item.second->shutdown();
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
                        controllers.emplace(main, std::make_unique<AudioClipResizeController>(*main));
    }
};

Bootstrap bootstrap;
}

void shutdownLibertyAudioClipResizeInteraction()
{
    bootstrap.shutdown();
}
