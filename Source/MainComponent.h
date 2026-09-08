#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include <array>
#include <vector>

class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent&) override
    {
        draggingClip = false;
        draggedTrack = -1;
    }
    bool keyPressed(const juce::KeyPress& key) override;

private:
    class AudioSettingsWindow;

    void timerCallback() override;
    void drawTransport(juce::Graphics& g, juce::Rectangle<int> area);
    void drawTrackArea(juce::Graphics& g, juce::Rectangle<int> area);
    void drawMixer(juce::Graphics& g, juce::Rectangle<int> area);
    void openAudioSettings();
    void openAudioFile();
    void rebuildWaveformCache(int trackIndex);
    bool handleMixerMouse(const juce::MouseEvent& event);
    int getAudioTrackAtPosition(juce::Point<int> position) const;
    bool isPointInsideAudioClip(int trackIndex, juce::Point<int> position) const;

    AudioEngine audioEngine;
    std::unique_ptr<AudioSettingsWindow> audioSettingsWindow;
    std::unique_ptr<juce::FileChooser> audioFileChooser;
    std::array<std::vector<float>, AudioEngine::maxAudioTracks> waveformMin;
    std::array<std::vector<float>, AudioEngine::maxAudioTracks> waveformMax;
    int selectedTrack = 0;
    bool isPlaying = false;
    double playheadSeconds = 0.0;
    bool draggingClip = false;
    int draggedTrack = -1;
    float dragStartMouseX = 0.0f;
    double dragStartSeconds = 0.0;
    int mixerDragMode = 0;
    bool keyboardFocusEnabled = (setWantsKeyboardFocus(true), true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
