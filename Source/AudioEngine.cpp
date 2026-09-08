#include "AudioEngine.h"
#include <cmath>

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    shutdown();
}

bool AudioEngine::initialise()
{
    const auto error = deviceManager.initialiseWithDefaultDevices(0, 2);

    if (error.isNotEmpty())
    {
        const juce::ScopedLock lock(stateLock);
        lastError = error;
        initialised.store(false);
        return false;
    }

    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
    {
        const juce::ScopedLock lock(stateLock);
        lastError = "No audio output device is available.";
        initialised.store(false);
        return false;
    }

    deviceManager.addAudioCallback(this);

    {
        const juce::ScopedLock lock(stateLock);
        deviceName = device->getName();
        lastError.clear();
    }

    sampleRate.store(device->getCurrentSampleRate());
    bufferSize.store(device->getCurrentBufferSizeSamples());
    outputChannels.store(device->getActiveOutputChannels().countNumberOfSetBits());
    initialised.store(true);
    return true;
}

void AudioEngine::shutdown()
{
    if (initialised.exchange(false))
        deviceManager.removeAudioCallback(this);

    deviceManager.closeAudioDevice();
    sampleRate.store(0.0);
    bufferSize.store(0);
    outputChannels.store(0);
}

juce::String AudioEngine::getDeviceName() const
{
    const juce::ScopedLock lock(stateLock);
    return deviceName;
}

juce::String AudioEngine::getLastError() const
{
    const juce::ScopedLock lock(stateLock);
    return lastError;
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device == nullptr)
        return;

    sampleRate.store(device->getCurrentSampleRate());
    bufferSize.store(device->getCurrentBufferSizeSamples());
    outputChannels.store(device->getActiveOutputChannels().countNumberOfSetBits());

    const auto rate = device->getCurrentSampleRate();
    phase = 0.0;
    phaseIncrement = rate > 0.0 ? (440.0 * juce::MathConstants<double>::twoPi / rate) : 0.0;
}

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const*,
                                                    int,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    // Real-time safe first audio path: generate a quiet 440 Hz test tone while playing.
    // No allocation, locks, file I/O or GUI work occurs in the callback.
    const bool shouldPlay = playing.load(std::memory_order_relaxed);

    for (int channel = 0; channel < numOutputChannels; ++channel)
    {
        auto* output = outputChannelData[channel];
        if (output == nullptr)
            continue;

        if (!shouldPlay)
        {
            juce::FloatVectorOperations::clear(output, numSamples);
            continue;
        }

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto value = static_cast<float>(0.05 * std::sin(phase));
            output[sample] = value;
            phase += phaseIncrement;
            if (phase >= juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;
        }
    }
}

void AudioEngine::audioDeviceStopped()
{
    playing.store(false, std::memory_order_relaxed);
}
