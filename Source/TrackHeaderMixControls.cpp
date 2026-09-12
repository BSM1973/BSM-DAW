#define private public
#include "MainComponent.h"
#undef private

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <atomic>
#include <map>
#include <memory>

float getLibertyInstrumentGain() noexcept;
void setLibertyInstrumentGain(float value) noexcept;
float getLibertyInstrumentPan() noexcept;
void setLibertyInstrumentPan(float value) noexcept;

namespace
{
constexpr int rulerH = 32;
constexpr int rowH = 70;
constexpr int instrumentTrack = AudioEngine::maxAudioTracks + 1;
constexpr int controlledTracks = AudioEngine::maxAudioTracks + 1;

class TrackHeaderMixControls final : public juce::Component,
                                     private juce::Timer
{
public:
    explicit TrackHeaderMixControls(MainComponent& ownerIn) : owner(ownerIn)
    {
        setInterceptsMouseClicks(false, true);

        for (int i = 0; i < controlledTracks; ++i)
        {
            auto& volume = volumeSliders[(size_t)i];
            volume.setSliderStyle(juce::Slider::LinearHorizontal);
            volume.setRange(0.0, 2.0, 0.001);
            volume.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            volume.setMouseClickGrabsKeyboardFocus(false);
            volume.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff171b20));
            volume.setColour(juce::Slider::trackColourId, juce::Colour(0xff4f82a7));
            volume.setColour(juce::Slider::thumbColourId, juce::Colour(0xffd8dde3));
            volume.onValueChange = [this, i]
            {
                if (syncing) return;
                const float value = (float)volumeSliders[(size_t)i].getValue();
                if (i < AudioEngine::maxAudioTracks) owner.audioEngine.setTrackGain(i, value);
                else setLibertyInstrumentGain(value);
                owner.repaint();
            };
            addAndMakeVisible(volume);

            auto& pan = panKnobs[(size_t)i];
            pan.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            pan.setRange(-1.0, 1.0, 0.001);
            pan.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            pan.setDoubleClickReturnValue(true, 0.0);
            pan.setMouseClickGrabsKeyboardFocus(false);
            pan.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff4f82a7));
            pan.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff252a31));
            pan.setColour(juce::Slider::thumbColourId, juce::Colours::white);
            pan.onValueChange = [this, i]
            {
                if (syncing) return;
                const float value = (float)panKnobs[(size_t)i].getValue();
                if (i < AudioEngine::maxAudioTracks) owner.audioEngine.setTrackPan(i, value);
                else setLibertyInstrumentPan(value);
                owner.repaint();
            };
            addAndMakeVisible(pan);
        }

        owner.addAndMakeVisible(this);
        startTimerHz(12);
    }

    ~TrackHeaderMixControls() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        setVisible(false);
    }

    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colour(0xffaeb6c0));
        g.setFont(juce::Font(8.0f, juce::Font::bold));

        for (int i = 0; i < controlledTracks; ++i)
        {
            const int row = (i < AudioEngine::maxAudioTracks) ? i : instrumentTrack;
            const int y = 76 + rulerH + row * rowH;
            g.drawText("VOL", 8, y + 42, 22, 12, juce::Justification::centredLeft);
            g.drawText("PAN", 111, y + 42, 28, 12, juce::Justification::centred);
        }
    }

    void resized() override
    {
        for (int i = 0; i < controlledTracks; ++i)
        {
            const int row = (i < AudioEngine::maxAudioTracks) ? i : instrumentTrack;
            const int y = 76 + rulerH + row * rowH;

            // Ligne 2 de l'en-tête : VOL | PAN | M | S
            volumeSliders[(size_t)i].setBounds(30, y + 39, 76, 20);
            panKnobs[(size_t)i].setBounds(112, y + 37, 26, 26);
        }
    }

private:
    void timerCallback() override
    {
        if (stopped.load()) return;

        const auto wantedBounds = owner.getLocalBounds();
        if (getBounds() != wantedBounds)
            setBounds(wantedBounds);

        syncing = true;
        for (int i = 0; i < controlledTracks; ++i)
        {
            const float gain = i < AudioEngine::maxAudioTracks
                ? owner.audioEngine.getTrackGain(i)
                : getLibertyInstrumentGain();
            const float pan = i < AudioEngine::maxAudioTracks
                ? owner.audioEngine.getTrackPan(i)
                : getLibertyInstrumentPan();
            volumeSliders[(size_t)i].setValue(gain, juce::dontSendNotification);
            panKnobs[(size_t)i].setValue(pan, juce::dontSendNotification);
        }
        syncing = false;

        // Aucun toFront() périodique : le z-order reste stable et ne clignote plus.
    }

    MainComponent& owner;
    std::array<juce::Slider, controlledTracks> volumeSliders;
    std::array<juce::Slider, controlledTracks> panKnobs;
    std::atomic<bool> stopped { false };
    bool syncing = false;
};

std::map<MainComponent*, std::unique_ptr<TrackHeaderMixControls>> controllers;

class Bootstrap final : private juce::Timer
{
public:
    Bootstrap() { startTimerHz(10); }
    ~Bootstrap() override { shutdown(); }

    void shutdown()
    {
        stopTimer();
        for (auto& p : controllers)
            if (p.second) p.second->shutdown();
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
                        controllers.emplace(main, std::make_unique<TrackHeaderMixControls>(*main));
    }
};

Bootstrap bootstrap;
}

void shutdownLibertyTrackHeaderMixControls()
{
    bootstrap.shutdown();
}
