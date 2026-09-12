#define private public
#include "MainComponent.h"
#undef private

#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>

int getLibertyTrackRowHeight() noexcept;
juce::String getLibertyTrackName(int track);

namespace
{
class AudioClipWarpView final : public juce::Component, private juce::Timer
{
public:
    explicit AudioClipWarpView(MainComponent& ownerIn)
        : owner(ownerIn), ownerListener(*this)
    {
        setInterceptsMouseClicks(true, true);
        setVisible(false);
        owner.addAndMakeVisible(this);
        owner.addMouseListener(&ownerListener, true);
        startTimerHz(15);
    }

    ~AudioClipWarpView() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        owner.removeMouseListener(&ownerListener);
        setVisible(false);
    }

    void showTrack(int track)
    {
        if (track < 0 || track >= AudioEngine::maxAudioTracks || !owner.audioEngine.hasAudioFile(track)) return;
        selectedTrack = track;
        owner.selectedTrack = track;
        setBounds(0, juce::jmax(0, owner.getHeight() - 210), owner.getWidth(), 210);
        setVisible(true);
        toFront(false);
        repaint();
        owner.repaint();
    }

    void hideView()
    {
        draggingMarker = -1;
        setVisible(false);
        owner.repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0d1014));
        g.setColour(juce::Colour(0xff343a44));
        g.drawHorizontalLine(0, 0.0f, (float)getWidth());

        if (selectedTrack < 0 || !owner.audioEngine.hasAudioFile(selectedTrack)) return;

        drawControls(g);
        drawWaveform(g);
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        if (waveformBounds().contains(e.getPosition()))
        {
            const int marker = markerAtX(e.x);
            setMouseCursor(marker > 0 && marker < owner.audioEngine.getTrackWarpMarkerCount(selectedTrack) - 1
                ? juce::MouseCursor::LeftRightResizeCursor
                : juce::MouseCursor::CrosshairCursor);
            return;
        }
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (mixerButton().contains(e.getPosition()))
        {
            hideView();
            return;
        }

        if (warpButton().contains(e.getPosition()))
        {
            owner.audioEngine.setTrackWarpEnabled(selectedTrack,
                !owner.audioEngine.isTrackWarpEnabled(selectedTrack));
            repaint();
            return;
        }

        if (modeButton().contains(e.getPosition()))
        {
            showModeMenu();
            return;
        }

        if (resetButton().contains(e.getPosition()))
        {
            owner.audioEngine.resetTrackWarpMarkers(selectedTrack);
            repaint();
            return;
        }

        if (!waveformBounds().contains(e.getPosition())) return;

        const int marker = markerAtX(e.x);
        if (e.mods.isRightButtonDown())
        {
            if (marker > 0 && marker < owner.audioEngine.getTrackWarpMarkerCount(selectedTrack) - 1)
                owner.audioEngine.removeTrackWarpMarker(selectedTrack, marker);
            repaint();
            return;
        }

        if (marker > 0 && marker < owner.audioEngine.getTrackWarpMarkerCount(selectedTrack) - 1)
        {
            draggingMarker = marker;
            owner.audioEngine.setTrackWarpEnabled(selectedTrack, true);
        }
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (!waveformBounds().contains(e.getPosition()) || selectedTrack < 0) return;
        const double time = timeForX(e.x);
        if (owner.audioEngine.addTrackWarpMarker(selectedTrack, time, time))
        {
            owner.audioEngine.setTrackWarpEnabled(selectedTrack, true);
            repaint();
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (draggingMarker <= 0 || selectedTrack < 0) return;
        owner.audioEngine.moveTrackWarpMarker(selectedTrack, draggingMarker, timeForX(e.x));
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        draggingMarker = -1;
    }

private:
    class OwnerListener final : public juce::MouseListener
    {
    public:
        explicit OwnerListener(AudioClipWarpView& v) : view(v) {}

        void mouseDown(const juce::MouseEvent& e) override
        {
            if (view.stopped.load()) return;
            if (e.eventComponent == &view || view.isParentOf(e.eventComponent)) return;

            const auto relative = e.getEventRelativeTo(&view.owner);
            const auto p = relative.getPosition();
            const int track = view.owner.getAudioTrackAtPosition(p);
            if (track >= 0 && view.owner.isPointInsideAudioClip(track, p))
            {
                view.showTrack(track);
                return;
            }

            const int arrangerTop = 76 + 32;
            const int arrangerBottom = view.owner.getHeight() - 210;
            if (p.y >= arrangerTop && p.y < arrangerBottom)
                view.hideView();
        }

    private:
        AudioClipWarpView& view;
    };

    juce::Rectangle<int> mixerButton() const { return { 10, 10, 68, 24 }; }
    juce::Rectangle<int> warpButton() const { return { 84, 10, 74, 24 }; }
    juce::Rectangle<int> modeButton() const { return { 10, 44, 148, 27 }; }
    juce::Rectangle<int> resetButton() const { return { 10, 79, 148, 24 }; }
    juce::Rectangle<int> waveformBounds() const { return { 174, 12, juce::jmax(1, getWidth() - 188), juce::jmax(1, getHeight() - 24) }; }

    static juce::String modeName(int mode)
    {
        switch (mode)
        {
            case 0: return "Beats";
            case 1: return "Tones";
            case 2: return "Texture";
            case 3: return "Re-Pitch";
            case 4: return "Complex";
            default: return "Beats";
        }
    }

    void drawControls(juce::Graphics& g)
    {
        auto drawButton = [&g](juce::Rectangle<int> r, const juce::String& text, bool active)
        {
            g.setColour(active ? juce::Colour(0xff315f7a) : juce::Colour(0xff252a31));
            g.fillRoundedRectangle(r.toFloat(), 5.0f);
            g.setColour(active ? juce::Colour(0xff72d8f5) : juce::Colour(0xff4b525c));
            g.drawRoundedRectangle(r.toFloat(), 5.0f, 1.0f);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(10.0f, juce::Font::bold));
            g.drawText(text, r, juce::Justification::centred);
        };

        drawButton(mixerButton(), "MIXER", false);
        drawButton(warpButton(), owner.audioEngine.isTrackWarpEnabled(selectedTrack) ? "WARP ON" : "WARP OFF",
                   owner.audioEngine.isTrackWarpEnabled(selectedTrack));
        drawButton(modeButton(), "MODE  " + modeName(owner.audioEngine.getTrackWarpMode(selectedTrack)), true);
        drawButton(resetButton(), "RESET WARP", false);

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText(getLibertyTrackName(selectedTrack), 10, 116, 148, 18, juce::Justification::centredLeft, true);
        g.setColour(juce::Colour(0xff9ca5af));
        g.setFont(juce::Font(9.0f));
        g.drawText(owner.audioEngine.getAudioFileName(selectedTrack), 10, 136, 148, 16, juce::Justification::centredLeft, true);
        g.drawText("Double-clic : marqueur", 10, 163, 148, 14, juce::Justification::centredLeft);
        g.drawText("Glisser : Warp", 10, 178, 148, 14, juce::Justification::centredLeft);
        g.drawText("Clic droit : supprimer", 10, 193, 148, 14, juce::Justification::centredLeft);
    }

    void drawWaveform(juce::Graphics& g)
    {
        const auto area = waveformBounds();
        g.setColour(juce::Colour(0xff14181e));
        g.fillRoundedRectangle(area.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff3b424c));
        g.drawRoundedRectangle(area.toFloat(), 5.0f, 1.0f);

        const auto* buffer = owner.audioEngine.getAudioBuffer(selectedTrack);
        if (buffer == nullptr || buffer->getNumSamples() <= 0 || buffer->getNumChannels() <= 0) return;

        const int centreY = area.getCentreY();
        const float amplitude = (float)area.getHeight() * 0.43f;
        const int width = juce::jmax(1, area.getWidth() - 8);
        const int samples = buffer->getNumSamples();
        const int channels = buffer->getNumChannels();

        juce::Path upper, lower;
        bool first = true;
        for (int px = 0; px < width; ++px)
        {
            const int start = (int)((std::int64_t)px * samples / width);
            const int end = juce::jmax(start + 1, (int)((std::int64_t)(px + 1) * samples / width));
            const int step = juce::jmax(1, (end - start) / 48);
            float lo = 0.0f, hi = 0.0f;
            for (int s = start; s < end && s < samples; s += step)
                for (int ch = 0; ch < channels; ++ch)
                {
                    const float v = buffer->getSample(ch, s);
                    lo = juce::jmin(lo, v);
                    hi = juce::jmax(hi, v);
                }
            const float x = (float)area.getX() + 4.0f + (float)px;
            const float yTop = (float)centreY - hi * amplitude;
            const float yBottom = (float)centreY - lo * amplitude;
            if (first)
            {
                upper.startNewSubPath(x, yTop);
                lower.startNewSubPath(x, yBottom);
                first = false;
            }
            else
            {
                upper.lineTo(x, yTop);
                lower.lineTo(x, yBottom);
            }
        }

        g.setColour(juce::Colour(0xff78bfe8));
        g.strokePath(upper, juce::PathStrokeType(1.0f));
        g.strokePath(lower, juce::PathStrokeType(1.0f));
        g.setColour(juce::Colour(0xff2d3540));
        g.drawHorizontalLine(centreY, (float)area.getX() + 4.0f, (float)area.getRight() - 4.0f);

        const int count = owner.audioEngine.getTrackWarpMarkerCount(selectedTrack);
        for (int i = 0; i < count; ++i)
        {
            const double target = owner.audioEngine.getTrackWarpMarkerTargetSeconds(selectedTrack, i);
            const double source = owner.audioEngine.getTrackWarpMarkerSourceSeconds(selectedTrack, i);
            const int x = xForTime(target);
            const bool endpoint = i == 0 || i == count - 1;
            g.setColour(endpoint ? juce::Colour(0xff7c8794) : juce::Colour(0xffffc857));
            g.drawVerticalLine(x, (float)area.getY() + 4.0f, (float)area.getBottom() - 4.0f);
            g.fillEllipse((float)x - 4.0f, (float)area.getY() + 5.0f, 8.0f, 8.0f);
            if (!endpoint)
            {
                g.setColour(juce::Colour(0xffd5d9de));
                g.setFont(juce::Font(8.0f));
                g.drawText(juce::String(source, 2) + "s", x + 5, area.getY() + 4, 48, 12, juce::Justification::left);
            }
        }

        const double localPlayhead = owner.audioEngine.getCurrentTimeSeconds() - owner.audioEngine.getTrackStartSeconds(selectedTrack);
        const double length = owner.audioEngine.getAudioFileLengthSeconds(selectedTrack);
        if (localPlayhead >= 0.0 && localPlayhead <= length)
        {
            const int x = xForTime(localPlayhead);
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.drawVerticalLine(x, (float)area.getY(), (float)area.getBottom());
        }
    }

    int xForTime(double seconds) const
    {
        const auto area = waveformBounds();
        const double length = juce::jmax(0.000001, owner.audioEngine.getAudioFileLengthSeconds(selectedTrack));
        const double n = juce::jlimit(0.0, 1.0, seconds / length);
        return area.getX() + 4 + (int)std::llround(n * (double)juce::jmax(1, area.getWidth() - 8));
    }

    double timeForX(int x) const
    {
        const auto area = waveformBounds();
        const double length = juce::jmax(0.000001, owner.audioEngine.getAudioFileLengthSeconds(selectedTrack));
        const double n = juce::jlimit(0.0, 1.0,
            ((double)x - (double)area.getX() - 4.0) / (double)juce::jmax(1, area.getWidth() - 8));
        return n * length;
    }

    int markerAtX(int x) const
    {
        if (selectedTrack < 0) return -1;
        const int count = owner.audioEngine.getTrackWarpMarkerCount(selectedTrack);
        int best = -1;
        int bestDistance = 9;
        for (int i = 0; i < count; ++i)
        {
            const int mx = xForTime(owner.audioEngine.getTrackWarpMarkerTargetSeconds(selectedTrack, i));
            const int distance = std::abs(x - mx);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = i;
            }
        }
        return best;
    }

    void showModeMenu()
    {
        juce::PopupMenu menu;
        const juce::StringArray modes { "Beats", "Tones", "Texture", "Re-Pitch", "Complex" };
        const int current = owner.audioEngine.getTrackWarpMode(selectedTrack);
        for (int i = 0; i < modes.size(); ++i)
            menu.addItem(i + 1, modes[i], true, current == i);

        const auto target = localAreaToGlobal(modeButton());
        juce::Component::SafePointer<AudioClipWarpView> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(target), [safe](int result)
        {
            if (safe == nullptr || result <= 0 || safe->selectedTrack < 0) return;
            safe->owner.audioEngine.setTrackWarpMode(safe->selectedTrack, result - 1);
            safe->owner.audioEngine.setTrackWarpEnabled(safe->selectedTrack, true);
            safe->repaint();
        });
    }

    void timerCallback() override
    {
        if (stopped.load()) return;
        if (!isVisible()) return;
        if (selectedTrack < 0 || !owner.audioEngine.hasAudioFile(selectedTrack))
        {
            hideView();
            return;
        }
        const auto wanted = juce::Rectangle<int>(0, juce::jmax(0, owner.getHeight() - 210), owner.getWidth(), 210);
        if (getBounds() != wanted) setBounds(wanted);
        repaint();
    }

    MainComponent& owner;
    OwnerListener ownerListener;
    std::atomic<bool> stopped { false };
    int selectedTrack = -1;
    int draggingMarker = -1;
};

std::map<MainComponent*, std::unique_ptr<AudioClipWarpView>> controllers;

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
                        controllers.emplace(main, std::make_unique<AudioClipWarpView>(*main));
    }
};

Bootstrap bootstrap;
}

void shutdownLibertyAudioClipWarpView()
{
    bootstrap.shutdown();
}
