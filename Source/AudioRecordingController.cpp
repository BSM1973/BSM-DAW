#define private public
#include "MainComponent.h"
#undef private

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <memory>
#include <map>

class LibertyAudioRecordingController final : public juce::Component, private juce::AudioIODeviceCallback, private juce::Timer
{
public:
    explicit LibertyAudioRecordingController(MainComponent& ownerIn) : owner(ownerIn)
    {
        for (int i = 0; i < AudioEngine::maxAudioTracks; ++i)
        {
            armButtons[(size_t)i].setButtonText("ARM");
            armButtons[(size_t)i].setClickingTogglesState(false);
            armButtons[(size_t)i].setMouseClickGrabsKeyboardFocus(false);
            armButtons[(size_t)i].onClick = [this, i] { armTrack(i); };
            owner.addAndMakeVisible(armButtons[(size_t)i]);
        }
        recButton.setButtonText("REC");
        recButton.setClickingTogglesState(false);
        recButton.setMouseClickGrabsKeyboardFocus(false);
        recButton.onClick = [this] { toggleRecording(); };
        owner.addAndMakeVisible(recButton);
        startTimerHz(20);
    }

    ~LibertyAudioRecordingController() override
    {
        stopRecording(false);
        stopTimer();
        for (auto& button : armButtons) button.setVisible(false);
        recButton.setVisible(false);
    }

    void resized() override
    {
        for (int i = 0; i < AudioEngine::maxAudioTracks; ++i)
            armButtons[(size_t)i].setBounds(150, 76 + 32 + i * 70 + 8, 48, 22);
        recButton.setBounds(1185, 10, 70, 24);
    }

private:
    void armTrack(int track)
    {
        if (recording) return;
        armedTrack = track;
        for (int i = 0; i < AudioEngine::maxAudioTracks; ++i)
            armButtons[(size_t)i].setButtonText(i == armedTrack ? "ARMED" : "ARM");
        owner.selectedTrack = track;
        owner.repaint();
    }

    void toggleRecording() { recording ? stopRecording(true) : startRecording(); }

    bool configureInput()
    {
        auto& manager = owner.audioEngine.getDeviceManager();
        auto* device = manager.getCurrentAudioDevice();
        if (device == nullptr) return false;

        auto setup = manager.getAudioDeviceSetup();
        const int available = device->getInputChannelNames().size();
        if (available <= 0) return false;

        if (setup.inputChannels.countNumberOfSetBits() == 0)
        {
            const int wanted = juce::jmin(2, available);
            setup.inputChannels.clear();
            for (int i = 0; i < wanted; ++i)
                setup.inputChannels.setBit(i);
            setup.useDefaultInputChannels = false;

            if (manager.setAudioDeviceSetup(setup, true).isNotEmpty())
                return false;

            device = manager.getCurrentAudioDevice();
            if (device == nullptr) return false;
        }

        inputIndices.clear();
        const auto active = device->getActiveInputChannels();
        for (int i = 0; i < available && (int)inputIndices.size() < 2; ++i)
            if (active[i]) inputIndices.push_back(i);

        return !inputIndices.empty();
    }

    void startRecording()
    {
        if (armedTrack < 0)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                "Liberty - Recording", "Arm an Audio track before pressing REC.", "OK");
            return;
        }

        if (!configureInput())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                "Liberty - Recording", "No active input is available on the selected audio device.", "OK");
            return;
        }

        auto& manager = owner.audioEngine.getDeviceManager();
        auto* device = manager.getCurrentAudioDevice();
        if (device == nullptr || device->getCurrentSampleRate() <= 0.0) return;

        recordingFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getNonexistentChildFile("Liberty_Recording", ".wav", false);

        juce::WavAudioFormat wav;
        auto output = recordingFile.createOutputStream();
        if (output == nullptr) return;

        auto writer = std::unique_ptr<juce::AudioFormatWriter>(wav.createWriterFor(
            output.release(), device->getCurrentSampleRate(),
            (unsigned int)inputIndices.size(), 24, {}, 0));
        if (writer == nullptr) return;

        recordingThread = std::make_unique<juce::TimeSliceThread>("Liberty Recording Writer");
        recordingThread->startThread();
        threadedWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
            writer.release(), *recordingThread, 32768);

        recordingBuffer.setSize((int)inputIndices.size(),
                                 juce::jmax(1, owner.audioEngine.getBufferSize()),
                                 false, false, true);

        recordStartSeconds = owner.audioEngine.getCurrentTimeSeconds();
        recording = true;
        manager.addAudioCallback(this);
        owner.audioEngine.setPlaying(true);
        recButton.setButtonText("STOP");
        owner.repaint();
    }

    void stopRecording(bool createClip)
    {
        if (!recording && threadedWriter == nullptr) return;

        recording = false;
        owner.audioEngine.setPlaying(false);
        owner.audioEngine.getDeviceManager().removeAudioCallback(this);

        // Destroy the writer first so all queued audio is flushed to disk.
        threadedWriter.reset();
        if (recordingThread != nullptr)
        {
            recordingThread->stopThread(2000);
            recordingThread.reset();
        }

        recButton.setButtonText("REC");

        if (createClip && armedTrack >= 0 && recordingFile.existsAsFile() && recordingFile.getSize() > 44)
        {
            juce::String error;
            if (owner.audioEngine.loadAudioFileIntoTrack(armedTrack, recordingFile, error))
            {
                owner.trackSourceFiles[(size_t)armedTrack] = recordingFile;
                owner.audioEngine.setTrackStartSeconds(armedTrack, recordStartSeconds);
                owner.rebuildWaveformCache(armedTrack);
                owner.selectedTrack = armedTrack;
                owner.repaint();
            }
            else
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Liberty - Recording", "Recording could not be loaded: " + error, "OK");
            }
        }

        // Keep the file alive: trackSourceFiles must remain valid after recording.
        owner.repaint();
    }

    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override
    {
        // This callback is registered alongside AudioEngine. JUCE sums the outputs
        // of all callbacks, so this callback must explicitly contribute silence.
        for (int channel = 0; channel < numOutputChannels; ++channel)
            if (outputChannelData != nullptr && outputChannelData[channel] != nullptr)
                juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);

        if (!recording || threadedWriter == nullptr || inputChannelData == nullptr)
            return;

        const int channels = (int)inputIndices.size();
        if (recordingBuffer.getNumSamples() < numSamples || recordingBuffer.getNumChannels() != channels)
            return;

        for (int channel = 0; channel < channels; ++channel)
        {
            const int source = inputIndices[(size_t)channel];
            if (source < 0 || source >= numInputChannels || inputChannelData[source] == nullptr)
                recordingBuffer.clear(channel, 0, numSamples);
            else
                recordingBuffer.copyFrom(channel, 0, inputChannelData[source], numSamples);
        }

        threadedWriter->write(recordingBuffer.getArrayOfReadPointers(), numSamples);
    }

    void timerCallback() override
    {
        resized();
        if (recording)
        {
            owner.playheadSeconds = owner.audioEngine.getCurrentTimeSeconds();
            owner.isPlaying = true;
        }
        owner.repaint();
    }

    MainComponent& owner;
    std::array<juce::TextButton, AudioEngine::maxAudioTracks> armButtons;
    juce::TextButton recButton;
    std::vector<int> inputIndices;
    std::unique_ptr<juce::TimeSliceThread> recordingThread;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    juce::AudioBuffer<float> recordingBuffer;
    juce::File recordingFile;
    double recordStartSeconds = 0.0;
    int armedTrack = -1;
    bool recording = false;
};

class LibertyAudioRecordingBootstrap final : private juce::Timer
{
public:
    LibertyAudioRecordingBootstrap() { startTimerHz(10); }
    ~LibertyAudioRecordingBootstrap() override { shutdown(); }
    void shutdown() { stopTimer(); controllers.clear(); }

private:
    void timerCallback() override
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i)))
                if (auto* main = dynamic_cast<MainComponent*>(window->getContentComponent()))
                    if (controllers.find(main) == controllers.end())
                        controllers.emplace(main, std::make_unique<LibertyAudioRecordingController>(*main));
    }

    std::map<MainComponent*, std::unique_ptr<LibertyAudioRecordingController>> controllers;
};

static LibertyAudioRecordingBootstrap libertyAudioRecordingBootstrap;
void shutdownLibertyAudioRecordingController() { libertyAudioRecordingBootstrap.shutdown(); }
