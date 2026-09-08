#include "AudioEngine.h"
#include <cmath>
#include <limits>

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
    transportSamples.store(0, std::memory_order_relaxed);
    audioFileLoaded.store(false, std::memory_order_relaxed);
    audioFileLengthSeconds.store(0.0, std::memory_order_relaxed);
    audioBuffer.reset();
    audioFileNumSamples = 0;
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

double AudioEngine::getCurrentTimeSeconds() const noexcept
{
    const auto rate = sampleRate.load(std::memory_order_relaxed);
    if (rate <= 0.0)
        return 0.0;

    return static_cast<double>(transportSamples.load(std::memory_order_relaxed)) / rate;
}

bool AudioEngine::loadAudioFile(const juce::File& file, juce::String& error)
{
    error.clear();

    if (!file.existsAsFile())
    {
        error = "The selected audio file does not exist.";
        return false;
    }

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr)
    {
        error = "BSM DAW could not read this audio format. Use WAV, AIFF or AIF.";
        return false;
    }

    const auto outputRate = sampleRate.load(std::memory_order_relaxed);
    if (outputRate <= 0.0)
    {
        error = "No audio device is available.";
        return false;
    }

    if (std::abs(reader->sampleRate - outputRate) > 0.01)
    {
        error = "This audio file uses " + juce::String(reader->sampleRate, 0)
              + " Hz, while the current device uses " + juce::String(outputRate, 0)
              + " Hz. Sample-rate conversion will be added to the audio import engine.";
        return false;
    }

    if (reader->lengthInSamples <= 0 || reader->lengthInSamples > std::numeric_limits<int>::max())
    {
        error = "The selected audio file is too large to load into memory.";
        return false;
    }

    const auto numSamples = static_cast<int>(reader->lengthInSamples);
    const auto numChannels = juce::jmax(1, juce::jmin(2, reader->numChannels));
    auto newBuffer = std::make_unique<juce::AudioBuffer<float>>(numChannels, numSamples);
    newBuffer->clear();

    if (!reader->read(newBuffer.get(), 0, numSamples, 0, true, true))
    {
        error = "Failed to decode the selected audio file.";
        return false;
    }

    playing.store(false, std::memory_order_relaxed);
    resetTransport();

    if (initialised.load(std::memory_order_relaxed))
        deviceManager.removeAudioCallback(this);

    audioBuffer = std::move(newBuffer);
    audioFileNumSamples = reader->lengthInSamples;

    {
        const juce::ScopedLock lock(stateLock);
        audioFileName = file.getFileName();
        lastError.clear();
    }

    audioFileLengthSeconds.store(static_cast<double>(audioFileNumSamples) / outputRate,
                                 std::memory_order_relaxed);
    audioFileLoaded.store(true, std::memory_order_release);

    if (initialised.load(std::memory_order_relaxed))
        deviceManager.addAudioCallback(this);

    return true;
}

void AudioEngine::clearAudioFile()
{
    playing.store(false, std::memory_order_relaxed);
    resetTransport();

    if (initialised.load(std::memory_order_relaxed))
        deviceManager.removeAudioCallback(this);

    audioFileLoaded.store(false, std::memory_order_release);
    audioFileLengthSeconds.store(0.0, std::memory_order_relaxed);
    audioBuffer.reset();
    audioFileNumSamples = 0;

    {
        const juce::ScopedLock lock(stateLock);
        audioFileName.clear();
    }

    if (initialised.load(std::memory_order_relaxed))
        deviceManager.addAudioCallback(this);
}

juce::String AudioEngine::getAudioFileName() const
{
    const juce::ScopedLock lock(stateLock);
    return audioFileName;
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

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const*, int,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels, int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    const bool shouldPlay = playing.load(std::memory_order_relaxed);

    if (!shouldPlay)
    {
        for (int channel = 0; channel < numOutputChannels; ++channel)
            if (outputChannelData[channel] != nullptr)
                juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
        return;
    }

    if (audioFileLoaded.load(std::memory_order_acquire) && audioBuffer != nullptr)
    {
        const auto position = transportSamples.load(std::memory_order_relaxed);
        const auto available = juce::jmax<std::int64_t>(0, audioFileNumSamples - position);
        const auto samplesToCopy = static_cast<int>(juce::jmin<std::int64_t>(numSamples, available));
        const auto sourceChannels = audioBuffer->getNumChannels();

        for (int channel = 0; channel < numOutputChannels; ++channel)
        {
            auto* output = outputChannelData[channel];
            if (output == nullptr)
                continue;

            if (channel < 2 && sourceChannels > 0 && samplesToCopy > 0)
            {
                const auto sourceChannel = sourceChannels == 1 ? 0 : channel;
                juce::FloatVectorOperations::copy(output,
                                                   audioBuffer->getReadPointer(sourceChannel)
                                                       + static_cast<int>(position),
                                                   samplesToCopy);
            }

            if (samplesToCopy < numSamples)
                juce::FloatVectorOperations::clear(output + samplesToCopy,
                                                   numSamples - samplesToCopy);
        }

        if (samplesToCopy > 0)
            transportSamples.fetch_add(samplesToCopy, std::memory_order_relaxed);

        if (samplesToCopy < numSamples)
            playing.store(false, std::memory_order_relaxed);

        return;
    }

    for (int channel = 0; channel < numOutputChannels; ++channel)
    {
        auto* output = outputChannelData[channel];
        if (output == nullptr)
            continue;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto value = static_cast<float>(0.05 * std::sin(phase));
            output[sample] = value;
            phase += phaseIncrement;
            if (phase >= juce::MathConstants<double>::twoPi)
                phase -= juce::MathConstants<double>::twoPi;
        }
    }

    transportSamples.fetch_add(numSamples, std::memory_order_relaxed);
}

void AudioEngine::audioDeviceStopped()
{
    playing.store(false, std::memory_order_relaxed);
}
