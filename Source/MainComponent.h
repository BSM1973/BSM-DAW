#pragma once
class MainComponent;
void refreshLibertyMixConsole(MainComponent*);

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "AudioEngine.h"
#include "MidiEngine.h"
#include "StepSequencer.h"
#include <array>
#include <vector>
#include <functional>
#include <cmath>

int getLibertyTrackRowHeight() noexcept;
bool commitLibertySequencerMidiClip(MainComponent&, const std::vector<MidiEngine::NoteEvent>&, int, double, double);

class MainComponent final : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool hasUnsavedChanges() const;
    void requestClose(std::function<void(bool)> completion);
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    MidiEngine& getMidiEngine() noexcept { return midiEngine; }
    const MidiEngine& getMidiEngine() const noexcept { return midiEngine; }
    MidiEngine* getMidiEngine(int track) noexcept
    {
        if (track == 0) return &midiEngine;
        if (track < 0 || track >= dynamicMidiTrackCount || (size_t)(track - 1) >= additionalMidiEngines.size()) return nullptr;
        return additionalMidiEngines[(size_t)(track - 1)].get();
    }
    const MidiEngine* getMidiEngine(int track) const noexcept
    {
        if (track == 0) return &midiEngine;
        if (track < 0 || track >= dynamicMidiTrackCount || (size_t)(track - 1) >= additionalMidiEngines.size()) return nullptr;
        return additionalMidiEngines[(size_t)(track - 1)].get();
    }
    double getTempoBpm() const noexcept { return tempoBpm; }
    int getTimeSignatureNumerator() const noexcept { return timeSignatureNumerator; }
    int getTimeSignatureDenominator() const noexcept { return timeSignatureDenominator; }
    double getAudioCurrentTimeSeconds() const noexcept { return audioEngine.getCurrentTimeSeconds(); }
    bool isAudioPlaying() const noexcept { return audioEngine.isPlaying(); }
    void notifyInstrumentChanged() noexcept { audioEngine.notifyInstrumentChanged(); }
    double getMidiClipLengthSeconds() const noexcept { return midiClipLengthSeconds; }
    double getMidiClipStartSeconds(int track) const noexcept
    {
        if (track == 0) return midiClipStartSeconds;
        if (track < 0 || (size_t)(track - 1) >= additionalMidiClipStartSeconds.size()) return 0.0;
        return additionalMidiClipStartSeconds[(size_t)(track - 1)];
    }
    double getMidiClipLengthSeconds(int track) const noexcept
    {
        if (track == 0) return midiClipLengthSeconds;
        if (track < 0 || (size_t)(track - 1) >= additionalMidiClipLengthSeconds.size()) return 0.0;
        return additionalMidiClipLengthSeconds[(size_t)(track - 1)];
    }
    void selectMidiTrack() noexcept { selectedTrack = -1; repaint(); }
    // Shared drawing entry point for the detachable Step Sequencer window.
    void paintFloatingStepSequencer(juce::Graphics& g, juce::Rectangle<int> area)
    {
        const int instrumentFirst = getAudioTrackCount() + getMidiTrackCount();
        if (selectedTrack >= instrumentFirst
            && selectedTrack < instrumentFirst + getInstrumentTrackCount())
            drawStepSequencerDock(g, area);
        else if (selectedTrack >= getAudioTrackCount() && selectedTrack < instrumentFirst)
            drawMidiStepSequencer(g, area);
        else
        {
            g.fillAll(juce::Colour(0xff101b2a));
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(17.0f, juce::Font::bold));
            g.drawText("STEP SEQUENCER", area.removeFromTop(65), juce::Justification::centred);
            g.setColour(juce::Colour(0xffa3b7cc));
            g.setFont(juce::Font(14.0f));
            g.drawText("Sélectionnez une piste Instrument pour éditer ses Patterns.",
                       area, juce::Justification::centred);
        }
    }
    void drawMidiStepSequencer(juce::Graphics& g, juce::Rectangle<int> area);
    void clickMidiStepSequencer(juce::Point<int> point, juce::Rectangle<int> area);
    void showFloatingStepSequencer();
    void clickFloatingStepSequencer(juce::Point<int> point, juce::Rectangle<int> area);
    void handleStepSequencerClick(juce::Point<int> point, juce::Rectangle<int> area);
    int addAudioTrack()
    {
        const int index = audioEngine.addAudioTrack();
        if (index < 0)
            return -1;
        waveformMin.resize((size_t) audioEngine.getAudioTrackCount());
        waveformMax.resize((size_t) audioEngine.getAudioTrackCount());
        trackSourceFiles.resize((size_t) audioEngine.getAudioTrackCount());
        pendingAudioFileNames.resize((size_t) audioEngine.getAudioTrackCount());
        pendingAudioLengths.resize((size_t) audioEngine.getAudioTrackCount());
        pendingAudioStartSeconds.resize((size_t) audioEngine.getAudioTrackCount());
        pendingAudioWarpStates.resize((size_t) audioEngine.getAudioTrackCount());
        repaint();
        refreshLibertyMixConsole(this);
        return index;
    }
    int getAudioTrackCount() const noexcept { return audioEngine.getAudioTrackCount(); }
    int getMidiTrackCount() const noexcept { return dynamicMidiTrackCount; }
    int getInstrumentTrackCount() const noexcept { return dynamicInstrumentTrackCount; }
    int addMidiTrack() noexcept
    {
        const int i = dynamicMidiTrackCount;
        try
        {
            additionalMidiEngines.push_back(std::make_unique<MidiEngine>());
            additionalMidiClipStartSeconds.push_back(0.0);
            additionalMidiClipLengthSeconds.push_back(2.0);
            additionalMidiClipLengthUserDefined.push_back(false);
            midiStepSequencers.resize((size_t)i + 1);
        }
        catch (...) { return -1; }
        ++dynamicMidiTrackCount;
        repaint(); refreshLibertyMixConsole(this); return i;
    }
    int addInstrumentTrack() noexcept
    {
        const int i = dynamicInstrumentTrackCount;
        if (!audioEngine.ensureInstrumentPlaybackTracks(i + 1)) return -1;
        try { instrumentStepSequencers.resize((size_t)i + 1); } catch (...) { return -1; }
        ++dynamicInstrumentTrackCount;
        repaint(); refreshLibertyMixConsole(this); return i;
    }
    LibertyStepSequencer::Pattern* getInstrumentStepSequencer(int track) noexcept
    {
        if (track < 0 || track >= (int)instrumentStepSequencers.size()) return nullptr;
        auto& bank = instrumentStepSequencers[(size_t)track];
        bank.activePattern = juce::jlimit(0, 7, bank.activePattern);
        return &bank.patterns[(size_t)bank.activePattern];
    }
    bool publishInstrumentStepSequencer(int track) noexcept
    {
        auto* pattern = getInstrumentStepSequencer(track);
        if (pattern == nullptr) return false;
        try
        {
            const auto notes = LibertyStepSequencer::render(*pattern);
            const auto baseStepTicks = juce::jmax<std::int64_t>(1, pattern->stepTicks);
            const auto effectiveStepTicks = pattern->rateModifier == 1 ? juce::jmax<std::int64_t>(1, baseStepTicks * 2 / 3)
                                          : pattern->rateModifier == 2 ? juce::jmax<std::int64_t>(1, baseStepTicks * 3 / 2)
                                          : baseStepTicks;
            const auto lengthTicks = (std::int64_t)juce::jlimit(1, juce::jlimit(1, LibertyStepSequencer::maxSteps, pattern->stepCount), pattern->cycleSteps) * effectiveStepTicks;
            const auto lengthSeconds = MidiEngine::tickToSeconds(lengthTicks, tempoBpm);
            // Editing the step grid only updates audition notes, not previously placed clips.
            audioEngine.setInstrumentTrackNotes(track, notes, 0.0, lengthSeconds, tempoBpm);
            return true;
        }
        catch (...) { return false; }
    }
    void publishInstrumentArrangementClips(int instrumentIndex)
    {
        if (instrumentIndex < 0 || instrumentIndex >= (int) instrumentStepSequencers.size()) return;
        std::vector<AudioEngine::InstrumentArrangementClip> clips;
        for (const auto& patternClip : instrumentStepSequencers[(size_t) instrumentIndex].timelineClips)
        {
            AudioEngine::InstrumentArrangementClip clip;
            clip.startSeconds = patternClip.startSeconds;
            clip.lengthSeconds = patternClip.lengthSeconds;
            clip.notes = patternClip.notes;
            clips.push_back(std::move(clip));
        }
        audioEngine.setInstrumentArrangementClips(instrumentIndex, clips, tempoBpm);
    }
    bool createInstrumentPatternClip(int instrumentIndex) noexcept
    {
        if (instrumentIndex < 0 || instrumentIndex >= (int) instrumentStepSequencers.size()) return false;
        auto* pattern = getInstrumentStepSequencer(instrumentIndex);
        if (pattern == nullptr) return false;
        const auto notes = LibertyStepSequencer::render(*pattern);
        // The project loader restores at most 8192 notes per Pattern clip.
        // Reject oversized renders instead of silently truncating on reopen.
        if (notes.empty() || notes.size() > 8192) return false;
        const auto lengthTicks = LibertyStepSequencer::getCycleLengthTicks(*pattern);
        if (lengthTicks <= 0) return false;
        auto& bank = instrumentStepSequencers[(size_t) instrumentIndex];
        // Keep the same bound as project loading. Without this guard, a long
        // session could create clips that would silently disappear on reopen.
        if (bank.timelineClips.size() >= 2048) return false;
        InstrumentStepSequencerBank::TimelinePatternClip clip;
        // Use the same integer MIDI-tick snapping as Pattern drag/move.
        constexpr auto sixteenthTicks = MidiEngine::ticksPerQuarterNote / 4;
        const auto playheadTick = juce::jmax<std::int64_t>(
            0, MidiEngine::secondsToTick(juce::jmax(0.0, playheadSeconds), tempoBpm));
        const auto snappedTick = ((playheadTick + sixteenthTicks / 2) / sixteenthTicks)
                                 * sixteenthTicks;
        clip.startSeconds = MidiEngine::tickToSeconds(snappedTick, tempoBpm);
        clip.lengthSeconds = juce::jmax(0.001, MidiEngine::tickToSeconds(lengthTicks, tempoBpm));
        clip.name = "Pattern " + juce::String(bank.activePattern + 1);
        clip.notes = notes;
        bank.timelineClips.push_back(std::move(clip));
        publishInstrumentArrangementClips(instrumentIndex);
        repaint();
        return true;
    }
    int getTrackScrollRows() const noexcept { return trackScrollRows; }
    void setTrackScrollRows(int rows) noexcept { trackScrollRows = juce::jmax(0, rows); repaint(); }
    int getTotalArrangeTrackCount() const noexcept { return getAudioTrackCount() + dynamicMidiTrackCount + dynamicInstrumentTrackCount; }
    static constexpr int transportHeight = 76;
    static constexpr int trackRulerHeight = 32;
    static constexpr int mixerHeight = 246;
    static constexpr int trackHeaderWidth = 210;
    int getArrangeTop() const noexcept { return transportHeight + trackRulerHeight; }
    int getMixerTop() const noexcept { return juce::jmax(getArrangeTop(), getHeight() - mixerHeight); }
    juce::Rectangle<int> getArrangeRowsBounds() const noexcept
    {
        return { 0, getArrangeTop(), getWidth(), juce::jmax(0, getMixerTop() - getArrangeTop()) };
    }

    void saveStepSequencers(juce::XmlElement& root) const;
    void loadStepSequencers(const juce::XmlElement& root);

    bool commitInstrumentStepSequencerToMidiClip(int instrumentIndex, int midiTrack = 0)
    {
        const auto* pattern = getInstrumentStepSequencer(instrumentIndex);
        if (pattern == nullptr || midiTrack < 0 || midiTrack >= getMidiTrackCount()) return false;
        auto notes = LibertyStepSequencer::render(*pattern, 0);
        if (notes.empty()) return false;
        const auto lengthTicks = LibertyStepSequencer::getCycleLengthTicks(*pattern);
        if (lengthTicks <= 0) return false;
        const auto lengthSeconds = juce::jmax(0.001, MidiEngine::tickToSeconds(lengthTicks, tempoBpm));
        return commitLibertySequencerMidiClip(*this, notes, midiTrack, playheadSeconds, lengthSeconds);
    }

    void updateMidiClipTiming() noexcept
    {
        const auto notes = midiEngine.getNotesCopy();
        if (notes.empty())
        {
            audioEngine.setMidiNotes(notes, midiClipStartSeconds, midiClipLengthSeconds, tempoBpm);
            audioEngine.setProjectExtraLengthSeconds(0.0);
            return;
        }
        const double secondsPerBeat = 60.0 / juce::jmax(1.0, tempoBpm) * (4.0 / static_cast<double>(juce::jmax(1, timeSignatureDenominator)));
        const double secondsPerMeasure = secondsPerBeat * static_cast<double>(juce::jmax(1, timeSignatureNumerator));
        double noteEndSeconds = 0.0;
        for (const auto& note : notes)
            noteEndSeconds = juce::jmax(noteEndSeconds, MidiEngine::tickToSeconds(note.startTick + note.lengthTicks, tempoBpm));
        if (!midiClipLengthUserDefined)
        {
            const auto requiredMeasures = std::ceil(juce::jmax(secondsPerMeasure, noteEndSeconds) / secondsPerMeasure);
            const auto requiredLength = juce::jmax(secondsPerMeasure, requiredMeasures * secondsPerMeasure);
            if (requiredLength > midiClipLengthSeconds + 0.000001)
                midiClipLengthSeconds = requiredLength;
        }
        audioEngine.setMidiNotes(notes, midiClipStartSeconds, midiClipLengthSeconds, tempoBpm);
        audioEngine.setProjectExtraLengthSeconds(midiClipStartSeconds + midiClipLengthSeconds);
    }

    void setMidiClipLengthFromProject(double lengthSeconds) noexcept
    {
        midiClipLengthSeconds = juce::jmax(0.0, lengthSeconds);
        midiClipLengthUserDefined = true;
    }

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
            setBounds(605, 34, 120, 36); owner->addAndMakeVisible(this);
        }
        void refresh() { tempoButton.setButtonText(juce::String(owner->tempoBpm, 2) + " BPM"); meterButton.setButtonText(juce::String(owner->timeSignatureNumerator) + "/" + juce::String(owner->timeSignatureDenominator)); repaint(); }
        void resized() override { tempoButton.setBounds(0, 0, 80, 36); meterButton.setBounds(80, 0, 40, 36); }
    private: MainComponent* owner; juce::TextButton tempoButton; juce::TextButton meterButton;
    };
    class ProjectButton final : public juce::Component
    {
    public:
        explicit ProjectButton(MainComponent* ownerIn) : owner(ownerIn)
        {
            setOpaque(true);
            button.setButtonText("PROJECT"); button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31)); button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff303640)); button.setColour(juce::TextButton::textColourOffId, juce::Colours::white); button.setColour(juce::TextButton::textColourOnId, juce::Colours::white); button.setMouseClickGrabsKeyboardFocus(false); button.onClick = [this] { owner->showProjectMenu(); }; addAndMakeVisible(button); setBounds(215, 10, 90, 24); owner->addAndMakeVisible(this); owner->initializeProjectTracking(); updateLabel();
        }
        void resized() override { button.setBounds(getLocalBounds()); }
        void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff15181d)); }
    public:
        void updateLabel() { const auto label = owner->hasUnsavedChanges() ? "PROJECT *" : "PROJECT"; if (button.getButtonText() != label) button.setButtonText(label); }
    private:
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
            g.setColour(juce::Colour(0xff63c7e8)); g.drawRoundedRectangle(clip, 5.0f, 1.0f);
            g.saveState(); g.reduceClipRegion(clip.toNearestInt());
            for (const auto& note : notes)
            {
                const auto x = clip.getX() + static_cast<float>(MidiEngine::tickToSeconds(note.startTick, owner->tempoBpm) * pixelsPerSecond);
                const auto w = juce::jmax(2.0f, static_cast<float>(MidiEngine::tickToSeconds(note.lengthTicks, owner->tempoBpm) * pixelsPerSecond));
                const auto y = clip.getY() + clip.getHeight() * (1.0f - static_cast<float>(note.pitch) / 127.0f);
                const auto noteRight = juce::jmin(clip.getRight(), x + w);
                if (noteRight <= clip.getX() || x >= clip.getRight()) continue;
                g.setColour(juce::Colour(0xffd8f5ff));
                g.fillRoundedRectangle(juce::Rectangle<float>(juce::jmax(clip.getX(), x), juce::jlimit(clip.getY() + 3.0f, clip.getBottom() - 7.0f, y), juce::jmax(2.0f, noteRight - juce::jmax(clip.getX(), x)), 4.0f), 2.0f);
            }
            g.restoreState();
            g.setColour(juce::Colour(0xffd8f5ff)); g.setFont(juce::Font(10.0f, juce::Font::bold)); g.drawText("MIDI CLIP", clip.reduced(8.0f, 4.0f), juce::Justification::topLeft, true);
            if (owner->selectedTrack < 0)
            {
                g.setColour(juce::Colour(0xffaee8fa));
                g.fillRoundedRectangle(clip.getX(), clip.getY() + 2.0f, 3.0f, clip.getHeight() - 4.0f, 1.5f);
                g.fillRoundedRectangle(clip.getRight() - 3.0f, clip.getY() + 2.0f, 3.0f, clip.getHeight() - 4.0f, 1.5f);
            }
        }
        void mouseMove(const juce::MouseEvent& event) override { updateCursor(event.position.x); }
        void mouseExit(const juce::MouseEvent&) override { setMouseCursor(juce::MouseCursor::NormalCursor); }
        void mouseDown(const juce::MouseEvent& event) override
        {
            const auto notes = owner->midiEngine.getNotesCopy();
            if (notes.empty()) return;
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
                owner->midiClipLengthUserDefined = true;
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
                repaint(); owner->repaint(); return;
            }
            if (dragging)
            {
                owner->midiClipStartSeconds = juce::jmax(0.0, std::round((dragStartSeconds + delta) / secondsPerMeasure) * secondsPerMeasure);
                repaint(); owner->repaint();
            }
        }
        void mouseUp(const juce::MouseEvent&) override { dragging = false; resizing = false; resizeSide = ResizeSide::none; }
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
            const double secondsPerBeat = 60.0 / juce::jmax(1.0, owner->tempoBpm) * (4.0 / static_cast<double>(juce::jmax(1, owner->timeSignatureDenominator)));
            return secondsPerBeat * static_cast<double>(juce::jmax(1, owner->timeSignatureNumerator));
        }
        double getMinimumLengthSeconds() const { return getSecondsPerMeasure(); }
        ResizeSide getResizeSide(float x, const juce::Rectangle<float>& clip) const
        {
            if (x <= clip.getX() + resizeZone) return ResizeSide::left;
            if (x >= clip.getRight() - resizeZone) return ResizeSide::right;
            return ResizeSide::none;
        }
        void updateCursor(float x)
        {
            const auto clip = getClipRectangle();
            if (!clip.contains(x, static_cast<float>(getHeight() / 2))) { setMouseCursor(juce::MouseCursor::NormalCursor); return; }
            const auto side = getResizeSide(x, clip);
            setMouseCursor(side != ResizeSide::none ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
        }
        void timerCallback() override
        {
            owner->updateMidiClipTiming();
            const int rowH = getLibertyTrackRowHeight();
            const auto rowY = 76 + 32 + ((owner->getAudioTrackCount() - owner->getTrackScrollRows()) * rowH);
            setBounds(210, rowY, juce::jmax(1, owner->getWidth() - 210), rowH);
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
    void timerCallback() override; void drawTransport(juce::Graphics&, juce::Rectangle<int>); void drawTrackArea(juce::Graphics&, juce::Rectangle<int>); void drawStepSequencerDock(juce::Graphics&, juce::Rectangle<int>); void drawMixer(juce::Graphics&, juce::Rectangle<int>); void openAudioSettings(); void editTempo(); void editTimeSignature(); void rebuildWaveformCache(int); bool handleMixerMouse(const juce::MouseEvent&); int getAudioTrackAtPosition(juce::Point<int>) const; bool isPointInsideAudioClip(int, juce::Point<int>) const; void showProjectMenu(); void newProject(); void openProject(); void saveProject(); void saveProjectAs(); bool saveProjectToFile(const juce::File&); bool loadProjectFromFile(const juce::File&); bool resetProjectState(); void initializeProjectTracking(); juce::String getProjectStateSignature() const; void markProjectClean(); void confirmBeforeProjectAction(std::function<void()> action);
    struct PendingAudioWarpState
    {
        bool enabled = false;
        int mode = 0;
        std::vector<std::pair<double, double>> markers;
    };

    AudioEngine audioEngine; MidiEngine midiEngine;
    std::vector<std::unique_ptr<MidiEngine>> additionalMidiEngines;
    std::vector<double> additionalMidiClipStartSeconds;
    std::vector<double> additionalMidiClipLengthSeconds;
    std::vector<bool> additionalMidiClipLengthUserDefined;
    struct InstrumentStepSequencerBank
    {
        std::array<LibertyStepSequencer::Pattern, 8> patterns {};
        int activePattern = 0;
        struct TimelinePatternClip
        {
            double startSeconds = 0.0;
            double lengthSeconds = 0.0;
            juce::String name = "Pattern";
            std::vector<MidiEngine::NoteEvent> notes;
        };
        std::vector<TimelinePatternClip> timelineClips;
    };
    // MIDI tracks keep independent Pattern banks; they never alias instrument banks.
    struct MidiStepSequencerBank
    {
        std::array<LibertyStepSequencer::Pattern, 8> patterns {};
        int activePattern = 0;
    };
    std::vector<MidiStepSequencerBank> midiStepSequencers;
    LibertyStepSequencer::Pattern* getMidiStepSequencer(int track) noexcept
    {
        if (track < 0 || track >= (int)midiStepSequencers.size()) return nullptr;
        auto& bank = midiStepSequencers[(size_t)track];
        bank.activePattern = juce::jlimit(0, 7, bank.activePattern);
        return &bank.patterns[(size_t)bank.activePattern];
    }
    std::vector<InstrumentStepSequencerBank> instrumentStepSequencers; bool dockStepSequencerMode = true; int stepSequencerPage = 0; int stepSequencerSelectedStep = 0; int stepSequencerMidiTarget = 0; bool draggingStepSequencerPattern = false; int draggedStepSequencerInstrument = -1; std::unique_ptr<AudioSettingsWindow> audioSettingsWindow; std::unique_ptr<juce::FileChooser> projectFileChooser; std::vector<std::vector<float>> waveformMin; std::vector<std::vector<float>> waveformMax; std::vector<juce::File> trackSourceFiles; std::vector<juce::String> pendingAudioFileNames; std::vector<double> pendingAudioLengths; std::vector<double> pendingAudioStartSeconds; std::vector<PendingAudioWarpState> pendingAudioWarpStates; juce::File currentProjectFile; juce::String savedProjectStateSignature; std::function<void()> pendingProjectAction; int selectedTrack = 0; int dynamicMidiTrackCount = 1; int dynamicInstrumentTrackCount = 1; int trackScrollRows = 0; bool isPlaying = false; double playheadSeconds = 0.0; double tempoBpm = 120.0; int timeSignatureNumerator = 4; int timeSignatureDenominator = 4; double midiClipStartSeconds = 0.0; double midiClipLengthSeconds = 2.0; bool midiClipLengthUserDefined = false; TempoControls tempoControls { this }; std::unique_ptr<ProjectButton> projectButton; MidiClipOverlay midiClipOverlay { this }; bool draggingClip = false; int draggedTrack = -1; int draggedPatternInstrument = -1; int draggedPatternClip = -1; float dragStartMouseX = 0.0f; double dragStartSeconds = 0.0; int mixerDragMode = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};