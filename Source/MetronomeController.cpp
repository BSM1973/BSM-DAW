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
                                  private juce::Timer
{
public:
    explicit MetronomeController(MainComponent& ownerIn) : owner(ownerIn)
    {
        setInterceptsMouseClicks(true, false);
        setAlwaysOnTop(true);
        owner.addAndMakeVisible(this);
        // Audio output is owned exclusively by AudioEngine.
        startTimerHz(12);
    }

    ~MetronomeController() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        // No independent device callback: AudioEngine owns the master output.
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
        const bool next = !enabled.load(std::memory_order_relaxed);
        enabled.store(next, std::memory_order_relaxed);
        owner.audioEngine.setMetronomeTiming(owner.tempoBpm, owner.timeSignatureNumerator, owner.timeSignatureDenominator);
        owner.audioEngine.setMetronomeEnabled(next);
        repaint();
    }

private:
    juce::Rectangle<int> buttonBounds() const { return { 850, 38, 64, 28 }; }

    void timerCallback() override
    {
        if (stopped.load()) return;
        const auto wanted = owner.getLocalBounds();
        if (getBounds() != wanted) setBounds(wanted);
        owner.audioEngine.setMetronomeTiming(owner.tempoBpm, owner.timeSignatureNumerator, owner.timeSignatureDenominator);
        toFront(false);
        repaint(buttonBounds());
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
