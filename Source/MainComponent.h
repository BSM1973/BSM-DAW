#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "MidiEngine.h"
#include <array>
#include <vector>
#include <functional>
#include <cmath>

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
    void mouseUp(const juce::MouseEvent&) override { draggingClip = false; draggedTrack = -1; }
    bool keyPressed(const juce::KeyPress& key) override;
    bool hasUnsavedChanges() const;
    void requestClose(std::function<void(bool)> completion);
    MidiEngine& getMidiEngine() noexcept { return midiEngine; }
    const MidiEngine& getMidiEngine() const noexcept { return midiEngine; }
    double getTempoBpm() const noexcept { return tempoBpm; }
    int getTimeSignatureNumerator() const noexcept { return timeSignatureNumerator; }
    int getTimeSignatureDenominator() const noexcept { return timeSignatureDenominator; }
    double getAudioCurrentTimeSeconds() const noexcept { return audioEngine.getCurrentTimeSeconds(); }
    bool isAudioPlaying() const noexcept { return audioEngine.isPlaying(); }
    void selectMidiTrack() noexcept { selectedTrack = -1; repaint(); }

private:
    class AudioSettingsWindow final : public juce::DocumentWindow
    {
    public:
        explicit AudioSettingsWindow(AudioEngine& engine)
            : juce::DocumentWindow("Liberty - Audio Settings", juce::Colour(0xff15181d), juce::DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new juce::AudioDeviceSelectorComponent(engine.getDeviceManager(), 0, 2, 1, 2, false, true, true, false), true);
            setResizable(true, true);
            centreWithSize(620, 500);
            setVisible(false);
        }
        void closeButtonPressed() override { setVisible(false); }
    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettingsWindow)
    };
    class TempoControls final : public juce::Component
    {
    public:
        explicit TempoControls(MainComponent* ownerIn) : owner(ownerIn)
        {
            tempoButton.setButtonText("120.00 BPM"); meterButton.setButtonText("4/4");
            for (auto* button : { &tempoButton, &meterButton })
            {
                button->setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
                button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff252a31));
                button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffc9cdd3));
                button->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
                button->setMouseClickGrabsKeyboardFocus(false); addAndMakeVisible(button);
            }
            tempoButton.onClick = [this] { owner->editTempo(); }; meterButton.onClick = [this] { owner->editTimeSignature(); };
            setBounds(550, 34, 190, 36); owner->addAndMakeVisible(this);
        }
        void refresh() { tempoButton.setButtonText(juce::String(owner->tempoBpm, 2) + " BPM"); meterButton.setButtonText(juce::String(owner->timeSignatureNumerator) + "/" + juce::String(owner->timeSignatureDenominator)); repaint(); }
        void resized() override { tempoButton.setBounds(0, 0, 120, 36); meterButton.setBounds(120, 0, 50, 36); }
    private: MainComponent* owner; juce::TextButton tempoButton; juce::TextButton meterButton;
    };
    class ProjectButton final : public juce::Component, private juce::Timer
    {
    public:
        explicit ProjectButton(MainComponent* ownerIn) : owner(ownerIn)
        {
            button.setButtonText("PROJECT"); button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31)); button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff303640)); button.setColour(juce::TextButton::textColourOffId, juce::Colours::white); button.setColour(juce::TextButton::textColourOnId, juce::Colours::white); button.setMouseClickGrabsKeyboardFocus(false); button.onClick = [this] { owner->showProjectMenu(); }; addAndMakeVisible(button); setBounds(215, 10, 90, 24); owner->addAndMakeVisible(this); owner->initializeProjectTracking(); startTimerHz(5);
        }
        void resized() override { button.setBounds(getLocalBounds()); }
    private:
        void timerCallback() override { const auto label = owner->hasUnsavedChanges() ? "PROJECT *" : "PROJECT"; if (button.getButtonText() != label) button.setButtonText(label); }
        MainComponent* owner; juce::TextButton button;
    };
    class MidiClipOverlay final : public juce::Component, private juce::Timer
    {
    public:
        explicit MidiClipOverlay(MainComponent* ownerIn) : owner(ownerIn)
        {
            setInterceptsMouseClicks(true, false);
            owner->addAndMakeVisible(this);
            startTimerHz(30);
        }
        void resized() override {}
        void paint(juce::Graphics& g) override
        {
            const auto notes = owner->midiEngine.getNotesCopy();
            if (notes.empty()) return;
            constexpr float pixelsPerSecond = 80.0f;
            const auto clipWidth = static_cast<float>(owner->midiClipLengthSeconds * pixelsPerSecond);
            auto clip = juce::Rectangle<float>(static_cast<float>(owner->midiClipStartSeconds * pixelsPerSecond), 4.0f,
                                                juce::jmin(juce::jmax(80.0f, clipWidth), static_cast<float>(getWidth())),
                                                static_cast<float>(getHeight() - 8));
            g.setColour(owner->selectedTrack < 0 ? juce::Colour(0xff245b70) : juce::Colour(0xff204756));
            g.fillRoundedRectangle(clip, 5.0f);
            g.setColour(juce::Colour(0xff63c7e8));
            g.drawRoundedRectangle(clip, 5.0f, 1.0f);
            g.saveState();
            g.reduceClipRegion(clip.toNearestInt());
            for (const auto& note : notes)
            {
                const auto x = clip.getX() + static_cast<float>(MidiEngine::tickToSeconds(note.startTick, owner->tempoBpm) * pixelsPerSecond);
                const auto w = juce::jmax(2.0f, static_cast<float>(MidiEngine::tickToSeconds(note.lengthTicks, owner->tempoBpm) * pixelsPerSecond));
                const auto y = clip.getY() + clip.getHeight() * (1.0f - static_cast<float>(note.pitch) / 127.0f);
                const auto noteRight = juce::jmin(clip.getRight(), x + w);
                if (noteRight <= clip.getX() || x >= clip.getRight()) continue;
                g.setColour(juce::Colour(0xffd8f5ff));
                g.fillRoundedRectangle(juce::Rectangle<float>(juce::jmax(clip.getX(), x), juce::jlimit(clip.getY() + 3.0f, clip.getBottom() - 7.0f, y),
                                                               juce::jmax(2.0f, noteRight - juce::jmax(clip.getX(), x)), 4.0f), 2.0f);
            }
            g.restoreState();
            g.setColour(juce::Colour(0xffd8f5ff));
            g.setFont(juce::Font(10.0f, juce::Font::bold));
            g.drawText("MIDI CLIP", clip.reduced(8.0f, 4.0f), juce::Justification::topLeft, true);

            if (owner->selectedTrack < 0)
            {
                g.setColour(juce::Colour(0xffaee8fa));
                g.fillRoundedRectangle(clip.getX(), clip.getY() + 2.0f, 3.0f, clip.getHeight() - 4.0f, 1.5f);
                g.fillRoundedRectangle(clip.getRight() - 3.0f, clip.getY() + 2.0f, 3.0f, clip.getHeight() - 4.0f, 1.5f);
            }
        }
        void mouseMove(const juce::MouseEvent& event) override
        {
            updateCursor(event.position.x);
        }
        void mouseExit(const juce::MouseEvent&) override
        {
            setMouseCursor(juce::MouseCursor::NormalCursor);
        }
        void mouseDown(const juce::MouseEvent& event) override
        {
            const auto notes = owner->midiEngine.getNotesCopy();
            if (notes.empty()) return;
            constexpr float pixelsPerSecond = 80.0f;
            const auto clip = getClipRectangle();
            if (!clip.contains(event.position)) return;

            owner->selectMidiTrack();
            const auto side = getResizeSide(event.position.x, clip);
            if (side != ResizeSide::none)
            {
                resizing = true;
                resizeSide = side;
                dragStartX = event.position.x;
                dragStartSeconds = owner->midiClipStartSeconds;
                dragStartLengthSeconds = owner->midiClipLengthSeconds;
                return;
            }

            dragging = true;
            dragStartX = event.position.x;
            dragStartSeconds = owner->midiClipStartSeconds;
        }
        void mouseDrag(const juce::MouseEvent& event) override
        {
            constexpr float pixelsPerSecond = 80.0f;
            const auto delta = (static_cast<double>(event.position.x) - static_cast<double>(dragStartX)) / pixelsPerSecond;
            const auto secondsPerMeasure = getSecondsPerMeasure();

            if (resizing)
            {
                if (resizeSide == ResizeSide::right)
                {
                    const auto newRight = juce::jmax(dragStartSeconds + getMinimumLengthSeconds(), dragStartSeconds + dragStartLengthSeconds + delta);
                    const auto snappedRight = std::round(newRight / secondsPerMeasure) * secondsPerMeasure;
                    owner->midiClipLengthSeconds = juce::jmax(getMinimumLengthSeconds(), snappedRight - dragStartSeconds);
                }
                else if (resizeSide == ResizeSide::left)
                {
                    const auto originalRight = dragStartSeconds + dragStartLengthSeconds;
                    const auto newStart = juce::jlimit(0.0, originalRight - getMinimumLengthSeconds(), dragStartSeconds + delta);
                    const auto snappedStart = juce::jmax(0.0, std::round(newStart / secondsPerMeasure) * secondsPerMeasure);
                    owner->midiClipStartSeconds = juce::jmin(snappedStart, originalRight - getMinimumLengthSeconds());
                    owner->midiClipLengthSeconds = juce::jmax(getMinimumLengthSeconds(), originalRight - owner->midiClipStartSeconds);
                }
                repaint();
                owner->repaint();
                return;
            }

            if (dragging)
            {
                owner->midiClipStartSeconds = juce::jmax(0.0, std::round((dragStartSeconds + delta) / secondsPerMeasure) * secondsPerMeasure);
                repaint();
                owner->repaint();
            }
        }
        void mouseUp(const juce::MouseEvent&) override
        {
            dragging = false;
            resizing = false;
            resizeSide = ResizeSide::none;
        }
    private:
        enum class ResizeSide { none, left, right };
        static constexpr float resizeZone = 14.0f;

        juce::Rectangle<float> getClipRectangle() const
        {
            constexpr float pixelsPerSecond = 80.0f;
            return juce::Rectangle<float>(static_cast<float>(owner->midiClipStartSeconds * pixelsPerSecond), 4.0f,
                                          juce::jmax(80.0f, static_cast<float>(owner->midiClipLengthSeconds * pixelsPerSecond)),
                                          static_cast<float>(getHeight() - 8));
        }

        double getSecondsPerMeasure() const
        {
            const double secondsPerBeat = 60.0 / juce::jmax(1.0, owner->tempoBpm)
                                         * (4.0 / static_cast<double>(juce::jmax(1, owner->timeSignatureDenominator)));
            return secondsPerBeat * static_cast<double>(juce::jmax(1, owner->timeSignatureNumerator));
        }

        double getMinimumLengthSeconds() const
        {
            return getSecondsPerMeasure();
        }

        ResizeSide getResizeSide(float x, const juce::Rectangle<float>& clip) const
        {
            if (x <= clip.getX() + resizeZone) return ResizeSide::left;
            if (x >= clip.getRight() - resizeZone) return ResizeSide::right;
            return ResizeSide::none;
        }

        void updateCursor(float x)
        {
            const auto clip = getClipRectangle();
            if (!clip.contains(x, static_cast<float>(getHeight() / 2)))
            {
                setMouseCursor(juce::MouseCursor::NormalCursor);
                return;
            }
            const auto side = getResizeSide(x, clip);
            if (side != ResizeSide::none)
                setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            else
                setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        }

        void timerCallback() override
        {
            const auto rowY = 76 + 32 + (4 * 70);
            setBounds(210, rowY, juce::jmax(1, owner->getWidth() - 210), 70);
            repaint();
        }
        MainComponent* owner;
        bool dragging = false;
        bool resizing = false;
        ResizeSide resizeSide = ResizeSide::none;
        float dragStartX = 0.0f;
        double dragStartSeconds = 0.0;
        double dragStartLengthSeconds = 0.0;
    };
    void timerCallback() override; void drawTransport(juce::Graphics&, juce::Rectangle<int>); void drawTrackArea(juce::Graphics&, juce::Rectangle<int>); void drawMixer(juce::Graphics&, juce::Rectangle<int>); void openAudioSettings(); void openAudioFile(); void editTempo(); void editTimeSignature(); void rebuildWaveformCache(int); bool handleMixerMouse(const juce::MouseEvent&); int getAudioTrackAtPosition(juce::Point<int>) const; bool isPointInsideAudioClip(int, juce::Point<int>) const; void showProjectMenu(); void newProject(); void openProject(); void saveProject(); void saveProjectAs(); bool saveProjectToFile(const juce::File&); bool loadProjectFromFile(const juce::File&); void resetProjectState(); void initializeProjectTracking(); juce::String getProjectStateSignature() const; void markProjectClean(); void confirmBeforeProjectAction(std::function<void()> action);
    AudioEngine audioEngine; MidiEngine midiEngine; std::unique_ptr<AudioSettingsWindow> audioSettingsWindow; std::unique_ptr<juce::FileChooser> audioFileChooser; std::unique_ptr<juce::FileChooser> projectFileChooser; std::array<std::vector<float>, AudioEngine::maxAudioTracks> waveformMin; std::array<std::vector<float>, AudioEngine::maxAudioTracks> waveformMax; std::array<juce::File, AudioEngine::maxAudioTracks> trackSourceFiles; juce::File currentProjectFile; juce::String savedProjectStateSignature; std::function<void()> pendingProjectAction; int selectedTrack = 0; bool isPlaying = false; double playheadSeconds = 0.0; double tempoBpm = 120.0; int timeSignatureNumerator = 4; int timeSignatureDenominator = 4; double midiClipStartSeconds = 0.0; double midiClipLengthSeconds = 2.0; TempoControls tempoControls { this }; ProjectButton projectButton { this }; MidiClipOverlay midiClipOverlay { this }; bool draggingClip = false; int draggedTrack = -1; float dragStartMouseX = 0.0f; double dragStartSeconds = 0.0; int mixerDragMode = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};