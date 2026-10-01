#define private public
#include "MainComponent.h"
#undef private

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <map>
#include <vector>

int getLibertyTrackRowHeight() noexcept;

class LibertyAudioRecordingController final : public juce::Component, private juce::Timer
{
public:
    explicit LibertyAudioRecordingController(MainComponent& ownerIn) : owner(ownerIn)
    {
        captureDeviceSignature();
        syncTrackControls();
        for (int i = 0; i < owner.getAudioTrackCount(); ++i)
        {
            armButtons[(size_t)i]->setButtonText("ARM");
            armButtons[(size_t)i]->setClickingTogglesState(false);
            armButtons[(size_t)i]->setMouseClickGrabsKeyboardFocus(false);
            armButtons[(size_t)i]->onClick = [this, i] { armTrack(i); };
            owner.addAndMakeVisible(*armButtons[(size_t)i]);

            monitorButtons[(size_t)i]->setButtonText("MON OFF");
            monitorButtons[(size_t)i]->setClickingTogglesState(true);
            monitorButtons[(size_t)i]->setMouseClickGrabsKeyboardFocus(false);
            monitorButtons[(size_t)i]->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
            monitorButtons[(size_t)i]->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2d6f8f));
            monitorButtons[(size_t)i]->onClick = [this, i] { handleMonitorClick(i); };
            owner.addAndMakeVisible(*monitorButtons[(size_t)i]);
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
        for (auto& state : monitoringEnabled)
            state->store(false, std::memory_order_relaxed);

        owner.audioEngine.setInputMonitoring(false);
        stopRecording(false);
        detachAudioCallback();
        stopTimer();

        for (auto& button : armButtons)
            if (button) button->setVisible(false);
        for (auto& button : monitorButtons)
            if (button) button->setVisible(false);
        recButton.setVisible(false);
    }

    void prepareForProjectReset()
    {
        if (recording)
            stopRecording(false);
        clearMonitoringState();
        armedTrack = -1;
        for (auto& button : armButtons)
            if (button) button->setButtonText("ARM");
        updateMonitoringCallback();
    }

    void resized() override
    {
        const int rowH = getLibertyTrackRowHeight();
        for (int i = 0; i < owner.getAudioTrackCount(); ++i)
        {
            const int y = 76 + 32 + (i - owner.getTrackScrollRows()) * rowH + 7;
            const bool visible = y >= 76 + 32 && y < owner.getHeight() - 210;
            armButtons[(size_t)i]->setVisible(visible);
            monitorButtons[(size_t)i]->setVisible(visible);
            armButtons[(size_t)i]->setBounds(108, y, 46, 22);
            monitorButtons[(size_t)i]->setBounds(158, y, 48, 22);
        }

        recButton.setBounds(525, 38, 70, 28);
    }

private:
    bool isArmedTrackMonitoring() const noexcept
    {
        return armedTrack >= 0
            && armedTrack < owner.getAudioTrackCount()
            && armedTrack < (int)monitoringEnabled.size()
            && monitoringEnabled[(size_t)armedTrack]->load(std::memory_order_relaxed);
    }

    void attachAudioCallback()
    {
        if (callbackRegistered)
            return;

        // Recording must not register a second hardware callback. AudioEngine
        // remains the sole owner of device output; recording input will be
        // routed through the central engine in the rebuilt architecture.
        callbackRegistered = false;
    }

    void detachAudioCallback()
    {
        if (!callbackRegistered)
            return;

        callbackRegistered = false;
    }

    void disableInputWhenIdle()
    {
        owner.audioEngine.setInputMonitoring(false);

        auto& manager = owner.audioEngine.getDeviceManager();
        auto setup = manager.getAudioDeviceSetup();
        if (setup.inputChannels.countNumberOfSetBits() == 0 && !setup.useDefaultInputChannels)
            return;

        setup.inputChannels.clear();
        setup.useDefaultInputChannels = false;
        if (manager.setAudioDeviceSetup(setup, true).isNotEmpty())
            return;

        inputIndices.clear();
        captureDeviceSignature();
    }

    void handleMonitorClick(int trackIndex)
    {
        const bool enabled = monitorButtons[(size_t)trackIndex]->getToggleState();
        for (int track = 0; track < (int) monitoringEnabled.size(); ++track)
        {
            const bool active = enabled && track == trackIndex;
            monitoringEnabled[(size_t)track]->store(active, std::memory_order_relaxed);
            monitorButtons[(size_t)track]->setToggleState(active, juce::dontSendNotification);
            monitorButtons[(size_t)track]->setButtonText(active ? "MON ON" : "MON OFF");
        }

        if (enabled && configureInput())
        {
            const int left = inputIndices.empty() ? 0 : inputIndices[0];
            const int right = inputIndices.size() > 1 ? inputIndices[1] : left;
            owner.audioEngine.setInputMonitoring(true, left, right);
            return;
        }

        owner.audioEngine.setInputMonitoring(false);
        if (enabled)
        {
            monitoringEnabled[(size_t)trackIndex]->store(false, std::memory_order_relaxed);
            monitorButtons[(size_t)trackIndex]->setToggleState(false, juce::dontSendNotification);
            monitorButtons[(size_t)trackIndex]->setButtonText("MON OFF");
        }
        updateMonitoringCallback();
    }

    void clearMonitoringState()
    {
        for (int track = 0; track < (int) monitoringEnabled.size(); ++track)
        {
            monitoringEnabled[(size_t)track]->store(false, std::memory_order_relaxed);
            monitorButtons[(size_t)track]->setToggleState(false, juce::dontSendNotification);
            monitorButtons[(size_t)track]->setButtonText("MON OFF");
        }
        owner.audioEngine.setInputMonitoring(false);
    }

    void updateMonitoringCallback()
    {
        if (recording)
        {
            attachAudioCallback();
            return;
        }

        if (isArmedTrackMonitoring())
        {
            if (configureInput())
            {
                attachAudioCallback();
                return;
            }

            clearMonitoringState();
            detachAudioCallback();
            disableInputWhenIdle();
        }
        else
        {
            detachAudioCallback();
            disableInputWhenIdle();
        }
    }

    void armTrack(int track)
    {
        if (recording)
            return;

        if (armedTrack != track && isArmedTrackMonitoring())
            clearMonitoringState();

        armedTrack = track;
        for (int i = 0; i < owner.getAudioTrackCount(); ++i)
            armButtons[(size_t)i]->setButtonText(i == armedTrack ? "ARMED" : "ARM");

        owner.selectedTrack = track;
        updateMonitoringCallback();
        owner.repaint();
    }

    void toggleRecording()
    {
        recording ? stopRecording(true) : startRecording();
    }

    bool configureInput()
    {
        auto& manager = owner.audioEngine.getDeviceManager();
        auto* device = manager.getCurrentAudioDevice();
        if (device == nullptr)
            return false;

        auto setup = manager.getAudioDeviceSetup();
        const int available = device->getInputChannelNames().size();
        if (available <= 0)
            return false;

        setup.inputChannels.clear();
        for (int i = 0; i < available; ++i)
            setup.inputChannels.setBit(i);
        setup.useDefaultInputChannels = false;

        if (manager.setAudioDeviceSetup(setup, true).isNotEmpty())
            return false;

        device = manager.getCurrentAudioDevice();
        if (device == nullptr)
            return false;

        inputIndices.clear();
        const auto active = device->getActiveInputChannels();
        for (int i = 0; i < available; ++i)
            if (active[i])
                inputIndices.push_back(i);

        if (inputIndices.empty())
            return false;

        captureDeviceSignature();
        return true;
    }

    bool createStereoRecordingFromActiveChannels(juce::File& stereoFile)
    {
        juce::WavAudioFormat wav;
        auto inputStream = recordingFile.createInputStream();
        if (inputStream == nullptr)
            return false;

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
            if (!reader->read(&chunk, 0, samples, position, true, true))
                return false;

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
        for (int i = 0; i < channels; ++i)
            order[(size_t)i] = i;

        std::sort(order.begin(), order.end(), [&energy](int a, int b)
        {
            return energy[(size_t)a] > energy[(size_t)b];
        });

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
        if (output == nullptr)
            return false;

        auto writer = std::unique_ptr<juce::AudioFormatWriter>(wav.createWriterFor(
            output.release(), reader->sampleRate, 2, 24, {}, 0));
        if (writer == nullptr)
            return false;

        reader.reset();
        inputStream = recordingFile.createInputStream();
        if (inputStream == nullptr)
            return false;

        reader.reset(wav.createReaderFor(inputStream.release(), true));
        if (reader == nullptr)
            return false;

        juce::AudioBuffer<float> source(channels, chunkSize);
        juce::AudioBuffer<float> stereo(2, chunkSize);
        position = 0;

        while (position < reader->lengthInSamples)
        {
            const int samples = static_cast<int>(juce::jmin<std::int64_t>(chunkSize, reader->lengthInSamples - position));
            source.clear();
            if (!reader->read(&source, 0, samples, position, true, true))
                return false;

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
        if (device == nullptr || device->getCurrentSampleRate() <= 0.0)
        {
            updateMonitoringCallback();
            return;
        }

        recordingFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getNonexistentChildFile("Liberty_Recording", ".wav", false);

        juce::WavAudioFormat wav;
        auto output = recordingFile.createOutputStream();
        if (output == nullptr)
        {
            if (recordingFile.existsAsFile())
                recordingFile.deleteFile();
            recordingFile = juce::File();
            updateMonitoringCallback();
            return;
        }

        auto writer = std::unique_ptr<juce::AudioFormatWriter>(wav.createWriterFor(
            output.release(), device->getCurrentSampleRate(),
            (unsigned int)inputIndices.size(), 24, {}, 0));
        if (writer == nullptr)
        {
            if (recordingFile.existsAsFile())
                recordingFile.deleteFile();
            recordingFile = juce::File();
            updateMonitoringCallback();
            return;
        }

        recordingThread = std::make_unique<juce::TimeSliceThread>("Liberty Recording Writer");
        recordingThread->startThread();
        threadedWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
            writer.release(), *recordingThread, 32768);

        recordingBuffer.setSize((int)inputIndices.size(),
                                 juce::jmax(1, owner.audioEngine.getBufferSize()),
                                 false, false, true);

        recordStartSeconds = owner.audioEngine.getCurrentTimeSeconds();
        recordingWriteFailed.store(false, std::memory_order_relaxed);
        recording = true;
        attachAudioCallback();

        owner.audioEngine.setPlaying(true);
        recButton.setButtonText("STOP");
        owner.repaint();
    }

    void stopRecording(bool createClip)
    {
        if (!recording && threadedWriter == nullptr)
        {
            updateMonitoringCallback();
            return;
        }

        recording = false;
        recordingWriteFailed.store(false, std::memory_order_release);
        owner.audioEngine.setPlaying(false);
        owner.isPlaying = false;

        threadedWriter.reset();
        if (recordingThread != nullptr)
        {
            recordingThread->stopThread(2000);
            recordingThread.reset();
        }

        recButton.setButtonText("REC");
        updateMonitoringCallback();

        if (!createClip)
        {
            if (recordingFile.existsAsFile())
                recordingFile.deleteFile();
            recordingFile = juce::File();
            owner.repaint();
            return;
        }

        if (armedTrack >= 0 && recordingFile.existsAsFile() && recordingFile.getSize() > 44)
        {
            juce::File stereoFile;
            if (!createStereoRecordingFromActiveChannels(stereoFile))
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Liberty - Recording", "Aucun signal audio détectable sur les entrées du périphérique sélectionné.", "OK");
                if (recordingFile.existsAsFile())
                    recordingFile.deleteFile();
                recordingFile = juce::File();
                owner.repaint();
                return;
            }

            juce::String error;
            if (owner.audioEngine.loadAudioFileIntoTrack(armedTrack, stereoFile, error))
            {
                owner.trackSourceFiles[(size_t)armedTrack] = stereoFile;
                owner.pendingAudioFileNames[(size_t)armedTrack].clear();
                owner.pendingAudioLengths[(size_t)armedTrack] = 0.0;
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

        if (recordingFile.existsAsFile())
            recordingFile.deleteFile();
        recordingFile = juce::File();
        owner.repaint();
    }

public:
    void processInputBlock(const float* const* inputChannelData, int numInputChannels, int numSamples)
    {
        if (inputChannelData == nullptr || !recording || threadedWriter == nullptr) return;
        const int channels = (int) inputIndices.size();
        if (recordingBuffer.getNumSamples() < numSamples || recordingBuffer.getNumChannels() != channels) return;
        for (int channel = 0; channel < channels; ++channel)
        {
            const int source = inputIndices[(size_t) channel];
            if (source < 0 || source >= numInputChannels || inputChannelData[source] == nullptr)
                recordingBuffer.clear(channel, 0, numSamples);
            else
                recordingBuffer.copyFrom(channel, 0, inputChannelData[source], numSamples);
        }
        if (!threadedWriter->write(recordingBuffer.getArrayOfReadPointers(), numSamples))
            recordingWriteFailed.store(true, std::memory_order_release);
    }

    void timerCallback() override
    {
        if (recordingWriteFailed.exchange(false, std::memory_order_acq_rel))
        {
            stopRecording(false);
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                "Liberty - Recording", "Recording stopped because audio data could not be written fast enough.", "OK");
        }

        handleDeviceChange();
        syncTrackControls();
        resized();
        if (recording)
        {
            owner.playheadSeconds = owner.audioEngine.getCurrentTimeSeconds();
            owner.isPlaying = true;
        }
        owner.repaint();
    }

    void syncTrackControls()
    {
        const int count = owner.getAudioTrackCount();

        if ((int)armButtons.size() > count)
        {
            if (recording && armedTrack >= count)
                stopRecording(false);

            if (armedTrack >= count)
            {
                clearMonitoringState();
                armedTrack = -1;
                updateMonitoringCallback();
            }

            while ((int)armButtons.size() > count)
            {
                armButtons.pop_back();
                monitorButtons.pop_back();
                monitoringEnabled.pop_back();
            }
        }

        while ((int)armButtons.size() < count)
        {
            const int i = (int)armButtons.size();
            auto arm = std::make_unique<juce::TextButton>("ARM");
            arm->setClickingTogglesState(false); arm->setMouseClickGrabsKeyboardFocus(false);
            arm->onClick = [this, i] { armTrack(i); }; owner.addAndMakeVisible(*arm); armButtons.push_back(std::move(arm));
            auto mon = std::make_unique<juce::TextButton>("MON OFF");
            mon->setClickingTogglesState(true); mon->setMouseClickGrabsKeyboardFocus(false);
            mon->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
            mon->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2d6f8f));
            auto state = std::make_unique<std::atomic<bool>>(false);
            mon->onClick = [this, i] { handleMonitorClick(i); };
            owner.addAndMakeVisible(*mon); monitorButtons.push_back(std::move(mon)); monitoringEnabled.push_back(std::move(state));
        }
    }

    void captureDeviceSignature()
    {
        deviceSignatureName = owner.audioEngine.getDeviceName();
        deviceSignatureRate = owner.audioEngine.getSampleRate();
        deviceSignatureBuffer = owner.audioEngine.getBufferSize();
    }

    void handleDeviceChange()
    {
        const auto name = owner.audioEngine.getDeviceName();
        const auto rate = owner.audioEngine.getSampleRate();
        const auto buffer = owner.audioEngine.getBufferSize();
        if (name == deviceSignatureName && rate == deviceSignatureRate && buffer == deviceSignatureBuffer)
            return;

        deviceSignatureName = name;
        deviceSignatureRate = rate;
        deviceSignatureBuffer = buffer;
        inputIndices.clear();

        if (recording)
        {
            stopRecording(false);
            return;
        }

        if (isArmedTrackMonitoring())
        {
            if (configureInput())
            {
                const int left = inputIndices.empty() ? 0 : inputIndices[0];
                const int right = inputIndices.size() > 1 ? inputIndices[1] : left;
                owner.audioEngine.setInputMonitoring(true, left, right);
                captureDeviceSignature();
                return;
            }

            clearMonitoringState();
        }

        disableInputWhenIdle();
        captureDeviceSignature();
    }

    MainComponent& owner;
    std::vector<std::unique_ptr<juce::TextButton>> armButtons;
    std::vector<std::unique_ptr<juce::TextButton>> monitorButtons;
    std::vector<std::unique_ptr<std::atomic<bool>>> monitoringEnabled;
    juce::TextButton recButton;
    std::vector<int> inputIndices;
    std::unique_ptr<juce::TimeSliceThread> recordingThread;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    juce::AudioBuffer<float> recordingBuffer;
    juce::File recordingFile;
    double recordStartSeconds = 0.0;
    int armedTrack = -1;
    bool recording = false;
    std::atomic<bool> recordingWriteFailed { false };
    bool callbackRegistered = false;
    juce::String deviceSignatureName;
    double deviceSignatureRate = 0.0;
    int deviceSignatureBuffer = 0;
};

class LibertyAudioRecordingBootstrap final : private juce::Timer
{
public:
    LibertyAudioRecordingBootstrap() { startTimerHz(10); }
    ~LibertyAudioRecordingBootstrap() override { shutdown(); }

    void processInput(AudioEngine* engine, const float* const* inputs, int numInputs, int numSamples)
    {
        if (engine == nullptr) return;
        for (auto& entry : controllers)
            if (entry.first != nullptr && &entry.first->audioEngine == engine && entry.second)
            {
                entry.second->processInputBlock(inputs, numInputs, numSamples);
                return;
            }
    }

    void prepareForProjectReset(MainComponent* owner)
    {
        auto it = controllers.find(owner);
        if (it != controllers.end() && it->second)
            it->second->prepareForProjectReset();
    }

    void shutdown()
    {
        stopTimer();
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
                    {
                        controllers.emplace(main, std::make_unique<LibertyAudioRecordingController>(*main));
                        stopTimer();
                        return;
                    }
    }

    std::map<MainComponent*, std::unique_ptr<LibertyAudioRecordingController>> controllers;
};

static LibertyAudioRecordingBootstrap libertyAudioRecordingBootstrap;

void processLibertyRecordingInput(AudioEngine* engine, const float* const* inputs, int numInputs, int numSamples)
{
    libertyAudioRecordingBootstrap.processInput(engine, inputs, numInputs, numSamples);
}

void prepareLibertyAudioRecordingForProjectReset(MainComponent* owner)
{
    if (owner != nullptr)
        libertyAudioRecordingBootstrap.prepareForProjectReset(owner);
}

void shutdownLibertyAudioRecordingController()
{
    libertyAudioRecordingBootstrap.shutdown();
}
