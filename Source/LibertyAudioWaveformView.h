#pragma once
#include <JuceHeader.h>
#include <functional>
#include <vector>
#include <cmath>

// Shared geometry and waveform renderer for Liberty's dock and floating audio views.
// The host owns transport, selection and Warp data; this component never changes them.
class LibertyAudioWaveformView final : public juce::Component
{
public:
    struct Marker { double seconds = 0.0; bool transient = false; };
    std::function<std::shared_ptr<const juce::AudioBuffer<float>>()> getBuffer;
    std::function<double()> getDuration;
    std::function<double()> getSourceDuration;
    std::function<std::vector<std::pair<double, double>>()> getWarpPairs;
    std::function<std::vector<Marker>()> getMarkers;
    std::function<void(double, const juce::MouseEvent&)> onPress;
    std::function<void(double, const juce::MouseEvent&)> onMove;
    std::function<void(double, const juce::MouseEvent&)> onRelease;
    // Centralized editing dispatch. Hosts supply the AudioEngine operations,
    // keeping their own SNAP, gesture priorities and undo policy.
    struct WarpEditActions
    {
        std::function<bool(double, double)> create;
        std::function<void(int, double)> move;
        std::function<void(int)> erase;
        bool add(double sourceSeconds, double targetSeconds) const
        {
            return create ? create(sourceSeconds, targetSeconds) : false;
        }
        void drag(int index, double targetSeconds) const
        {
            if (move && index > 0 && std::isfinite(targetSeconds))
                move(index, targetSeconds);
        }
        void remove(int index) const
        {
            if (erase && index > 0) erase(index);
        }
    };

    // Shared piecewise-linear Warp transform, used by both waveform hosts.
    // Marker pairs are { sourceSeconds, targetSeconds } in ascending target order.
    static double sourceFractionForTarget(double targetFraction, double targetDuration,
                                         double sourceDuration,
                                         const std::vector<std::pair<double, double>>& markers)
    {
        if (markers.size() < 2) return targetFraction;
        const double target = juce::jlimit(0.0, targetDuration,
            targetFraction * targetDuration);
        for (size_t i = 0; i + 1 < markers.size(); ++i)
        {
            const double ta = markers[i].second, tb = markers[i + 1].second;
            if (target > tb && i + 2 < markers.size()) continue;
            const double alpha = juce::jlimit(0.0, 1.0,
                (target - ta) / juce::jmax(0.000001, tb - ta));
            return juce::jlimit(0.0, 1.0,
                (markers[i].first + (markers[i + 1].first - markers[i].first) * alpha)
                / juce::jmax(0.000001, sourceDuration));
        }
        return targetFraction;
    }

    // Shared interaction geometry: each host keeps its own gesture semantics.
    struct Viewport
    {
        juce::Rectangle<int> bounds;
        double duration = 0.0;
        double zoom = 1.0;
        double scroll = 0.0;
        double visibleFraction() const noexcept { return 1.0 / juce::jmax(1.0, zoom); }
        double clampedScroll() const noexcept
        {
            return juce::jlimit(0.0, 1.0 - visibleFraction(), scroll);
        }
        double secondsAt(int pixel) const noexcept
        {
            if (bounds.getWidth() <= 0) return 0.0;
            const double fraction = clampedScroll()
                + ((double)(pixel - bounds.getX()) / bounds.getWidth()) * visibleFraction();
            return juce::jlimit(0.0, juce::jmax(0.0, duration),
                fraction * juce::jmax(0.0, duration));
        }
        int pixelAt(double seconds) const noexcept
        {
            if (duration <= 0.0 || bounds.getWidth() <= 0) return bounds.getX();
            return bounds.getX() + (int)std::round(
                (seconds / duration - clampedScroll()) / visibleFraction() * bounds.getWidth());
        }
        int nearestMarker(double seconds, const std::vector<Marker>& markers,
                          int tolerancePixels = 7) const noexcept
        {
            const int cursor = pixelAt(seconds);
            int best = -1, distance = tolerancePixels + 1;
            for (int i = 0; i < (int)markers.size(); ++i)
            {
                const int delta = std::abs(pixelAt(markers[(size_t)i].seconds) - cursor);
                if (delta < distance) { distance = delta; best = i; }
            }
            return best;
        }
    };

    // Dock and floating editor can both use the same painter without
    // surrendering their own advanced waveform rendering and mouse tools.
    static void paintSharedMarkers(juce::Graphics& g, juce::Rectangle<int> bounds,
                                   const std::vector<Marker>& markers,
                                   const std::function<int(double)>& toPixel)
    {
        const juce::Graphics::ScopedSaveState clipped(g);
        g.reduceClipRegion(bounds);
        for (const auto& marker : markers)
        {
            const int x = toPixel(marker.seconds);
            if (x < bounds.getX() || x >= bounds.getRight()) continue;
            g.setColour(juce::Colour(marker.transient ? 0xff59d8b7 : 0xffffba66));
            g.drawVerticalLine(x, (float)bounds.getY(), (float)bounds.getBottom());
        }
    }
    double zoom = 1.0;
    double selectionStart = -1.0, selectionEnd = -1.0;
    bool loopSelection = false;

    double secondsAt(int x) const
    {
        const double duration = getDuration ? getDuration() : 0.0;
        return duration * juce::jlimit(0.0, 1.0,
            ((double)x / juce::jmax(1, getWidth())) / juce::jmax(1.0, zoom));
    }
    int pixelAt(double seconds) const
    {
        const double duration = getDuration ? getDuration() : 0.0;
        return (int)std::round(seconds / juce::jmax(0.001, duration)
            * getWidth() * juce::jmax(1.0, zoom));
    }
    void mouseDown(const juce::MouseEvent& e) override { if (onPress) onPress(secondsAt(e.x), e); }
    void mouseDrag(const juce::MouseEvent& e) override { if (onMove) onMove(secondsAt(e.x), e); }
    void mouseUp(const juce::MouseEvent& e) override { if (onRelease) onRelease(secondsAt(e.x), e); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff172d42));
        const auto pcm = getBuffer ? getBuffer() : nullptr;
        if (!pcm || pcm->getNumChannels() < 1 || pcm->getNumSamples() < 2) return;
        const int count = pcm->getNumSamples(), centre = getHeight() / 2;
        const auto warpPairs = getWarpPairs ? getWarpPairs()
            : std::vector<std::pair<double, double>>{};
        const double targetDuration = getDuration ? getDuration() : 0.0;
        const double sourceDuration = getSourceDuration ? getSourceDuration() : targetDuration;
        const auto sourceFraction = [&](double targetFraction)
        {
            return sourceFractionForTarget(targetFraction, targetDuration,
                sourceDuration, warpPairs);
        };
        const float* data = pcm->getReadPointer(0);
        g.setColour(juce::Colour(0xff52c5e8));
        for (int x = 0; x < getWidth(); ++x)
        {
            const double leftFraction = (double)x / juce::jmax(1, getWidth()) / zoom;
            const double rightFraction = (double)(x + 1) / juce::jmax(1, getWidth()) / zoom;
            const int first = juce::jlimit(0, count - 1,
                (int)(sourceFraction(leftFraction) * count));
            const int last = juce::jlimit(first + 1, count,
                (int)std::ceil(sourceFraction(rightFraction) * count));
            float peak = 0.0f;
            for (int i = first; i < last; ++i) peak = juce::jmax(peak, std::abs(data[i]));
            const int h = (int)(peak * (getHeight() - 12) * 0.5f);
            g.drawVerticalLine(x, (float)(centre - h), (float)(centre + h));
        }
        if (selectionStart >= 0 && selectionEnd >= 0)
        {
            const int left = juce::jmax(0, juce::jmin(pixelAt(selectionStart), pixelAt(selectionEnd)));
            const int right = juce::jmin(getWidth(), juce::jmax(pixelAt(selectionStart), pixelAt(selectionEnd)));
            g.setColour(juce::Colour(loopSelection ? 0xff2d927c : 0xff6baeea).withAlpha(0.23f));
            g.fillRect(left, 0, juce::jmax(0, right - left), getHeight());
        }
        if (getMarkers)
            for (const auto& marker : getMarkers())
            {
                const int x = pixelAt(marker.seconds);
                if (x < 0 || x >= getWidth()) continue;
                g.setColour(juce::Colour(marker.transient ? 0xff59d8b7 : 0xffffba66));
                g.drawVerticalLine(x, 0.0f, (float)getHeight());
            }
    }
};
