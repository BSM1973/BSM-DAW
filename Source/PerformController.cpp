#define private public
#include "MainComponent.h"
#undef private

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <atomic>
#include <map>
#include <memory>

bool isLibertyMixConsoleVisible(MainComponent* owner);
void setLibertyMixConsoleVisible(MainComponent* owner, bool shouldShow);
int getLibertyTrackColourId(int track);
juce::String getLibertyTrackName(int track);

namespace
{
constexpr int transportHeight = 76;
constexpr int audioTracks = AudioEngine::maxAudioTracks;
constexpr int midiTrack = AudioEngine::maxAudioTracks;
constexpr int instrumentTrack = AudioEngine::maxAudioTracks + 1;
constexpr int performTracks = AudioEngine::maxAudioTracks + 2;
constexpr int sceneCount = 8;

juce::Colour trackColour(int id)
{
    static constexpr std::array<juce::uint32, 9> colours {
        0xff31506a, 0xff3b82f6, 0xff22c55e, 0xffeab308, 0xfff97316,
        0xffef4444, 0xffa855f7, 0xffec4899, 0xff14b8a6
    };
    return juce::Colour(colours[(size_t)juce::jlimit(0, 8, id)]);
}

bool isSupportedAudioFile(const juce::File& file)
{
    const auto ext = file.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aif" || ext == ".aiff";
}

class PerformAudioPlayer final : private juce::AudioIODeviceCallback
{
public:
    explicit PerformAudioPlayer(MainComponent& ownerIn) : owner(ownerIn)
    {
        formats.registerBasicFormats();
        owner.audioEngine.getDeviceManager().addAudioCallback(this);
        callbackAttached = true;
    }

    ~PerformAudioPlayer() override
    {
        stopCaptureInternal();
        if (callbackAttached)
            owner.audioEngine.getDeviceManager().removeAudioCallback(this);
    }

    void setEnabled(bool shouldEnable)
    {
        const juce::ScopedLock sl(lock);
        enabled = shouldEnable;
        if (!enabled)
            for (auto& track : tracks) { track.playing = false; track.position = 0; }
    }

    bool loadAndLaunch(int trackIndex, const juce::File& file, juce::String& error)
    {
        if (trackIndex < 0 || trackIndex >= audioTracks || !file.existsAsFile())
        {
            error = "Invalid PERFORM clip.";
            return false;
        }

        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr)
        {
            error = "This audio file cannot be opened.";
            return false;
        }

        const int sourceChannels = juce::jlimit(1, 2, (int)reader->numChannels);
        const int sourceSamples = (int)juce::jmin<juce::int64>(reader->lengthInSamples, (juce::int64)0x7fffffff);
        if (sourceSamples <= 0)
        {
            error = "The audio file is empty.";
            return false;
        }

        juce::AudioBuffer<float> source(sourceChannels, sourceSamples);
        source.clear();
        if (!reader->read(&source, 0, sourceSamples, 0, true, true))
        {
            error = "The audio file could not be decoded.";
            return false;
        }

        const double deviceRate = juce::jmax(1.0, owner.audioEngine.getSampleRate());
        const double sourceRate = juce::jmax(1.0, reader->sampleRate);
        std::unique_ptr<juce::AudioBuffer<float>> finalBuffer;

        if (std::abs(deviceRate - sourceRate) < 0.5)
        {
            finalBuffer = std::make_unique<juce::AudioBuffer<float>>(sourceChannels, sourceSamples);
            for (int ch = 0; ch < sourceChannels; ++ch)
                finalBuffer->copyFrom(ch, 0, source, ch, 0, sourceSamples);
        }
        else
        {
            const int targetSamples = juce::jmax(1, (int)std::llround((double)sourceSamples * deviceRate / sourceRate));
            finalBuffer = std::make_unique<juce::AudioBuffer<float>>(sourceChannels, targetSamples);
            finalBuffer->clear();
            const double speedRatio = sourceRate / deviceRate;
            for (int ch = 0; ch < sourceChannels; ++ch)
            {
                juce::LagrangeInterpolator interpolator;
                interpolator.process(speedRatio,
                                     source.getReadPointer(ch),
                                     finalBuffer->getWritePointer(ch),
                                     targetSamples);
            }
        }

        {
            const juce::ScopedLock sl(lock);
            auto& track = tracks[(size_t)trackIndex];
            track.buffer = std::move(finalBuffer);
            track.file = file;
            track.position = 0;
            track.playing = true;
            enabled = true;
        }
        return true;
    }

    void stopTrack(int trackIndex)
    {
        if (trackIndex < 0 || trackIndex >= audioTracks) return;
        const juce::ScopedLock sl(lock);
        tracks[(size_t)trackIndex].playing = false;
        tracks[(size_t)trackIndex].position = 0;
    }

    void stopAll()
    {
        const juce::ScopedLock sl(lock);
        for (auto& track : tracks) { track.playing = false; track.position = 0; }
    }

    bool isTrackPlaying(int trackIndex) const
    {
        if (trackIndex < 0 || trackIndex >= audioTracks) return false;
        const juce::ScopedLock sl(lock);
        return tracks[(size_t)trackIndex].playing;
    }

    bool startCapture(juce::String& error)
    {
        const juce::ScopedLock sl(lock);
        if (captureActive) return true;

        const double rate = owner.audioEngine.getSampleRate();
        if (rate <= 0.0)
        {
            error = "No audio device is available.";
            return false;
        }

        auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("BSM").getChildFile("Liberty").getChildFile("Perform Captures");
        if (!folder.createDirectory().wasOk() && !folder.isDirectory())
        {
            error = "The PERFORM capture folder could not be created.";
            return false;
        }

        captureFile = folder.getNonexistentChildFile("Perform Capture " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S"), ".wav", false);
        std::unique_ptr<juce::FileOutputStream> stream(captureFile.createOutputStream());
        if (stream == nullptr)
        {
            error = "The PERFORM capture file could not be created.";
            return false;
        }

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(stream.get(), rate, 2, 24, {}, 0));
        if (writer == nullptr)
        {
            error = "The PERFORM capture writer could not be created.";
            return false;
        }
        stream.release();

        captureThread = std::make_unique<juce::TimeSliceThread>("Liberty PERFORM Capture");
        captureThread->startThread();
        captureWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(writer.release(), *captureThread, 65536);
        captureActive = true;
        return true;
    }

    juce::File stopCapture()
    {
        const juce::ScopedLock sl(lock);
        return stopCaptureInternal();
    }

    bool isCapturing() const noexcept { return captureActive.load(std::memory_order_relaxed); }

private:
    struct TrackState
    {
        std::unique_ptr<juce::AudioBuffer<float>> buffer;
        juce::File file;
        juce::int64 position = 0;
        bool playing = false;
    };

    juce::File stopCaptureInternal()
    {
        captureActive.store(false, std::memory_order_relaxed);
        captureWriter.reset();
        if (captureThread != nullptr)
        {
            captureThread->stopThread(2000);
            captureThread.reset();
        }
        return captureFile;
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        const int block = device != nullptr ? juce::jmax(32, device->getCurrentBufferSizeSamples()) : 512;
        captureMix.setSize(2, block, false, false, true);
    }

    void audioDeviceStopped() override
    {
        const juce::ScopedLock sl(lock);
        for (auto& track : tracks) track.playing = false;
    }

    void audioDeviceIOCallbackWithContext(const float* const*, int,
                                          float* const* outputChannelData, int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override
    {
        if (!enabled.load(std::memory_order_relaxed)) return;
        if (!lock.tryEnter()) return;

        const int captureSamples = juce::jmin(numSamples, captureMix.getNumSamples());
        if (captureSamples > 0) captureMix.clear(0, captureSamples);

        for (int trackIndex = 0; trackIndex < audioTracks; ++trackIndex)
        {
            auto& state = tracks[(size_t)trackIndex];
            if (!state.playing || state.buffer == nullptr || state.position >= state.buffer->getNumSamples())
                continue;

            const int remaining = (int)juce::jmin<juce::int64>((juce::int64)numSamples,
                                                                (juce::int64)state.buffer->getNumSamples() - state.position);
            if (remaining <= 0)
            {
                state.playing = false;
                continue;
            }

            const float gain = owner.audioEngine.getTrackGain(trackIndex);
            const float pan = owner.audioEngine.getTrackPan(trackIndex);
            const float leftGain = gain * (pan > 0.0f ? 1.0f - pan : 1.0f);
            const float rightGain = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);
            const int sourceChannels = state.buffer->getNumChannels();

            if (numOutputChannels > 0 && outputChannelData[0] != nullptr)
                juce::FloatVectorOperations::addWithMultiply(outputChannelData[0],
                    state.buffer->getReadPointer(0) + state.position, leftGain, remaining);
            if (numOutputChannels > 1 && outputChannelData[1] != nullptr)
                juce::FloatVectorOperations::addWithMultiply(outputChannelData[1],
                    state.buffer->getReadPointer(sourceChannels > 1 ? 1 : 0) + state.position, rightGain, remaining);

            if (captureActive.load(std::memory_order_relaxed) && captureSamples > 0)
            {
                const int n = juce::jmin(remaining, captureSamples);
                juce::FloatVectorOperations::addWithMultiply(captureMix.getWritePointer(0),
                    state.buffer->getReadPointer(0) + state.position, leftGain, n);
                juce::FloatVectorOperations::addWithMultiply(captureMix.getWritePointer(1),
                    state.buffer->getReadPointer(sourceChannels > 1 ? 1 : 0) + state.position, rightGain, n);
            }

            state.position += remaining;
            if (state.position >= state.buffer->getNumSamples())
            {
                state.playing = false;
                state.position = 0;
            }
        }

        if (captureActive.load(std::memory_order_relaxed) && captureWriter != nullptr && captureSamples > 0)
        {
            const float master = owner.audioEngine.getMasterGain();
            captureMix.applyGain(0, captureSamples, master);
            captureWriter->write(captureMix.getArrayOfReadPointers(), captureSamples);
        }

        lock.exit();
    }

    MainComponent& owner;
    juce::AudioFormatManager formats;
    std::array<TrackState, audioTracks> tracks;
    mutable juce::CriticalSection lock;
    std::atomic<bool> enabled { false };
    bool callbackAttached = false;
    juce::AudioBuffer<float> captureMix;
    std::unique_ptr<juce::TimeSliceThread> captureThread;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> captureWriter;
    juce::File captureFile;
    std::atomic<bool> captureActive { false };
};

class PerformView final : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          private juce::Timer
{
public:
    explicit PerformView(MainComponent& ownerIn)
        : owner(ownerIn), player(ownerIn)
    {
        setOpaque(true);
        setInterceptsMouseClicks(true, true);
        setAlwaysOnTop(true);

        for (int track = 0; track < performTracks; ++track)
        {
            auto& stop = stopTrackButtons[(size_t)track];
            stop.setButtonText("STOP");
            stop.setMouseClickGrabsKeyboardFocus(false);
            stop.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252b32));
            stop.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            stop.onClick = [this, track]
            {
                if (track < audioTracks) player.stopTrack(track);
                activeTrackScene[(size_t)track] = -1;
                refreshClipLabels();
                repaint();
            };
            addAndMakeVisible(stop);

            for (int scene = 0; scene < sceneCount; ++scene)
            {
                auto& cell = clipButtons[(size_t)track][(size_t)scene];
                cell.setMouseClickGrabsKeyboardFocus(false);
                cell.onClick = [this, track, scene] { launchClip(track, scene); };
                addAndMakeVisible(cell);
            }
        }

        for (int scene = 0; scene < sceneCount; ++scene)
        {
            auto& launch = sceneButtons[(size_t)scene];
            launch.setButtonText("SCENE " + juce::String(scene + 1));
            launch.setMouseClickGrabsKeyboardFocus(false);
            launch.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff27313a));
            launch.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            launch.onClick = [this, scene] { launchScene(scene); };
            addAndMakeVisible(launch);
        }

        stopAllButton.setButtonText("STOP ALL");
        stopAllButton.setMouseClickGrabsKeyboardFocus(false);
        stopAllButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6f3030));
        stopAllButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        stopAllButton.onClick = [this]
        {
            player.stopAll();
            activeTrackScene.fill(-1);
            refreshClipLabels();
            repaint();
        };
        addAndMakeVisible(stopAllButton);

        recordArrangeButton.setButtonText("RECORD TO ARRANGE");
        recordArrangeButton.setMouseClickGrabsKeyboardFocus(false);
        recordArrangeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff31404d));
        recordArrangeButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        recordArrangeButton.onClick = [this] { toggleCaptureToArrange(); };
        addAndMakeVisible(recordArrangeButton);

        activeTrackScene.fill(-1);
        setVisible(false);
        owner.addAndMakeVisible(this);
        startTimerHz(20);
    }

    ~PerformView() override
    {
        if (player.isCapturing()) player.stopCapture();
        player.setEnabled(false);
        stopTimer();
    }

    void setPerformVisible(bool shouldShow)
    {
        performVisible = shouldShow;
        dragTrack = dragScene = -1;
        setVisible(shouldShow);
        player.setEnabled(shouldShow);

        if (shouldShow)
        {
            // ARRANGE transport is deliberately stopped. PERFORM owns independent audio.
            owner.audioEngine.setPlaying(false);
            owner.isPlaying = false;
            setBounds(0, transportHeight, owner.getWidth(), juce::jmax(1, owner.getHeight() - transportHeight));
            resized();
            refreshClipLabels();
            toFront(false);
            repaint();
        }
        else
        {
            player.stopAll();
            activeTrackScene.fill(-1);
            if (player.isCapturing()) finishCaptureToArrange();
        }
    }

    bool isPerformVisible() const noexcept { return performVisible; }

    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        if (!performVisible || files.isEmpty()) return false;
        for (const auto& path : files)
            if (isSupportedAudioFile(juce::File(path))) return true;
        return false;
    }

    void fileDragEnter(const juce::StringArray&, int x, int y) override { updateDropTarget({ x, y }); }
    void fileDragMove(const juce::StringArray&, int x, int y) override { updateDropTarget({ x, y }); }
    void fileDragExit(const juce::StringArray&) override
    {
        dragTrack = dragScene = -1;
        refreshClipLabels();
        repaint();
    }

    void filesDropped(const juce::StringArray& files, int x, int y) override
    {
        updateDropTarget({ x, y });
        if (dragTrack < 0 || dragTrack >= audioTracks || dragScene < 0 || dragScene >= sceneCount)
        {
            dragTrack = dragScene = -1;
            refreshClipLabels();
            repaint();
            return;
        }

        juce::File selected;
        for (const auto& path : files)
        {
            const juce::File candidate(path);
            if (isSupportedAudioFile(candidate)) { selected = candidate; break; }
        }
        if (!selected.existsAsFile()) return;

        performAudioFiles[(size_t)dragTrack][(size_t)dragScene] = selected;
        performTrackOwnsSlots[(size_t)dragTrack] = true;
        activeTrackScene[(size_t)dragTrack] = -1;
        dragTrack = dragScene = -1;
        refreshClipLabels();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0c1014));
        g.setColour(juce::Colour(0xff161c22));
        g.fillRect(0, 0, getWidth(), 64);
        g.setColour(juce::Colour(0xff303944));
        g.drawHorizontalLine(63, 0.0f, (float)getWidth());

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(21.0f, juce::Font::bold));
        g.drawText("PERFORM", 22, 10, 150, 28, juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff8f98a3));
        g.setFont(juce::Font(10.0f));
        g.drawText("SESSION   CLIPS   SCENES   LIVE LAUNCH", 170, 16, 330, 18, juce::Justification::centredLeft);

        for (int track = 0; track < performTracks; ++track)
        {
            const auto header = trackHeaders[(size_t)track];
            const auto colour = colourForTrack(track);
            g.setColour(juce::Colour(0xff151a20));
            g.fillRoundedRectangle(header.toFloat(), 5.0f);
            g.setColour(colour);
            g.fillRoundedRectangle((float)header.getX(), (float)header.getY(), (float)header.getWidth(), 5.0f, 2.5f);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(11.5f, juce::Font::bold));
            g.drawText(trackName(track), header.reduced(6, 6), juce::Justification::centredTop, true);
            g.setColour(juce::Colour(0xff7e8791));
            g.setFont(juce::Font(8.0f, juce::Font::bold));
            g.drawText(trackType(track), header.getX() + 5, header.getY() + 28, header.getWidth() - 10, 14, juce::Justification::centred, true);
        }

        for (int scene = 0; scene < sceneCount; ++scene)
        {
            const auto y = sceneRows[(size_t)scene].getY();
            g.setColour(scene % 2 == 0 ? juce::Colour(0xff11161b) : juce::Colour(0xff0f1419));
            g.fillRect(0, y, getWidth(), sceneRows[(size_t)scene].getHeight());
            g.setColour(juce::Colour(0xff29313a));
            g.drawHorizontalLine(sceneRows[(size_t)scene].getBottom() - 1, 0.0f, (float)getWidth());
        }

        if (dragTrack >= 0 && dragScene >= 0)
        {
            const auto r = clipButtons[(size_t)dragTrack][(size_t)dragScene].getBounds().expanded(2);
            g.setColour(juce::Colour(0xff69d4ff));
            g.drawRoundedRectangle(r.toFloat(), 5.0f, 3.0f);
            g.setFont(juce::Font(9.0f, juce::Font::bold));
            g.drawText("DROP AUDIO HERE", r, juce::Justification::centred);
        }
    }

    void resized() override
    {
        const int leftMargin = 18;
        const int sceneLaunchW = 92;
        const int gridLeft = leftMargin;
        const int gridRight = getWidth() - sceneLaunchW - 28;
        const int gap = 6;
        const int available = juce::jmax(performTracks * 110, gridRight - gridLeft);
        const int columnW = juce::jlimit(110, 220, (available - (performTracks - 1) * gap) / performTracks);
        const int headerTop = 72;
        const int headerH = 62;
        const int rowsTop = 142;
        const int footerH = 54;
        const int rowsAvailable = juce::jmax(320, getHeight() - rowsTop - footerH - 16);
        const int rowH = juce::jlimit(44, 86, rowsAvailable / sceneCount);

        for (int track = 0; track < performTracks; ++track)
        {
            const int x = gridLeft + track * (columnW + gap);
            trackHeaders[(size_t)track] = { x, headerTop, columnW, headerH };
            stopTrackButtons[(size_t)track].setBounds(x + 8, headerTop + 40, columnW - 16, 18);
            for (int scene = 0; scene < sceneCount; ++scene)
            {
                const int y = rowsTop + scene * rowH;
                clipButtons[(size_t)track][(size_t)scene].setBounds(x + 2, y + 4, columnW - 4, rowH - 8);
            }
        }

        for (int scene = 0; scene < sceneCount; ++scene)
        {
            const int y = rowsTop + scene * rowH;
            sceneRows[(size_t)scene] = { 0, y, getWidth(), rowH };
            sceneButtons[(size_t)scene].setBounds(getWidth() - sceneLaunchW - 18, y + 4, sceneLaunchW, rowH - 8);
        }
        recordArrangeButton.setBounds(18, getHeight() - 44, 170, 30);
        stopAllButton.setBounds(getWidth() - 140, getHeight() - 44, 122, 30);
    }

private:
    juce::String trackName(int track) const
    {
        if (track < audioTracks) return "Audio " + juce::String(track + 1);
        if (track == midiTrack) return "MIDI 1";
        return "Instrument 1";
    }

    juce::String trackType(int track) const
    {
        if (track < audioTracks) return "AUDIO";
        if (track == midiTrack) return "MIDI";
        return "INSTRUMENT";
    }

    juce::Colour colourForTrack(int track) const
    {
        return trackColour(getLibertyTrackColourId(track));
    }

    bool hasDroppedAudio(int track, int scene) const
    {
        return track >= 0 && track < audioTracks && scene >= 0 && scene < sceneCount
            && performAudioFiles[(size_t)track][(size_t)scene].existsAsFile();
    }

    bool slotHasClip(int track, int scene) const
    {
        if (track < audioTracks)
            return hasDroppedAudio(track, scene);

        // MIDI and virtual-instrument Session clips will get their own independent
        // clip engine later. They deliberately do not inherit ARRANGE data.
        return false;
    }

    juce::String slotName(int track, int scene) const
    {
        if (!slotHasClip(track, scene)) return "EMPTY";
        auto name = performAudioFiles[(size_t)track][(size_t)scene].getFileName();
        if (name.length() > 22) name = name.substring(0, 19) + "...";
        return name;
    }

    void refreshClipLabels()
    {
        for (int track = 0; track < performTracks; ++track)
        {
            const auto colour = colourForTrack(track);
            for (int scene = 0; scene < sceneCount; ++scene)
            {
                auto& button = clipButtons[(size_t)track][(size_t)scene];
                const bool hasClip = slotHasClip(track, scene);
                const bool active = activeTrackScene[(size_t)track] == scene
                                 && track < audioTracks && player.isTrackPlaying(track);
                const bool target = dragTrack == track && dragScene == scene;
                button.setButtonText(target ? "DROP AUDIO" : slotName(track, scene));
                button.setEnabled(track < audioTracks);
                button.setColour(juce::TextButton::buttonColourId,
                                 target ? juce::Colour(0xff245b70)
                                        : (active ? colour.brighter(0.25f)
                                                  : (hasClip ? colour.withAlpha(0.70f) : juce::Colour(0xff181d22))));
                button.setColour(juce::TextButton::textColourOffId,
                                 (hasClip || target) ? juce::Colours::white : juce::Colour(0xff68717b));
            }
        }
    }

    void updateDropTarget(juce::Point<int> point)
    {
        int nextTrack = -1;
        int nextScene = -1;
        for (int track = 0; track < audioTracks; ++track)
        {
            for (int scene = 0; scene < sceneCount; ++scene)
            {
                if (clipButtons[(size_t)track][(size_t)scene].getBounds().contains(point))
                {
                    nextTrack = track;
                    nextScene = scene;
                    break;
                }
            }
            if (nextTrack >= 0) break;
        }
        if (nextTrack != dragTrack || nextScene != dragScene)
        {
            dragTrack = nextTrack;
            dragScene = nextScene;
            refreshClipLabels();
            repaint();
        }
    }

    void launchClip(int track, int scene)
    {
        if (track < 0 || track >= audioTracks || !slotHasClip(track, scene)) return;
        juce::String error;
        if (!player.loadAndLaunch(track, performAudioFiles[(size_t)track][(size_t)scene], error))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Liberty - PERFORM", error, "OK");
            return;
        }
        activeTrackScene[(size_t)track] = scene;
        refreshClipLabels();
        repaint();
    }

    void launchScene(int scene)
    {
        for (int track = 0; track < audioTracks; ++track)
            if (slotHasClip(track, scene)) launchClip(track, scene);
    }

    void toggleCaptureToArrange()
    {
        if (!player.isCapturing())
        {
            juce::String error;
            if (!player.startCapture(error))
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                       "Liberty - PERFORM", error, "OK");
                return;
            }
            recordArrangeButton.setButtonText("STOP RECORDING");
            recordArrangeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff9b4545));
        }
        else
        {
            finishCaptureToArrange();
        }
    }

    void finishCaptureToArrange()
    {
        const auto capture = player.stopCapture();
        recordArrangeButton.setButtonText("RECORD TO ARRANGE");
        recordArrangeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff31404d));
        if (!capture.existsAsFile()) return;

        int targetTrack = -1;
        for (int i = 0; i < AudioEngine::maxAudioTracks; ++i)
            if (!owner.audioEngine.hasAudioFile(i)) { targetTrack = i; break; }

        if (targetTrack < 0)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                "Liberty - PERFORM",
                "PERFORM capture was saved, but ARRANGE has no empty Audio track.\n\n" + capture.getFullPathName(),
                "OK");
            return;
        }

        juce::String error;
        if (!owner.audioEngine.loadAudioFileIntoTrack(targetTrack, capture, error))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Liberty - PERFORM", error, "OK");
            return;
        }
        owner.audioEngine.setTrackStartSeconds(targetTrack, 0.0);
        owner.trackSourceFiles[(size_t)targetTrack] = capture;
        owner.rebuildWaveformCache(targetTrack);
        owner.selectedTrack = targetTrack;
        owner.repaint();
    }

    void timerCallback() override
    {
        if (!performVisible) return;
        const auto wanted = juce::Rectangle<int>(0, transportHeight, owner.getWidth(), juce::jmax(1, owner.getHeight() - transportHeight));
        if (getBounds() != wanted) setBounds(wanted);
        toFront(false);
        refreshClipLabels();
        repaint();
    }

    MainComponent& owner;
    PerformAudioPlayer player;
    std::array<std::array<juce::TextButton, sceneCount>, performTracks> clipButtons;
    std::array<juce::TextButton, sceneCount> sceneButtons;
    std::array<juce::TextButton, performTracks> stopTrackButtons;
    juce::TextButton stopAllButton, recordArrangeButton;
    std::array<juce::Rectangle<int>, performTracks> trackHeaders;
    std::array<juce::Rectangle<int>, sceneCount> sceneRows;
    std::array<int, performTracks> activeTrackScene;
    std::array<std::array<juce::File, sceneCount>, audioTracks> performAudioFiles;
    std::array<bool, audioTracks> performTrackOwnsSlots {};
    int dragTrack = -1;
    int dragScene = -1;
    bool performVisible = false;
};

class PerformController final : private juce::Timer
{
public:
    explicit PerformController(MainComponent& ownerIn) : owner(ownerIn), view(ownerIn)
    {
        performButton.setButtonText("PERFORM");
        performButton.setMouseClickGrabsKeyboardFocus(false);
        performButton.setClickingTogglesState(false);
        performButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        performButton.onClick = [this]
        {
            if (!performVisible) setLibertyMixConsoleVisible(&owner, false);
            setPerformVisible(true);
        };
        owner.addAndMakeVisible(performButton);

        arrangeOverlay.setButtonText("ARRANGE");
        mixOverlay.setButtonText("MIXCONSOLE");
        for (auto* b : { &arrangeOverlay, &mixOverlay })
        {
            b->setMouseClickGrabsKeyboardFocus(false);
            b->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
            b->setVisible(false);
            owner.addAndMakeVisible(*b);
        }
        arrangeOverlay.onClick = [this]
        {
            setPerformVisible(false);
            setLibertyMixConsoleVisible(&owner, false);
        };
        mixOverlay.onClick = [this]
        {
            setPerformVisible(false);
            setLibertyMixConsoleVisible(&owner, true);
        };

        startTimerHz(30);
    }

    ~PerformController() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        view.setPerformVisible(false);
        performButton.setVisible(false);
        arrangeOverlay.setVisible(false);
        mixOverlay.setVisible(false);
    }

    void setPerformVisible(bool shouldShow)
    {
        performVisible = shouldShow;
        view.setPerformVisible(shouldShow);
        arrangeOverlay.setVisible(shouldShow);
        mixOverlay.setVisible(shouldShow);
        performButton.setColour(juce::TextButton::buttonColourId,
                                shouldShow ? juce::Colour(0xff315f7a) : juce::Colour(0xff252a31));
        if (shouldShow) view.toFront(false);
        arrangeOverlay.toFront(false);
        mixOverlay.toFront(false);
        performButton.toFront(false);
        owner.repaint();
    }

    bool isVisible() const noexcept { return performVisible; }

private:
    void timerCallback() override
    {
        if (stopped.load()) return;
        arrangeOverlay.setBounds(1055, 8, 88, 26);
        mixOverlay.setBounds(1147, 8, 112, 26);
        performButton.setBounds(1263, 8, 94, 26);
        if (performVisible)
        {
            arrangeOverlay.toFront(false);
            mixOverlay.toFront(false);
        }
        performButton.toFront(false);
    }

    MainComponent& owner;
    PerformView view;
    juce::TextButton arrangeOverlay, mixOverlay, performButton;
    std::atomic<bool> stopped { false };
    bool performVisible = false;
};

std::map<MainComponent*, std::unique_ptr<PerformController>> controllers;

class Bootstrap final : private juce::Timer
{
public:
    Bootstrap() { startTimerHz(10); }
    ~Bootstrap() override { shutdown(); }

    void shutdown()
    {
        stopTimer();
        for (auto& entry : controllers)
            if (entry.second) entry.second->shutdown();
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
                        controllers.emplace(main, std::make_unique<PerformController>(*main));
    }
};

Bootstrap bootstrap;
}

bool isLibertyPerformVisible(MainComponent* owner)
{
    if (owner == nullptr) return false;
    const auto it = controllers.find(owner);
    return it != controllers.end() && it->second && it->second->isVisible();
}

void setLibertyPerformVisible(MainComponent* owner, bool shouldShow)
{
    if (owner == nullptr) return;
    const auto it = controllers.find(owner);
    if (it == controllers.end() || !it->second) return;
    it->second->setPerformVisible(shouldShow);
}

void shutdownLibertyPerformController()
{
    bootstrap.shutdown();
}
