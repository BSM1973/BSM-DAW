#include "AudioEngine.h"
#include <cmath>
#include <limits>

AudioEngine::AudioEngine() = default;
AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::initialise()
{
    const auto error = deviceManager.initialiseWithDefaultDevices(0, 2);
    if (error.isNotEmpty()) { const juce::ScopedLock lock(stateLock); lastError = error; initialised.store(false); return false; }
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr) { const juce::ScopedLock lock(stateLock); lastError = "No audio output device is available."; initialised.store(false); return false; }
    deviceManager.addAudioCallback(this);
    { const juce::ScopedLock lock(stateLock); deviceName = device->getName(); lastError.clear(); }
    sampleRate.store(device->getCurrentSampleRate());
    bufferSize.store(device->getCurrentBufferSizeSamples());
    outputChannels.store(device->getActiveOutputChannels().countNumberOfSetBits());
    initialised.store(true);
    return true;
}

void AudioEngine::shutdown()
{
    if (initialised.exchange(false)) deviceManager.removeAudioCallback(this);
    deviceManager.closeAudioDevice();
    sampleRate.store(0.0); bufferSize.store(0); outputChannels.store(0);
    transportSamples.store(0, std::memory_order_relaxed);
    audioFileLoaded.store(false, std::memory_order_relaxed);
    audioFileLengthSeconds.store(0.0, std::memory_order_relaxed);
    audioBuffer.reset(); audioFileNumSamples = 0;
}

juce::String AudioEngine::getDeviceName() const { const juce::ScopedLock lock(stateLock); return deviceName; }
juce::String AudioEngine::getLastError() const { const juce::ScopedLock lock(stateLock); return lastError; }

void AudioEngine::setCurrentTimeSeconds(double seconds) noexcept
{
    const auto rate = sampleRate.load(std::memory_order_relaxed);
    if (rate <= 0.0) return;
    const auto requested = static_cast<std::int64_t>(std::llround(juce::jmax(0.0, seconds) * rate));
    const auto clamped = audioFileNumSamples > 0 ? juce::jlimit<std::int64_t>(0, audioFileNumSamples, requested) : juce::jmax<std::int64_t>(0, requested);
    transportSamples.store(clamped, std::memory_order_relaxed);
}

double AudioEngine::getCurrentTimeSeconds() const noexcept
{
    const auto rate = sampleRate.load(std::memory_order_relaxed);
    return rate > 0.0 ? static_cast<double>(transportSamples.load(std::memory_order_relaxed)) / rate : 0.0;
}

bool AudioEngine::loadAudioFile(const juce::File& file, juce::String& error)
{
    error.clear();
    if (!file.existsAsFile()) { error = "The selected audio file does not exist."; return false; }
    juce::AudioFormatManager formatManager; formatManager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr) { error = "BSM DAW could not read this audio format. Use WAV, AIFF or AIF."; return false; }
    const auto outputRate = sampleRate.load(std::memory_order_relaxed);
    if (outputRate <= 0.0) { error = "No audio device is available."; return false; }
    if (reader->lengthInSamples <= 0 || reader->lengthInSamples > std::numeric_limits<int>::max()) { error = "The selected audio file is too large to load into memory."; return false; }
    const auto inputSamples = static_cast<int>(reader->lengthInSamples);
    const auto inputChannels = juce::jmax(1, juce::jmin(2, static_cast<int>(reader->numChannels)));
    auto decodedBuffer = std::make_unique<juce::AudioBuffer<float>>(inputChannels, inputSamples);
    decodedBuffer->clear();
    if (!reader->read(decodedBuffer.get(), 0, inputSamples, 0, true, true)) { error = "Failed to decode the selected audio file."; return false; }
    const auto sourceRate = reader->sampleRate;
    const auto ratio = sourceRate / outputRate;
    if (ratio <= 0.0) { error = "The selected audio file has an invalid sample rate."; return false; }
    const auto outputSamples64 = static_cast<std::int64_t>(std::floor(static_cast<double>(inputSamples) / ratio));
    if (outputSamples64 <= 0 || outputSamples64 > std::numeric_limits<int>::max()) { error = "The resampled audio file is too large to load into memory."; return false; }
    const auto outputSamples = static_cast<int>(outputSamples64);
    auto newBuffer = std::make_unique<juce::AudioBuffer<float>>(inputChannels, outputSamples);
    newBuffer->clear();
    if (std::abs(sourceRate - outputRate) > 0.01)
        for (int channel = 0; channel < inputChannels; ++channel) { juce::LagrangeInterpolator interpolator; interpolator.reset(); interpolator.process(ratio, decodedBuffer->getReadPointer(channel), newBuffer->getWritePointer(channel), outputSamples); }
    else newBuffer->makeCopyOf(*decodedBuffer);
    playing.store(false, std::memory_order_relaxed); resetTransport();
    if (initialised.load(std::memory_order_relaxed)) deviceManager.removeAudioCallback(this);
    audioBuffer = std::move(newBuffer); audioFileNumSamples = outputSamples;
    { const juce::ScopedLock lock(stateLock); audioFileName = file.getFileName(); lastError.clear(); }
    audioFileLengthSeconds.store(static_cast<double>(audioFileNumSamples) / outputRate, std::memory_order_relaxed);
    audioFileLoaded.store(true, std::memory_order_release);
    if (initialised.load(std::memory_order_relaxed)) deviceManager.addAudioCallback(this);
    return true;
}

void AudioEngine::clearAudioFile()
{
    playing.store(false, std::memory_order_relaxed); resetTransport();
    if (initialised.load(std::memory_order_relaxed)) deviceManager.removeAudioCallback(this);
    audioFileLoaded.store(false, std::memory_order_release); audioFileLengthSeconds.store(0.0, std::memory_order_relaxed);
    audioBuffer.reset(); audioFileNumSamples = 0;
    { const juce::ScopedLock lock(stateLock); audioFileName.clear(); }
    if (initialised.load(std::memory_order_relaxed)) deviceManager.addAudioCallback(this);
}

juce::String AudioEngine::getAudioFileName() const { const juce::ScopedLock lock(stateLock); return audioFileName; }

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device == nullptr) return;
    sampleRate.store(device->getCurrentSampleRate()); bufferSize.store(device->getCurrentBufferSizeSamples());
    outputChannels.store(device->getActiveOutputChannels().countNumberOfSetBits());
    const auto rate = device->getCurrentSampleRate(); phase = 0.0;
    phaseIncrement = rate > 0.0 ? (440.0 * juce::MathConstants<double>::twoPi / rate) : 0.0;
}

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputChannelData, int numOutputChannels, int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    const bool shouldPlay = playing.load(std::memory_order_relaxed);
    if (!shouldPlay) { for (int ch = 0; ch < numOutputChannels; ++ch) if (outputChannelData[ch] != nullptr) juce::FloatVectorOperations::clear(outputChannelData[ch], numSamples); return; }

    if (audioFileLoaded.load(std::memory_order_acquire) && audioBuffer != nullptr)
    {
        const auto position = transportSamples.load(std::memory_order_relaxed);
        const auto available = juce::jmax<std::int64_t>(0, audioFileNumSamples - position);
        const auto samplesToCopy = static_cast<int>(juce::jmin<std::int64_t>(numSamples, available));
        const auto sourceChannels = audioBuffer->getNumChannels();
        const auto muted = trackMuted.load(std::memory_order_relaxed);
        const auto track = muted ? 0.0f : trackGain.load(std::memory_order_relaxed);
        const auto master = masterGain.load(std::memory_order_relaxed);
        const auto pan = trackPan.load(std::memory_order_relaxed);
        const auto leftGain = track * master * (pan > 0.0f ? 1.0f - pan : 1.0f);
        const auto rightGain = track * master * (pan < 0.0f ? 1.0f + pan : 1.0f);

        for (int channel = 0; channel < numOutputChannels; ++channel)
        {
            auto* output = outputChannelData[channel]; if (output == nullptr) continue;
            if (channel < 2 && sourceChannels > 0 && samplesToCopy > 0)
            {
                const auto sourceChannel = sourceChannels == 1 ? 0 : channel;
                juce::FloatVectorOperations::copy(output, audioBuffer->getReadPointer(sourceChannel) + static_cast<int>(position), samplesToCopy);
                juce::FloatVectorOperations::multiply(output, channel == 0 ? leftGain : rightGain, samplesToCopy);
            }
            else juce::FloatVectorOperations::clear(output, samplesToCopy);
            if (samplesToCopy < numSamples) juce::FloatVectorOperations::clear(output + samplesToCopy, numSamples - samplesToCopy);
        }
        if (samplesToCopy > 0) transportSamples.fetch_add(samplesToCopy, std::memory_order_relaxed);
        if (samplesToCopy < numSamples) playing.store(false, std::memory_order_relaxed);
        return;
    }

    const auto master = masterGain.load(std::memory_order_relaxed);
    for (int channel = 0; channel < numOutputChannels; ++channel)
    {
        auto* output = outputChannelData[channel]; if (output == nullptr) continue;
        for (int sample = 0; sample < numSamples; ++sample) { output[sample] = static_cast<float>(0.05 * master * std::sin(phase)); phase += phaseIncrement; if (phase >= juce::MathConstants<double>::twoPi) phase -= juce::MathConstants<double>::twoPi; }
    }
    transportSamples.fetch_add(numSamples, std::memory_order_relaxed);
}

void AudioEngine::audioDeviceStopped() { playing.store(false, std::memory_order_relaxed); }
