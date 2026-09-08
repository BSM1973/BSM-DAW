#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <cstdint>
#include <memory>

class AudioEngine final : private juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    bool initialise();
    void shutdown();

    bool isInitialised() const noexcept { return initialised.load(); }
    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    const juce::AudioDeviceManager& getDeviceManager() const noexcept { return deviceManager; }
    juce::String getDeviceName() const;
    double getSampleRate() const noexcept { return sampleRate.load(); }
    int getBufferSize() const noexcept { return bufferSize.load(); }
    int getOutputChannels() const noexcept { return outputChannels.load(); }
    juce::String getLastError() const;

    void setPlaying(bool shouldPlay) noexcept { playing.store(shouldPlay, std::memory_order_relaxed); }
    bool isPlaying() const noexcept { return playing.load(std::memory_order_relaxed); }
    void resetTransport() noexcept { transportSamples.store(0, std::memory_order_relaxed); }
    void setCurrentTimeSeconds(double seconds) noexcept;
    double getCurrentTimeSeconds() const noexcept;

    bool loadAudioFile(const juce::File& file, juce::String& error);
    void clearAudioFile();
    bool hasAudioFile() const noexcept { return audioFileLoaded.load(std::memory_order_relaxed); }
    juce::String getAudioFileName() const;
    double getAudioFileLengthSeconds() const noexcept { return audioFileLengthSeconds.load(std::memory_order_relaxed); }
    const juce::AudioBuffer<float>* getAudioBuffer() const noexcept { return audioBuffer.get(); }

private:
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    juce::AudioDeviceManager deviceManager;
    std::atomic<bool> initialised { false };
    std::atomic<bool> playing { false };
    std::atomic<bool> audioFileLoaded { false };
    std::atomic<double> sampleRate { 0.0 };
    std::atomic<int> bufferSize { 0 };
    std::atomic<int> outputChannels { 0 };
    std::atomic<std::int64_t> transportSamples { 0 };
    std::atomic<double> audioFileLengthSeconds { 0.0 };
    double phase = 0.0;
    double phaseIncrement = 0.0;
    mutable juce::CriticalSection stateLock;
    juce::String deviceName;
    juce::String lastError;
    juce::String audioFileName;
    std::unique_ptr<juce::AudioBuffer<float>> audioBuffer;
    std::int64_t audioFileNumSamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
