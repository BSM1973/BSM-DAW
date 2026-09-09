#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include <array>
#include <vector>
#include <functional>

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

    bool hasUnsavedChanges() const;
    void requestClose(std::function<void(bool)> completion);

private:
    class AudioSettingsWindow;

    class TempoControls final : public juce::Component
    {
    public:
        explicit TempoControls(MainComponent* ownerIn) : owner(ownerIn)
        {
            tempoButton.setButtonText("120.00 BPM");
            meterButton.setButtonText("4/4");

            for (auto* button : { &tempoButton, &meterButton })
            {
                button->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
                button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff252a31));
                button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffc9cdd3));
                button->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
                button->setMouseClickGrabsKeyboardFocus(false);
                addAndMakeVisible(button);
            }

            tempoButton.onClick = [this] { owner->editTempo(); };
            meterButton.onClick = [this] { owner->editTimeSignature(); };
            setBounds(550, 34, 190, 36);
            owner->addAndMakeVisible(this);
        }

        void refresh()
        {
            tempoButton.setButtonText(juce::String(owner->tempoBpm, 2) + " BPM");
            meterButton.setButtonText(juce::String(owner->timeSignatureNumerator) + "/" + juce::String(owner->timeSignatureDenominator));
            repaint();
        }

        void resized() override
        {
            tempoButton.setBounds(0, 0, 120, 36);
            meterButton.setBounds(120, 0, 50, 36);
        }

    private:
        MainComponent* owner;
        juce::TextButton tempoButton;
        juce::TextButton meterButton;
    };

    class ProjectButton final : public juce::Component,
                                private juce::Timer
    {
    public:
        explicit ProjectButton(MainComponent* ownerIn) : owner(ownerIn)
        {
            button.setButtonText("PROJECT");
            button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
            button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff303640));
            button.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            button.setMouseClickGrabsKeyboardFocus(false);
            button.onClick = [this] { owner->showProjectMenu(); };
            addAndMakeVisible(button);

            setBounds(215, 10, 90, 24);
            owner->addAndMakeVisible(this);
            owner->initializeProjectTracking();
            startTimerHz(5);
        }

        void resized() override { button.setBounds(getLocalBounds()); }

    private:
        void timerCallback() override
        {
            const auto label = owner->hasUnsavedChanges() ? "PROJECT *" : "PROJECT";
            if (button.getButtonText() != label)
                button.setButtonText(label);
        }

        MainComponent* owner;
        juce::TextButton button;
    };

    void timerCallback() override;
    void drawTransport(juce::Graphics& g, juce::Rectangle<int> area);
    void drawTrackArea(juce::Graphics& g, juce::Rectangle<int> area);
    void drawMixer(juce::Graphics& g, juce::Rectangle<int> area);
    void openAudioSettings();
    void openAudioFile();
    void editTempo();
    void editTimeSignature();
    void rebuildWaveformCache(int trackIndex);
    bool handleMixerMouse(const juce::MouseEvent& event);
    int getAudioTrackAtPosition(juce::Point<int> position) const;
    bool isPointInsideAudioClip(int trackIndex, juce::Point<int> position) const;

    void showProjectMenu();
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    bool saveProjectToFile(const juce::File& file);
    bool loadProjectFromFile(const juce::File& file);
    void resetProjectState();
    void initializeProjectTracking();
    juce::String getProjectStateSignature() const;
    void markProjectClean();
    void confirmBeforeProjectAction(std::function<void()> action);

    AudioEngine audioEngine;
    std::unique_ptr<AudioSettingsWindow> audioSettingsWindow;
    std::unique_ptr<juce::FileChooser> audioFileChooser;
    std::unique_ptr<juce::FileChooser> projectFileChooser;
    std::array<std::vector<float>, AudioEngine::maxAudioTracks> waveformMin;
    std::array<std::vector<float>, AudioEngine::maxAudioTracks> waveformMax;
    std::array<juce::File, AudioEngine::maxAudioTracks> trackSourceFiles;
    juce::File currentProjectFile;
    juce::String savedProjectStateSignature;
    std::function<void()> pendingProjectAction;
    int selectedTrack = 0;
    bool isPlaying = false;
    double playheadSeconds = 0.0;
    double tempoBpm = 120.0;
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;
    TempoControls tempoControls { this };
    ProjectButton projectButton { this };
    bool draggingClip = false;
    int draggedTrack = -1;
    float dragStartMouseX = 0.0f;
    double dragStartSeconds = 0.0;
    int mixerDragMode = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};