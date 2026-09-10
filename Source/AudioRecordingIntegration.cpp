#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

// This integration is isolated from the validated audio/MIDI engines. The
// private/public shim is intentionally limited to this bridge so the existing
// MainComponent API and all stable editing code remain untouched.
#define private public
#include "MainComponent.h"
#undef private

namespace
{
class Recorder final : private juce::AudioIODeviceCallback
{
public:
    explicit Recorder(juce::AudioDeviceManager& managerIn) : manager(managerIn)
    {
        manager.addAudioCallback(this);
    }

    ~Recorder() override
    {
        stop();
        manager.removeAudioCallback(this);
    }

    bool start()
    {
        if (recording.load(std::memory_order_acquire)) return false;
        const auto* device = manager.getCurrentAudioDevice();
        if (device == nullptr || device->getActiveInputChannels().countNumberOfSetBits() == 0) return false;
        const auto rate = device->getCurrentSampleRate();
        if (rate <= 0.0) return false;

        const int channels = juce::jlimit(1, 2, device->getActiveInputChannels().countNumberOfSetBits());
        constexpr double maxSeconds = 600.0;
        buffer.setSize(channels, static_cast<int>(std::ceil(rate * maxSeconds)), false, true, true);
        buffer.clear();
        writePosition.store(0, std::memory_order_release);
        recordingRate = rate;
        recordingChannels = channels;
        recording.store(true, std::memory_order_release);
        return true;
    }

    void stop()
    {
        recording.store(false, std::memory_order_release);
        while (callbackActive.load(std::memory_order_acquire))
            juce::Thread::yield();
    }

    bool isRecording() const noexcept { return recording.load(std::memory_order_acquire); }
    int getRecordedSamples() const noexcept { return writePosition.load(std::memory_order_acquire); }
    double getRecordingRate() const noexcept { return recordingRate; }

    bool writeToWav(const juce::File& file) const
    {
        const int samples = getRecordedSamples();
        if (samples <= 0 || recordingChannels <= 0 || recordingRate <= 0.0) return false;
        std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
        if (stream == nullptr) return false;
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), recordingRate,
                                                                            static_cast<unsigned int>(recordingChannels),
                                                                            24, {}, 0));
        if (writer == nullptr) return false;
        stream.release();
        return writer->writeFromAudioSampleBuffer(buffer, 0, samples);
    }

private:
    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const*, int, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override
    {
        callbackActive.store(true, std::memory_order_release);
        if (recording.load(std::memory_order_acquire) && inputChannelData != nullptr)
        {
            const int current = writePosition.load(std::memory_order_relaxed);
            const int capacity = buffer.getNumSamples();
            const int count = juce::jmin(numSamples, juce::jmax(0, capacity - current));
            const int channels = juce::jmin(recordingChannels, numInputChannels);
            for (int channel = 0; channel < channels; ++channel)
                if (inputChannelData[channel] != nullptr)
                    buffer.copyFrom(channel, current, inputChannelData[channel], count);
            if (count > 0)
                writePosition.store(current + count, std::memory_order_release);
            if (count < numSamples)
                recording.store(false, std::memory_order_release);
        }
        callbackActive.store(false, std::memory_order_release);
    }

    juce::AudioDeviceManager& manager;
    juce::AudioBuffer<float> buffer;
    std::atomic<bool> recording { false };
    std::atomic<bool> callbackActive { false };
    std::atomic<int> writePosition { 0 };
    double recordingRate = 0.0;
    int recordingChannels = 0;
};

class RecordingController final : public juce::Component, private juce::Timer
{
public:
    RecordingController(MainComponent& ownerIn) : owner(ownerIn), recorder(ownerIn.audioEngine.getDeviceManager())
    {
        setInterceptsMouseClicks(true, false);
        owner.addAndMakeVisible(this);
        startTimerHz(20);
    }

    ~RecordingController() override { stopRecording(); }

    void paint(juce::Graphics& g) override
    {
        const auto r = getLocalBounds().reduced(1);
        g.setColour(recorder.isRecording() ? juce::Colour(0xffa83f46) : juce::Colour(0xff252a31));
        g.fillRoundedRectangle(r.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff454b54));
        g.drawRoundedRectangle(r.toFloat(), 5.0f, 1.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(recorder.isRecording() ? "STOP REC" : "REC", r, juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        if (recorder.isRecording()) stopRecording();
        else startRecording();
    }

private:
    void timerCallback() override
    {
        const auto x = 525;
        setBounds(x, 38, 56, 28);
        repaint();
    }

    void startRecording()
    {
        if (owner.selectedTrack < 0 || owner.selectedTrack >= AudioEngine::maxAudioTracks)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Recording", "Select an Audio track before recording.", "OK");
            return;
        }

        const auto* device = owner.audioEngine.getDeviceManager().getCurrentAudioDevice();
        if (device == nullptr || device->getActiveInputChannels().countNumberOfSetBits() == 0)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Recording", "No audio input is available. Select an input in AUDIO SETTINGS.", "OK");
            return;
        }

        recordingTrack = owner.selectedTrack;
        recordingStartSeconds = owner.audioEngine.getCurrentTimeSeconds();
        savedExtraLength = owner.audioEngine.getProjectExtraLengthSeconds();
        owner.audioEngine.setProjectExtraLengthSeconds(juce::jmax(savedExtraLength, recordingStartSeconds + 600.0));
        if (!recorder.start()) return;
        owner.audioEngine.setPlaying(true);
        owner.isPlaying = true;
        owner.repaint();
    }

    void stopRecording()
    {
        if (!recorder.isRecording() && recorder.getRecordedSamples() <= 0) return;
        recorder.stop();
        owner.audioEngine.setPlaying(false);
        owner.isPlaying = false;
        owner.audioEngine.setProjectExtraLengthSeconds(savedExtraLength);

        const int samples = recorder.getRecordedSamples();
        if (samples <= 0) { owner.repaint(); return; }

        juce::File folder;
        if (owner.currentProjectFile.existsAsFile())
            folder = owner.currentProjectFile.getSiblingFile(owner.currentProjectFile.getFileNameWithoutExtension() + "_Media");
        else
            folder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("BSM DAW Recordings");
        folder.createDirectory();

        const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
        const auto file = folder.getNonexistentChildFile("Recording_" + stamp + "_Audio_" + juce::String(recordingTrack + 1), ".wav");
        if (!recorder.writeToWav(file))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Recording", "The recording could not be written to WAV.", "OK");
            owner.repaint();
            return;
        }

        juce::String error;
        if (!owner.audioEngine.loadAudioFileIntoTrack(recordingTrack, file, error))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Liberty - Recording", error, "OK");
            owner.repaint();
            return;
        }

        owner.audioEngine.setTrackStartSeconds(recordingTrack, recordingStartSeconds);
        owner.trackSourceFiles[static_cast<size_t>(recordingTrack)] = file;
        owner.selectedTrack = recordingTrack;
        owner.playheadSeconds = recordingStartSeconds + owner.audioEngine.getAudioFileLengthSeconds(recordingTrack);
        owner.audioEngine.setCurrentTimeSeconds(owner.playheadSeconds);
        owner.rebuildWaveformCache(recordingTrack);
        owner.repaint();
    }

    MainComponent& owner;
    Recorder recorder;
    int recordingTrack = -1;
    double recordingStartSeconds = 0.0;
    double savedExtraLength = 0.0;
};

class Bootstrap final : private juce::Timer
{
public:
    Bootstrap() { startTimerHz(20); }
private:
    void timerCallback() override
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
        {
            auto* window = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i));
            if (window == nullptr) continue;
            auto* main = dynamic_cast<MainComponent*>(window->getContentComponent());
            if (main == nullptr) continue;
            if (controllers.find(main) == controllers.end())
                controllers.emplace(main, std::make_unique<RecordingController>(*main));
        }
    }
    std::map<MainComponent*, std::unique_ptr<RecordingController>> controllers;
};

Bootstrap bootstrap;
}
