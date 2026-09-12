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
int getLibertyTrackRowHeight() noexcept;

namespace
{
constexpr int rulerH = 32;
constexpr int instrumentTrack = AudioEngine::maxAudioTracks + 1;
constexpr int controlledTracks = AudioEngine::maxAudioTracks + 1;

class PanLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override
    {
        const auto size = (float)juce::jmin(width, height) - 2.0f;
        const auto cx = (float)x + (float)width * 0.5f;
        const auto cy = (float)y + (float)height * 0.5f;
        const auto radius = size * 0.5f;
        const auto bounds = juce::Rectangle<float>(cx - radius, cy - radius, size, size);
        const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        g.setColour(juce::Colour(0xffff9a24));
        g.fillEllipse(bounds);

        g.setColour(juce::Colour(0xff5b2b00));
        g.fillEllipse(bounds.reduced(4.0f));

        juce::Path pointer;
        const float pointerLength = radius * 0.72f;
        const float pointerThickness = 2.6f;
        pointer.addRoundedRectangle(-pointerThickness * 0.5f, -pointerLength,
                                    pointerThickness, pointerLength, 1.0f);
        g.setColour(juce::Colours::white);
        g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(cx, cy));

        g.setColour(juce::Colour(0xffffc66d));
        g.fillEllipse(cx - 2.0f, cy - 2.0f, 4.0f, 4.0f);
    }
};

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
            pan.setLookAndFeel(&panLookAndFeel);
            pan.onValueChange = [this, i]
            {
                if (syncing) return;
                const float value = (float)panKnobs[(size_t)i].getValue();
                if (i < AudioEngine::maxAudioTracks) owner.audioEngine.setTrackPan(i, value);
                else setLibertyInstrumentPan(value);
                owner.repaint();
            };
            addAndMakeVisible(pan);

            auto& mute = muteButtons[(size_t)i];
            auto& solo = soloButtons[(size_t)i];
            mute.setButtonText("M");
            solo.setButtonText("S");
            mute.setMouseClickGrabsKeyboardFocus(false);
            solo.setMouseClickGrabsKeyboardFocus(false);
            mute.onClick = [this, i]
            {
                const int track = controlTrack(i);
                if (track < AudioEngine::maxAudioTracks)
                    owner.audioEngine.setTrackMuted(track, !owner.audioEngine.isTrackMuted(track));
                else
                    owner.audioEngine.setInstrumentTrackMuted(!owner.audioEngine.isInstrumentTrackMuted());
                syncButtons();
                owner.repaint();
            };
            solo.onClick = [this, i]
            {
                const int track = controlTrack(i);
                if (track < AudioEngine::maxAudioTracks)
                    owner.audioEngine.setTrackSolo(track, !owner.audioEngine.isTrackSolo(track));
                else
                    owner.audioEngine.setInstrumentTrackSolo(!owner.audioEngine.isInstrumentTrackSolo());
                syncButtons();
                owner.repaint();
            };
            addAndMakeVisible(mute);
            addAndMakeVisible(solo);
        }

        owner.addAndMakeVisible(this);
        startTimerHz(12);
    }

    ~TrackHeaderMixControls() override
    {
        shutdown();
        for (auto& pan : panKnobs) pan.setLookAndFeel(nullptr);
    }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        setVisible(false);
    }

    void paint(juce::Graphics& g) override
    {
        g.setFont(juce::Font(8.0f, juce::Font::bold));
        const int rowH = getLibertyTrackRowHeight();

        for (int i = 0; i < controlledTracks; ++i)
        {
            const int row = controlTrack(i);
            const int y = 76 + rulerH + row * rowH;
            const int controlY = y + rowH - 29;

            g.setColour(juce::Colour(0xff1e232a));
            g.fillRect(6, controlY - 2, 200, 29);

            g.setColour(juce::Colour(0xffc5cbd3));
            g.drawText("VOL", 8, controlY + 6, 22, 12, juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xffffb04d));
            g.drawText("PAN", 104, controlY + 6, 27, 12, juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        const int rowH = getLibertyTrackRowHeight();
        for (int i = 0; i < controlledTracks; ++i)
        {
            const int row = controlTrack(i);
            const int y = 76 + rulerH + row * rowH;
            const int controlY = y + rowH - 29;

            volumeSliders[(size_t)i].setBounds(30, controlY + 2, 72, 20);
            panKnobs[(size_t)i].setBounds(130, controlY - 1, 28, 28);
            muteButtons[(size_t)i].setBounds(160, controlY + 2, 21, 20);
            soloButtons[(size_t)i].setBounds(184, controlY + 2, 21, 20);
        }
    }

private:
    static int controlTrack(int control) noexcept
    {
        return control < AudioEngine::maxAudioTracks ? control : instrumentTrack;
    }

    void syncButtons()
    {
        for (int i = 0; i < controlledTracks; ++i)
        {
            const int track = controlTrack(i);
            const bool muted = track < AudioEngine::maxAudioTracks
                ? owner.audioEngine.isTrackMuted(track)
                : owner.audioEngine.isInstrumentTrackMuted();
            const bool solo = track < AudioEngine::maxAudioTracks
                ? owner.audioEngine.isTrackSolo(track)
                : owner.audioEngine.isInstrumentTrackSolo();

            muteButtons[(size_t)i].setColour(juce::TextButton::buttonColourId,
                muted ? juce::Colour(0xff9b4545) : juce::Colour(0xff252a31));
            soloButtons[(size_t)i].setColour(juce::TextButton::buttonColourId,
                solo ? juce::Colour(0xff8b7a32) : juce::Colour(0xff252a31));
            muteButtons[(size_t)i].setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            soloButtons[(size_t)i].setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        }
    }

    void timerCallback() override
    {
        if (stopped.load()) return;

        const auto wantedBounds = owner.getLocalBounds();
        if (getBounds() != wantedBounds)
            setBounds(wantedBounds);
        else
            resized();

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
        syncButtons();
    }

    MainComponent& owner;
    PanLookAndFeel panLookAndFeel;
    std::array<juce::Slider, controlledTracks> volumeSliders;
    std::array<juce::Slider, controlledTracks> panKnobs;
    std::array<juce::TextButton, controlledTracks> muteButtons;
    std::array<juce::TextButton, controlledTracks> soloButtons;
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
