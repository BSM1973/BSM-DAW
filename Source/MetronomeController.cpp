#define private public
#include "MainComponent.h"
#undef private

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>

namespace
{
class MetronomeController final : public juce::Component,
                                  private juce::AudioIODeviceCallback,
                                  private juce::Timer
{
public:
    explicit MetronomeController(MainComponent& ownerIn) : owner(ownerIn)
    {
        setInterceptsMouseClicks(true, false);
        setAlwaysOnTop(true);
        owner.addAndMakeVisible(this);
        owner.audioEngine.getDeviceManager().addAudioCallback(this);
        startTimerHz(12);
    }

    ~MetronomeController() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        owner.audioEngine.getDeviceManager().removeAudioCallback(this);
        setVisible(false);
    }

    bool hitTest(int x, int y) override
    {
        return buttonBounds().contains(x, y);
    }

    void paint(juce::Graphics& g) override
    {
        const auto b = buttonBounds();
        g.setColour(enabled.load() ? juce::Colour(0xff285d46) : juce::Colour(0xff252a31));
        g.fillRoundedRectangle(b.toFloat(), 5.0f);
        g.setColour(enabled.load() ? juce::Colour(0xff67e6a3) : juce::Colour(0xff4b525c));
        g.drawRoundedRectangle(b.toFloat(), 5.0f, 1.0f);

        const float cx = (float)b.getX() + 17.0f;
        const float top = (float)b.getY() + 5.0f;
        juce::Path body;
        body.startNewSubPath(cx - 7.0f, top + 17.0f);
        body.lineTo(cx + 7.0f, top + 17.0f);
        body.lineTo(cx + 4.0f, top + 3.0f);
        body.lineTo(cx - 4.0f, top + 3.0f);
        body.closeSubPath();
        g.setColour(enabled.load() ? juce::Colour(0xffb7ffd3) : juce::Colour(0xffc5cbd3));
        g.strokePath(body, juce::PathStrokeType(1.5f));
        g.drawLine(cx, top + 4.0f, cx + 5.0f, top + 12.0f, 1.5f);
        g.fillEllipse(cx + 3.4f, top + 10.4f, 3.2f, 3.2f);

        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText(enabled.load() ? "ON" : "OFF", b.getX() + 31, b.getY(), 29, b.getHeight(), juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!buttonBounds().contains(e.getPosition())) return;
        enabled.store(!enabled.load());
        repaint();
    }

private:
    juce::Rectangle<int> buttonBounds() const { return { 850, 38, 64, 28 }; }

    void timerCallback() override
    {
        if (stopped.load()) return;
        const auto wanted = owner.getLocalBounds();
        if (getBounds() != wanted) setBounds(wanted);
        toFront(false);
        repaint(buttonBounds());
    }

    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const*, int,
                                           float* const* outputs, int numOutputs,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::clear(outputs[ch], numSamples);

        if (!enabled.load(std::memory_order_relaxed) || !owner.audioEngine.isPlaying()) return;

        const double rate = owner.audioEngine.getSampleRate();
        if (rate <= 0.0) return;

        const double bpm = juce::jmax(1.0, owner.tempoBpm);
        const int numerator = juce::jmax(1, owner.timeSignatureNumerator);
        const int denominator = juce::jmax(1, owner.timeSignatureDenominator);
        const double secondsPerBeat = 60.0 / bpm * (4.0 / (double)denominator);
        const auto beatSamples = juce::jmax<std::int64_t>(1, (std::int64_t)std::llround(secondsPerBeat * rate));
        const auto clickSamples = juce::jmax<std::int64_t>(1, (std::int64_t)std::llround(0.035 * rate));
        const auto position = owner.audioEngine.transportSamples.load(std::memory_order_relaxed);
        constexpr double twoPi = 6.28318530717958647692;

        for (int s = 0; s < numSamples; ++s)
        {
            const std::int64_t projectSample = position + s;
            const std::int64_t beatIndex = projectSample / beatSamples;
            const std::int64_t offset = projectSample % beatSamples;
            if (offset >= clickSamples) continue;

            const bool accent = (beatIndex % numerator) == 0;
            const double t = (double)offset / rate;
            const double frequency = accent ? 1760.0 : 1200.0;
            const double decay = std::exp(-t * 95.0);
            const float value = (float)(std::sin(twoPi * frequency * t) * decay * (accent ? 0.34 : 0.22));

            if (numOutputs > 0 && outputs[0] != nullptr) outputs[0][s] += value;
            if (numOutputs > 1 && outputs[1] != nullptr) outputs[1][s] += value;
        }
    }

    MainComponent& owner;
    std::atomic<bool> enabled { false };
    std::atomic<bool> stopped { false };
};

std::map<MainComponent*, std::unique_ptr<MetronomeController>> controllers;

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
                        controllers.emplace(main, std::make_unique<MetronomeController>(*main));
    }
};

Bootstrap bootstrap;
}

void shutdownLibertyMetronomeController()
{
    bootstrap.shutdown();
}
