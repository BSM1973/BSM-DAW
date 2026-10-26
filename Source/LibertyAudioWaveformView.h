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
    std::function<std::vector<Marker>()> getMarkers;
    std::function<void(double, const juce::MouseEvent&)> onPress;
    std::function<void(double, const juce::MouseEvent&)> onMove;
    std::function<void(double, const juce::MouseEvent&)> onRelease;
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
        const float* data = pcm->getReadPointer(0);
        g.setColour(juce::Colour(0xff52c5e8));
        for (int x = 0; x < getWidth(); ++x)
        {
            const int first = juce::jlimit(0, count - 1,
                (int)((double)x / juce::jmax(1, getWidth()) / zoom * count));
            const int last = juce::jmin(count, first + juce::jmax(1,
                (int)(count / (double)juce::jmax(1, getWidth()) / zoom)));
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
