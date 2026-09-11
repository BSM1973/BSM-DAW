#define private public
#include "MainComponent.h"
#undef private

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <map>
#include <vector>

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

        setup.inputChannels.clear();
        for (int i = 0; i < available; ++i)
            setup.inputChannels.setBit(i);
        setup.useDefaultInputChannels = false;

        if (manager.setAudioDeviceSetup(setup, true).isNotEmpty())
            return false;

        device = manager.getCurrentAudioDevice();
        if (device == nullptr) return false;

        inputIndices.clear();
        const auto active = device->getActiveInputChannels();
        for (int i = 0; i < available; ++i)
            if (active[i]) inputIndices.push_back(i);

        return !inputIndices.empty();
    }

    bool createStereoRecordingFromActiveChannels(juce::File& stereoFile)
    {
        juce::WavAudioFormat wav;
        auto inputStream = recordingFile.createInputStream();
        if (inputStream == nullptr) return false;
        std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(inputStream.release(), true));
        if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels <= 0)
            return false;

        const int channels = static_cast<int>(reader->numChannels);
        const int chunkSize = 8192;
        juce::AudioBuffer<float> chunk(channels, chunkSize);
        std::vector<double> energy((size_t)channels, 0.0);
        std::vector<std::int64_t> counts((size_t)channels, 0);

        std::int64_t position = 0;
        while (position < reader->lengthInSamples)
        {
            const int samples = static_cast<int>(juce::jmin<std::int64_t>(chunkSize, reader->lengthInSamples - position));
            chunk.clear();
            if (!reader->read(&chunk, 0, samples, position, true, true)) return false;
            for (int ch = 0; ch < channels; ++ch)
            {
                const float* data = chunk.getReadPointer(ch);
                double sum = 0.0;
                for (int n = 0; n < samples; ++n)
                    sum += static_cast<double>(data[n]) * static_cast<double>(data[n]);
                energy[(size_t)ch] += sum;
                counts[(size_t)ch] += samples;
            }
            position += samples;
        }

        std::vector<int> order((size_t)channels);
        for (int i = 0; i < channels; ++i) order[(size_t)i] = i;
        std::sort(order.begin(), order.end(), [&energy](int a, int b) { return energy[(size_t)a] > energy[(size_t)b]; });

        const double bestRms = counts[(size_t)order[0]] > 0
            ? std::sqrt(energy[(size_t)order[0]] / static_cast<double>(counts[(size_t)order[0]]))
            : 0.0;
        if (bestRms < 0.00001)
            return false;

        const int leftChannel = order[0];
        const int rightChannel = channels > 1 ? order[1] : order[0];
        stereoFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getNonexistentChildFile("Liberty_Recording_Stereo", ".wav", false);
        auto output = stereoFile.createOutputStream();
        if (output == nullptr) return false;

        auto writer = std::unique_ptr<juce::AudioFormatWriter>(wav.createWriterFor(
            output.release(), reader->sampleRate, 2, 24, {}, 0));
        if (writer == nullptr) return false;

        reader.reset();
        inputStream = recordingFile.createInputStream();
        if (inputStream == nullptr) return false;
        reader.reset(wav.createReaderFor(inputStream.release(), true));
        if (reader == nullptr) return false;
        juce::AudioBuffer<float> source(channels, chunkSize);
        juce::AudioBuffer<float> stereo(2, chunkSize);
        position = 0;
        while (position < reader->lengthInSamples)
        {
            const int samples = static_cast<int>(juce::jmin<std::int64_t>(chunkSize, reader->lengthInSamples - position));
            source.clear();
            if (!reader->read(&source, 0, samples, position, true, true)) return false;
            stereo.copyFrom(0, 0, source, leftChannel, 0, samples);
            stereo.copyFrom(1, 0, source, rightChannel, 0, samples);
            writer->writeFromAudioSampleBuffer(stereo, 0, samples);
            position += samples;
        }
        writer.reset();
        return stereoFile.existsAsFile() && stereoFile.getSize() > 44;
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
                "Liberty - Recording", "Liberty could not activate the input channels on the selected audio device.", "OK");
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

        threadedWriter.reset();
        if (recordingThread != nullptr)
        {
            recordingThread->stopThread(2000);
            recordingThread.reset();
        }

        recButton.setButtonText("REC");

        if (createClip && armedTrack >= 0 && recordingFile.existsAsFile() && recordingFile.getSize() > 44)
        {
            juce::File stereoFile;
            if (!createStereoRecordingFromActiveChannels(stereoFile))
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Liberty - Recording", "Aucun signal audio détectable sur les entrées du périphérique sélectionné.", "OK");
                owner.repaint();
                return;
            }

            juce::String error;
            if (owner.audioEngine.loadAudioFileIntoTrack(armedTrack, stereoFile, error))
            {
                owner.trackSourceFiles[(size_t)armedTrack] = stereoFile;
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
