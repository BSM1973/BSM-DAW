#include <map>
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
class MainComponent;
void refreshLibertyRecordingTrackLayout(MainComponent* owner);
void refreshLibertyTrackHeaderMixLayout(MainComponent* owner);
bool commitLibertySequencerMidiClip(MainComponent&, const std::vector<MidiEngine::NoteEvent>&, int, double, double);

class MainComponent final : public juce::Component,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer,
                            private juce::ScrollBar::Listener
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
    void performMainMenuAction(int commandId);
    void auditionPianoKey(int lane, int pitch, bool on) { audioEngine.sendPianoPreviewMidi(lane, on ? juce::MidiMessage::noteOn(1, pitch, (juce::uint8)100) : juce::MidiMessage::noteOff(1, pitch)); }
    int getSelectedPianoInstrumentLane() const noexcept { return selectedTrack - getAudioTrackCount() - getMidiTrackCount(); }
    void openRecentProject(const juce::File& file, std::function<void()> onOpened);
    void updateLoopPunchButtons();
    double loopStartSeconds = 0.0, loopEndSeconds = 8.0;
    double punchInSeconds = 0.0, punchOutSeconds = 8.0;
    bool loopEnabled = false, punchInEnabled = false, punchOutEnabled = false;
    bool punchRecordingRequested = false, punchWaitingForIn = false;
    int draggingRangeMarker = 0;
    double loopDragAnchorSeconds = 0.0, loopDragInitialStartSeconds = 0.0;
    double loopDragInitialEndSeconds = 0.0;
    juce::TextButton punchInButton { "IN" }, loopButton { "LOOP" }, punchOutButton { "OUT" };
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
    int floatingStepSequencerTrack = -1;
    void paintFloatingStepSequencer(juce::Graphics& g, juce::Rectangle<int> area)
    {
        // The detached editor owns a snapshot of its source track. Track
        // selection in ARRANGE must not turn an open editor into an empty view.
        const int previousTrack = selectedTrack;
        const int firstInstrument = getAudioTrackCount() + getMidiTrackCount();
        if (floatingStepSequencerTrack >= getAudioTrackCount()
            && floatingStepSequencerTrack < firstInstrument + getInstrumentTrackCount())
            selectedTrack = floatingStepSequencerTrack;
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
        selectedTrack = previousTrack;
    }
    void paintOverChildren(juce::Graphics& g) override;
    juce::TextButton stepSequencerDockButton { "STEP SEQ" };
    void drawMidiStepSequencer(juce::Graphics& g, juce::Rectangle<int> area);
    void clickMidiStepSequencer(juce::Point<int> point, juce::Rectangle<int> area);
    void showFloatingStepSequencer();
    void clickFloatingStepSequencer(juce::Point<int> point, juce::Rectangle<int> area);
    void handleStepSequencerClick(juce::Point<int> point, juce::Rectangle<int> area);
    bool configureStartupTracks(int audioCount, int instrumentCount);
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
    juce::String getTrackDisplayName(int logical) const
    {
        auto it = customTrackNames.find(logical);
        if (it != customTrackNames.end()) return it->second;
        if (logical < getAudioTrackCount()) return "Audio " + juce::String(logical + 1);
        if (logical < getAudioTrackCount() + getMidiTrackCount()) return "MIDI " + juce::String(logical - getAudioTrackCount() + 1);
        return "Instrument " + juce::String(logical - getAudioTrackCount() - getMidiTrackCount() + 1);
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
    // Per-track heights are indexed by the logical arranger track order.
    // A zero entry inherits the global default height.
    std::vector<int> individualTrackHeights;
    int getArrangeTrackHeight(int logicalRow) const noexcept
    {
        if (logicalRow >= 0 && (size_t) logicalRow < individualTrackHeights.size()
            && individualTrackHeights[(size_t) logicalRow] > 0)
            return individualTrackHeights[(size_t) logicalRow];
        return getLibertyTrackRowHeight();
    }
    void setArrangeTrackHeight(int logicalRow, int pixels)
    {
        if (logicalRow < 0 || logicalRow >= getTotalArrangeTrackCount()) return;
        if ((size_t) logicalRow >= individualTrackHeights.size())
            individualTrackHeights.resize((size_t) logicalRow + 1, 0);
        // Drag below the collapse threshold to retain only the first header row.
        individualTrackHeights[(size_t) logicalRow] = pixels < 92 ? 38 : juce::jlimit(144, 240, pixels);
        refreshLibertyRecordingTrackLayout(this);
        refreshLibertyTrackHeaderMixLayout(this);
        repaint();
    }
    // A global preset applies to every track; individual edge resizing remains available.
    void applyArrangeTrackHeightPreset()
    {
        individualTrackHeights.clear();
        refreshLibertyRecordingTrackLayout(this);
        refreshLibertyTrackHeaderMixLayout(this);
        repaint();
    }
    int getArrangeTrackTop(int logicalRow) const noexcept
    {
        int y = getArrangeTop();
        const int first = getTrackScrollRows();
        // Scrolled-out tracks must remain above the viewport, not collapse
        // onto the first visible row and stack their native controls there.
        if (logicalRow < first)
        {
            for (int row = logicalRow; row < first; ++row)
                y -= getArrangeTrackHeight(row);
        }
        else
        {
            for (int row = first; row < logicalRow; ++row)
                y += getArrangeTrackHeight(row);
        }
        return y;
    }
    int getArrangeTrackAtY(int y) const noexcept
    {
        if (y < getArrangeTop() || y >= getMixerTop()) return -1;
        int top = getArrangeTop();
        for (int row = getTrackScrollRows(); row < getTotalArrangeTrackCount(); ++row)
        {
            top += getArrangeTrackHeight(row);
            if (y < top) return row;
        }
        return -1;
    }
    bool draggingPlayheadMarker = false;
    int getTrackScrollRows() const noexcept { return trackScrollRows; }
    void setTrackScrollRows(int rows) noexcept
    {
        const int next = juce::jlimit(0, juce::jmax(0, getTotalArrangeTrackCount() - 1), rows);
        if (trackScrollRows == next) return;
        trackScrollRows = next;
        // Native track-header controls must follow the scroll synchronously,
        // not on the next periodic timer tick.
        refreshLibertyTrackHeaderMixLayout(this);
        repaint();
    }
    int getTotalArrangeTrackCount() const noexcept { return getAudioTrackCount() + dynamicMidiTrackCount + dynamicInstrumentTrackCount; }
    static constexpr int transportHeight = 76;
    static constexpr int trackRulerHeight = 32;
    struct DockResizeHandle final : juce::Component
    {
        std::function<void(int)> onDragDelta;
        int startScreenY = 0;
        std::function<void()> onStart;
        void mouseEnter(const juce::MouseEvent&) override { setMouseCursor(juce::MouseCursor::UpDownResizeCursor); }
        void mouseMove(const juce::MouseEvent&) override { setMouseCursor(juce::MouseCursor::UpDownResizeCursor); }
        void mouseDown(const juce::MouseEvent& e) override
        {
            startScreenY = e.getScreenY();
            if (onStart) onStart();
            setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        }
        void mouseDrag(const juce::MouseEvent& e) override
        {
            if (onDragDelta) onDragDelta(startScreenY - e.getScreenY());
        }
    };
    // Dedicated input surface above full-window overlays. It intercepts only
    // the actual mini-mixer strips, leaving editor tabs and dock resizing free.
    class DockMixerInputLayer final : public juce::Component
    {
    public:
        explicit DockMixerInputLayer(MainComponent& parent) : owner(parent) { setInterceptsMouseClicks(true, false); }
        bool hitTest(int x, int y) override
        {
            if (!owner.dockMixerMode)
                return false;
            const int count = owner.getAudioTrackCount() + owner.getInstrumentTrackCount();
            for (int i = 0; i < count; ++i)
                if (juce::Rectangle<int>(220 + i * 125 - owner.mixerHorizontalOffset, 12, 116, juce::jmax(1, getHeight() - 22)).contains(x, y))
                    return true;
            return false;
        }
        void mouseDown(const juce::MouseEvent& e) override { owner.handleMixerMouse(e.getEventRelativeTo(&owner)); }
        void mouseDrag(const juce::MouseEvent& e) override
        {
            // Mute/Solo are click actions, not continuous drag actions.
            const auto down = e.getMouseDownPosition();
            const int localY = down.y;
            if (localY >= 44 && localY < 64) return;
            owner.handleMixerMouse(e.getEventRelativeTo(&owner));
        }
    private:
        MainComponent& owner;
    };
    // One dock navigation hit layer: same 32px-spaced geometry as paint().
    // A single owner receives the clicks instead of separate painted and
    // interactive elements whose positions can drift apart.
    void activateDockTab(int index);
    class DockNavigationHitLayer final : public juce::Component
    {
    public:
        explicit DockNavigationHitLayer(MainComponent& parent) : owner(parent)
        {
            setInterceptsMouseClicks(true, false);
        }
        bool hitTest(int x, int y) override
        {
            if (owner.dockCollapsed || x < 8 || x >= 156) return false;
            for (int i = 0; i < 4; ++i)
                if (y >= 12 + i * 32 && y < 40 + i * 32) return true;
            return false;
        }
        void mouseDown(const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) return;
            const int index = (e.y - 12) / 32;
            if (index >= 0 && index < 4) owner.activateDockTab(index);
        }
    private:
        MainComponent& owner;
    };
    DockNavigationHitLayer dockNavigationHitLayer { *this };
    DockMixerInputLayer dockMixerInputLayer { *this };
    juce::ScrollBar mixerHorizontalScrollBar { false };
    int mixerHorizontalOffset = 0;
    void scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) override
    {
        if (bar == &mixerHorizontalScrollBar)
        {
            mixerHorizontalOffset = juce::jmax(0, (int) std::lround(newRangeStart));
            repaint();
        }
    }
    void updateMixerHorizontalScrollBar()
    {
        const int channelCount = getAudioTrackCount() + getInstrumentTrackCount() + 1;
        const int viewportWidth = juce::jmax(1, getWidth() - 220);
        const int contentWidth = channelCount * 125;
        const int maximumOffset = juce::jmax(0, contentWidth - viewportWidth);
        mixerHorizontalOffset = juce::jlimit(0, maximumOffset, mixerHorizontalOffset);
        const bool show = dockMixerMode && maximumOffset > 0;
        mixerHorizontalScrollBar.setVisible(show);
        mixerHorizontalScrollBar.setBounds(220, getHeight() - 13,
                                          juce::jmax(0, getWidth() - 228), 9);
        mixerHorizontalScrollBar.setRangeLimits(0.0, (double)juce::jmax(contentWidth, viewportWidth));
        mixerHorizontalScrollBar.setCurrentRange((double)mixerHorizontalOffset, (double)viewportWidth,
                                                juce::dontSendNotification);
        mixerHorizontalScrollBar.setSingleStepSize(125.0);
        if (show) mixerHorizontalScrollBar.toFront(false);
    }

    DockResizeHandle dockResizeHandle;
    int mixerHeight = 492;
    bool dockCollapsed = false;
    juce::TextButton dockVisibilityButton;
    bool draggingDockHeight = false;
    int dockDragStartY = 0;
    int dockDragStartHeight = 492;
    static constexpr int trackHeaderWidth = 330;
    int getArrangeTop() const noexcept { return transportHeight + trackRulerHeight; }
    int getMixerTop() const noexcept { return juce::jmax(getArrangeTop(), getHeight() - (dockCollapsed ? 32 : mixerHeight)); }
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
    int headerGainDragTrack = -1;
    class AudioSettingsWindow final : public juce::DocumentWindow
    {
        class AudioSettingsContent final : public juce::Component, private juce::Timer
        {
        class AudioControlsLookAndFeel final : public juce::LookAndFeel_V4
        {
        public:
            AudioControlsLookAndFeel()
            {
                setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1d2935));
                setColour(juce::ComboBox::textColourId, juce::Colour(0xfff0f6f8));
                setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff314553));
                setColour(juce::ComboBox::arrowColourId, juce::Colour(0xff65e0dc));
                setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1d2935));
                setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff286674));
                setColour(juce::TextButton::textColourOffId, juce::Colour(0xfff0f6f8));
                setColour(juce::TextButton::textColourOnId, juce::Colours::white);
                setColour(juce::Label::textColourId, juce::Colour(0xffd6e4ed));
                setColour(juce::ToggleButton::textColourId, juce::Colour(0xffd6e4ed));
                setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff141e28));
                setColour(juce::ListBox::outlineColourId, juce::Colour(0xff314553));
                setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff1d2935));
                setColour(juce::PopupMenu::textColourId, juce::Colour(0xfff0f6f8));
                setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff286674));
                setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
                setColour(juce::ScrollBar::thumbColourId, juce::Colour(0xff427e89));
                setColour(juce::ScrollBar::trackColourId, juce::Colour(0xff141e28));

            }

            void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                      const juce::Colour&, bool isOver, bool isDown) override
            {
                const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
                const auto fill = isDown ? juce::Colour(0xff286674)
                    : isOver ? juce::Colour(0xff263c49) : juce::Colour(0xff1d2935);
                g.setColour(fill);
                g.fillRoundedRectangle(bounds, 7.0f);
                g.setColour(juce::Colour(button.hasKeyboardFocus(true)
                    ? 0xff65e0dc : 0xff314553));
                g.drawRoundedRectangle(bounds, 7.0f, 1.0f);
            }

            void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                  bool isOver, bool isDown) override
            {
                const auto area = button.getLocalBounds().toFloat();
                const float side = juce::jmin(18.0f, juce::jmax(0.0f, area.getHeight() - 4.0f));
                const juce::Rectangle<float> check(2.0f, (area.getHeight() - side) * 0.5f,
                                                    side, side);
                g.setColour(button.getToggleState() ? juce::Colour(0xff286674)
                    : juce::Colour(isDown ? 0xff263c49 : 0xff1d2935));
                g.fillRoundedRectangle(check, 4.0f);
                g.setColour(juce::Colour(isOver || button.hasKeyboardFocus(true)
                    ? 0xff65e0dc : 0xff516878));
                g.drawRoundedRectangle(check.reduced(0.5f), 4.0f, 1.3f);
                if (button.getToggleState())
                {
                    juce::Path tick;
                    tick.startNewSubPath(check.getX() + 4.0f, check.getCentreY());
                    tick.lineTo(check.getX() + 7.5f, check.getBottom() - 4.0f);
                    tick.lineTo(check.getRight() - 3.0f, check.getY() + 4.0f);
                    g.setColour(juce::Colour(0xfff0f6f8));
                    g.strokePath(tick, juce::PathStrokeType(1.8f));
                }
                g.setColour(button.findColour(juce::ToggleButton::textColourId));
                g.setFont(juce::Font(14.0f));
                g.drawFittedText(button.getButtonText(),
                    juce::Rectangle<int>((int) (check.getRight() + 8.0f), 0,
                        juce::jmax(0, button.getWidth() - (int) check.getRight() - 10),
                        button.getHeight()),
                    juce::Justification::centredLeft, 1);
            }

            void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                              int, int, int, int, juce::ComboBox& box) override
            {
                auto bounds = juce::Rectangle<float>(0.5f, 0.5f,
                    (float) juce::jmax(0, width - 1), (float) juce::jmax(0, height - 1));
                g.setColour(juce::Colour(isButtonDown ? 0xff263c49 : 0xff1d2935));
                g.fillRoundedRectangle(bounds, 7.0f);
                g.setColour(juce::Colour(box.hasKeyboardFocus(true) ? 0xff65e0dc : 0xff314553));
                g.drawRoundedRectangle(bounds, 7.0f, 1.2f);
                const float cx = (float) width - 17.0f;
                const float cy = (float) height * 0.5f;
                juce::Path arrow;
                arrow.startNewSubPath(cx - 4.0f, cy - 2.0f);
                arrow.lineTo(cx, cy + 2.0f);
                arrow.lineTo(cx + 4.0f, cy - 2.0f);
                g.setColour(juce::Colour(0xff65e0dc));
                g.strokePath(arrow, juce::PathStrokeType(1.8f,
                    juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        };
        static void translateAudioControls(juce::Component& parent)
        {
            for (int i = 0; i < parent.getNumChildComponents(); ++i)
            {
                if (auto* child = parent.getChildComponent(i))
                {
                    if (auto* label = dynamic_cast<juce::Label*>(child))
                    {
                        const auto original = label->getText().trim();
                        if (original == "Output:")
                            label->setText("Sortie :", juce::dontSendNotification);
                        else if (original == "Input:")
                            label->setText(juce::String("Entr")
                                + juce::String::charToString((juce::juce_wchar) 0x00E9)
                                + "e :", juce::dontSendNotification);
                        else if (original == "Sample rate:")
                            label->setText(juce::String("Fr")
                                + juce::String::charToString((juce::juce_wchar) 0x00E9)
                                + "quence :", juce::dontSendNotification);
                        else if (original == "Audio buffer size:")
                            label->setText("Taille du buffer :", juce::dontSendNotification);
                        else if (original == "MIDI Output:")
                            label->setText("Sortie MIDI :", juce::dontSendNotification);
                        else if (original == "Active output channels:")
                            label->setText("Canaux de sortie :", juce::dontSendNotification);
                        else if (original == "Active input channels:")
                            label->setText(juce::String("Canaux d'entr")
                                + juce::String::charToString((juce::juce_wchar) 0x00E9)
                                + "e :", juce::dontSendNotification);
                        else if (original == "MIDI Input:")
                            label->setText(juce::String("Entr")
                                + juce::String::charToString((juce::juce_wchar) 0x00E9)
                                + "e MIDI :", juce::dontSendNotification);
                    }
                    else if (auto* button = dynamic_cast<juce::TextButton*>(child))
                    {
                        if (button->getButtonText() == "Test")
                            button->setButtonText("Tester");
                    }
                    translateAudioControls(*child);
                }
            }
        }
        AudioControlsLookAndFeel controlsLookAndFeel;
        public:
            explicit AudioSettingsContent(AudioEngine& engine) : audioEngine(engine)
            {
                midiInputLabel.setText(juce::String("Entr") + juce::String::charToString((juce::juce_wchar) 0x00E9) + "e MIDI :", juce::dontSendNotification);
                midiInputLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe4f0f5));
                addAndMakeVisible(midiInputLabel);
                midiInputBox.setLookAndFeel(&controlsLookAndFeel);
                midiInputBox.onChange = [this]
                {
                    const int index = midiInputBox.getSelectedId() - 1;
                    if (index >= 0 && index < (int)midiInputIdentifiers.size())
                        audioEngine.selectMidiInput(midiInputIdentifiers[(size_t)index]);
                };
                addAndMakeVisible(midiInputBox);
                midiOutputLabel.setText("Sortie MIDI :", juce::dontSendNotification);
                addAndMakeVisible(midiOutputLabel);
                midiOutputBox.setLookAndFeel(&controlsLookAndFeel);
                midiOutputBox.onChange = [this]
                {
                    const int index = midiOutputBox.getSelectedId() - 1;
                    if (index >= 0 && index < (int)midiOutputIdentifiers.size())
                        audioEngine.getDeviceManager().setDefaultMidiOutputDevice(midiOutputIdentifiers[(size_t)index]);
                };
                addAndMakeVisible(midiOutputBox);
                refreshMidiInputs();
                refreshMidiOutputs();
                selector = std::make_unique<juce::AudioDeviceSelectorComponent>(
                    engine.getDeviceManager(), 0, 2, 1, 2, false, false, false, false);
                selector->setLookAndFeel(&controlsLookAndFeel);
                // Localise only known static labels in the JUCE selector.
                // Device names and dynamic values must never be modified.
                translateAudioControls(*selector);
                viewport.setViewedComponent(selector.get(), false);
                viewport.setScrollBarsShown(true, false);
                viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::nonHover);
                viewport.setScrollBarThickness(9);
                viewport.setLookAndFeel(&controlsLookAndFeel);
                addAndMakeVisible(viewport);
                setSize(760, 650);
                startTimerHz(1);
            }

            ~AudioSettingsContent() override
            {
                // Disconnect before the look-and-feel member is destroyed.
                stopTimer();
                viewport.setViewedComponent(nullptr, false);
                viewport.setLookAndFeel(nullptr);
                selector->setLookAndFeel(nullptr);
                midiInputBox.setLookAndFeel(nullptr);
                midiOutputBox.setLookAndFeel(nullptr);
            }

            void paint(juce::Graphics& g) override
            {
                g.fillAll(juce::Colour(0xff0f141b));
                const int w = getWidth();
                g.setColour(juce::Colour(0xff65e0dc));
                // Sliders icon, entirely inside its own 28px-wide region.
                for (int i = 0; i < 2; ++i)
                {
                    const float y = 31.0f + (float)i * 12.0f;
                    g.drawLine(20.0f, y, 43.0f, y, 2.0f);
                    g.fillEllipse((i == 0 ? 32.0f : 25.0f), y - 3.0f, 6.0f, 6.0f);
                }
                g.setColour(juce::Colour(0xfff0f6f8));
                g.setFont(juce::Font(22.0f, juce::Font::plain));
                // Use a Unicode code point for È, independent of source-file encoding.
                const juce::String heading = juce::String("PARAM")
                    + juce::String::charToString((juce::juce_wchar) 0x00C8) + "TRES AUDIO ET MIDI";
                g.drawFittedText(heading, 55, 17, juce::jmax(0, w - 70), 43,
                                 juce::Justification::centredLeft, 1);

                const auto panel = juce::Rectangle<float>(18.0f, 80.0f,
                                                           (float)juce::jmax(0, w - 36), 80.0f);
                g.setColour(juce::Colour(0xff1d2935));
                g.fillRoundedRectangle(panel, 11.0f);
                g.setColour(juce::Colour(0xff314553));
                g.drawRoundedRectangle(panel.reduced(0.5f), 11.0f, 1.2f);
                g.setColour(juce::Colour(0xff65e0dc));
                g.fillRoundedRectangle(38.0f, 107.0f, 4.0f, 27.0f, 2.0f);
                g.setColour(juce::Colour(0xfff0f6f8));
                g.setFont(juce::Font(19.0f, juce::Font::bold));
                const juce::String panelTitle = juce::String("CONFIGURATION AUDIO ET MIDI");
                g.drawFittedText(panelTitle, 56, 100, juce::jmax(0, w - 78), 42,
                                 juce::Justification::centredLeft, 1);
                // A dedicated recessed bay for the live device selector.
                // The selector remains an actual JUCE child component on top.
                const auto deviceBay = juce::Rectangle<float>(18.0f, 173.0f,
                    (float) juce::jmax(0, w - 36),
                    (float) juce::jmax(0, getHeight() - 190));
                g.setColour(juce::Colour(0xff141e28));
                g.fillRoundedRectangle(deviceBay, 11.0f);
                g.setColour(juce::Colour(0xff314553));
                g.drawRoundedRectangle(deviceBay.reduced(0.5f), 11.0f, 1.0f);
                // Clearly visible section header, separated from interactive JUCE controls.
                g.setColour(juce::Colour(0xff65e0dc));
                g.fillRoundedRectangle(37.0f, 194.0f, 3.0f, 19.0f, 1.5f);
                g.setColour(juce::Colour(0xffe4f0f5));
                g.setFont(juce::Font(15.0f, juce::Font::bold));
                const juce::String deviceHeading = juce::String("P")
                    + juce::String::charToString((juce::juce_wchar) 0x00C9)
                    + "RIPH" + juce::String::charToString((juce::juce_wchar) 0x00C9)
                    + "RIQUES ET LATENCE";
                g.drawFittedText(deviceHeading, 50, 187,
                    juce::jmax(0, w - 90), 32, juce::Justification::centredLeft, 1);
                g.setColour(juce::Colour(0xff314553));
                g.drawLine(37.0f, 226.0f, (float) juce::jmax(37, w - 37), 226.0f, 1.0f);
                // An unobtrusive footer makes use of the empty area below the
                // JUCE selector without drawing over its interactive controls.
                const float footerY = (float) getHeight() - 53.0f;
                if (footerY > 400.0f)
                {
                    g.setColour(juce::Colour(0xff314553));
                    g.drawLine(37.0f, footerY - 12.0f,
                               (float) juce::jmax(37, w - 37), footerY - 12.0f, 1.0f);
                    g.setColour(juce::Colour(0xff91a8b7));
                    g.setFont(juce::Font(12.0f));
                    g.drawFittedText("LIBERTY  /  AUDIO ENGINE", 39, (int) footerY,
                        juce::jmax(0, w - 78), 20,
                        juce::Justification::centredLeft, 1);
                }


                // Bottom accent is inside the bay, away from all interactive controls.
                const float railY = (float) getHeight() - 20.0f;
                if (railY > deviceBay.getY() + 24.0f)
                {
                    g.setColour(juce::Colour(0xff65e0dc).withAlpha(0.45f));
                    g.fillRoundedRectangle(36.0f, railY,
                        (float) juce::jmax(0, juce::jmin(112, w - 72)), 2.0f, 1.0f);
                }


            }

            void resized() override
            {
                // Preserve the fully operational JUCE audio-device selector.
                const int availableWidth = juce::jmax(0, getWidth() - 60);
                const int availableHeight = juce::jmax(0, getHeight() - 319);
                midiInputLabel.setBounds(37, 234, 255, 28);
                midiInputBox.setBounds(300, 234, juce::jmax(160, getWidth() - 340), 28);
                midiOutputLabel.setBounds(37, 270, 255, 28);
                midiOutputBox.setBounds(300, 270, juce::jmax(160, getWidth() - 340), 28);
                viewport.setBounds(30, 310, availableWidth, juce::jmax(0, availableHeight - 72));
                translateAudioControls(*selector);
                // JUCE lays out the audio controls internally; allow scrolling
                // instead of clipping controls at smaller window sizes.
                // The dialog's minimum width already guarantees sufficient
                // space for the selector. Fit it to the viewport to avoid
                // horizontal overflow and reserve room for the scroll bar.
                // Keep the selector's internal layout tall enough for all
                // device, channel, sample-rate, buffer and MIDI controls.
                // A slightly larger content height provides breathing room
                // without forcing a scrollbar when the window is spacious.
                // JUCE's device selector contains several vertically stacked
                // controls whose visibility depends on the active device.
                // Keep enough scrollable content height for the bottom controls.
                selector->setSize(juce::jmax(0, availableWidth - 12),
                                  juce::jmax(520, availableHeight));
                // Clamp the scroll offset to the actual visible viewport area,
                // which may shrink when the vertical scrollbar is displayed.
                const int visibleWidth = viewport.getViewWidth();
                const int visibleHeight = viewport.getViewHeight();
                viewport.setViewPosition(
                    juce::jlimit(0, juce::jmax(0, selector->getWidth() - visibleWidth),
                                 viewport.getViewPositionX()),
                    juce::jlimit(0, juce::jmax(0, selector->getHeight() - visibleHeight),
                                 viewport.getViewPositionY()));
            }
        private:
            void timerCallback() override
            {
                // JUCE may rebuild its device controls after hardware changes.
                // Re-apply only translations of recognised static English labels.
                if (selector != nullptr && isShowing())
                    translateAudioControls(*selector);
                refreshMidiInputs();
                refreshMidiOutputs();
            }

            void refreshMidiOutputs()
            {
                const auto devices = juce::MidiOutput::getAvailableDevices();
                midiOutputIdentifiers.clear();
                midiOutputBox.clear(juce::dontSendNotification);
                for (const auto& device : devices)
                {
                    midiOutputIdentifiers.push_back(device.identifier);
                    midiOutputBox.addItem(device.name, (int)midiOutputIdentifiers.size());
                }
                const auto selected = audioEngine.getDeviceManager().getDefaultMidiOutputIdentifier();
                int selectedId = 0;
                for (int i = 0; i < (int)midiOutputIdentifiers.size(); ++i)
                    if (midiOutputIdentifiers[(size_t)i] == selected) selectedId = i + 1;
                midiOutputBox.setSelectedId(selectedId, juce::dontSendNotification);
                midiOutputBox.setTextWhenNothingSelected("Choisir une sortie MIDI");
            }

            void refreshMidiInputs()
            {
                const auto devices = juce::MidiInput::getAvailableDevices();
                bool same = (int)midiInputIdentifiers.size() == devices.size();
                if (same)
                    for (int i = 0; i < devices.size(); ++i)
                        if (midiInputIdentifiers[(size_t)i] != devices[i].identifier) same = false;
                if (!same)
                {
                    midiInputIdentifiers.clear();
                    midiInputBox.clear(juce::dontSendNotification);
                    for (const auto& device : devices)
                    {
                        midiInputIdentifiers.push_back(device.identifier);
                        midiInputBox.addItem(device.name, (int)midiInputIdentifiers.size());
                    }
                }
                int selectedId = 0;
                for (int i = 0; i < (int)midiInputIdentifiers.size(); ++i)
                    if (midiInputIdentifiers[(size_t)i] == audioEngine.getSelectedMidiInput())
                        selectedId = i + 1;
                midiInputBox.setSelectedId(selectedId, juce::dontSendNotification);
                midiInputBox.setTextWhenNothingSelected(devices.isEmpty() ? "Aucune entrée MIDI" : "Choisir une entrée MIDI");
            }
            AudioEngine& audioEngine;
            juce::Label midiInputLabel;
            juce::ComboBox midiInputBox;
            juce::Label midiOutputLabel;
            juce::ComboBox midiOutputBox;
            std::vector<juce::String> midiInputIdentifiers;
            std::vector<juce::String> midiOutputIdentifiers;
            juce::Viewport viewport;
            std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
        };
    public:
        explicit AudioSettingsWindow(AudioEngine& engine)
            : juce::DocumentWindow(juce::String("Liberty - Param")
                + juce::String::charToString((juce::juce_wchar) 0x00E8)
                + "tres audio et MIDI", juce::Colour(0xff0f141b), juce::DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new AudioSettingsContent(engine), true);
            setResizable(true, true);
            setResizeLimits(680, 620, 1400, 1100);
            centreWithSize(760, 650);
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
            // Same compact segmented transport style as the playback controls.
            tempoButton.setConnectedEdges(juce::Button::ConnectedOnRight);
            meterButton.setConnectedEdges(juce::Button::ConnectedOnLeft);
            for (auto* button : { &tempoButton, &meterButton })
            {
                button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff202c37));
                button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff24535d));
                button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe7f0f6));
                button->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
                button->setMouseClickGrabsKeyboardFocus(false); addAndMakeVisible(button);
            }
            tempoButton.onClick = [this] { owner->editTempo(); }; meterButton.onClick = [this] { owner->editTimeSignature(); };
            setBounds(1110, 34, 120, 36); owner->addAndMakeVisible(this);
        }
        void refresh() { tempoButton.setButtonText(juce::String(owner->tempoBpm, 2) + " BPM"); meterButton.setButtonText(juce::String(owner->timeSignatureNumerator) + "/" + juce::String(owner->timeSignatureDenominator)); repaint(); }
        void resized() override { tempoButton.setBounds(0, 4, 80, 28); meterButton.setBounds(80, 4, 40, 28); }
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
            // Keep Piano Roll notes visible on MIDI and Instrument tracks.
            const int firstMidiTrack = owner->getAudioTrackCount();
            const int lastPlayableTrack = firstMidiTrack + owner->getMidiTrackCount()
                                         + owner->getInstrumentTrackCount();
            const bool selectedPlayableTrack = owner->selectedTrack >= firstMidiTrack
                                               && owner->selectedTrack < lastPlayableTrack;
            const bool inArrange = selectedPlayableTrack
                && owner->getArrangeTrackTop(owner->selectedTrack) >= owner->getArrangeTop()
                && owner->getArrangeTrackTop(owner->selectedTrack)
                   + owner->getArrangeTrackHeight(owner->selectedTrack) <= owner->getMixerTop();
            setVisible(inArrange);
            if (!inArrange) return;
            const int rowY = owner->getArrangeTrackTop(owner->selectedTrack);
            const int rowH = owner->getArrangeTrackHeight(owner->selectedTrack);
            setBounds(owner->trackHeaderWidth, rowY,
                      juce::jmax(1, owner->getWidth() - owner->trackHeaderWidth), rowH);
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
    void timerCallback() override; void drawTransport(juce::Graphics&, juce::Rectangle<int>); void drawTrackArea(juce::Graphics&, juce::Rectangle<int>); void drawStepSequencerDock(juce::Graphics&, juce::Rectangle<int>); void drawMixer(juce::Graphics&, juce::Rectangle<int>); void openAudioSettings(); void editTempo(); void applyTempoBpm(double value); void editTimeSignature(); void rebuildWaveformCache(int); bool handleMixerMouse(const juce::MouseEvent&); int getAudioTrackAtPosition(juce::Point<int>) const; bool isPointInsideAudioClip(int, juce::Point<int>) const; void showProjectMenu(); void newProject(); void openProject(); void saveProject(); void saveProjectAs(); bool saveProjectToFile(const juce::File&); bool loadProjectFromFile(const juce::File&);  bool resetProjectState(); void initializeProjectTracking(); juce::String getProjectStateSignature() const; void markProjectClean(); void confirmBeforeProjectAction(std::function<void()> action);
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
    std::vector<InstrumentStepSequencerBank> instrumentStepSequencers; bool dockStepSequencerMode = false; bool dockMixerMode = true; bool drumSamplerDockSelected = false; int stepSequencerPage = 0; int stepSequencerSelectedStep = 0; int stepSequencerMidiTarget = 0; bool draggingStepSequencerPattern = false; int draggedStepSequencerInstrument = -1; std::map<int, juce::String> customTrackNames; std::unique_ptr<AudioSettingsWindow> audioSettingsWindow; std::unique_ptr<juce::FileChooser> projectFileChooser; std::vector<std::vector<float>> waveformMin; std::vector<std::vector<float>> waveformMax; std::vector<juce::File> trackSourceFiles; std::vector<juce::String> pendingAudioFileNames; std::vector<double> pendingAudioLengths; std::vector<double> pendingAudioStartSeconds; std::vector<PendingAudioWarpState> pendingAudioWarpStates; juce::File currentProjectFile; juce::String savedProjectStateSignature; std::function<void()> pendingProjectAction; int selectedTrack = 0; int armedInstrumentTrack = -1; int monitoredInstrumentTrack = -1; int dynamicMidiTrackCount = 1; int dynamicInstrumentTrackCount = 1; int trackScrollRows = 0; bool isPlaying = false; double playheadSeconds = 0.0; double tempoBpm = 120.0; int timeSignatureNumerator = 4; int timeSignatureDenominator = 4; double midiClipStartSeconds = 0.0; double midiClipLengthSeconds = 2.0; bool midiClipLengthUserDefined = false; TempoControls tempoControls { this }; std::unique_ptr<ProjectButton> projectButton; MidiClipOverlay midiClipOverlay { this }; bool draggingClip = false; int draggedTrack = -1; int draggedPatternInstrument = -1; int draggedPatternClip = -1; float dragStartMouseX = 0.0f; double dragStartSeconds = 0.0; int mixerDragMode = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};