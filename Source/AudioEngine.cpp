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
    playing.store(false);
    if (initialised.exchange(false)) deviceManager.removeAudioCallback(this);
    deviceManager.closeAudioDevice();
    sampleRate.store(0.0); bufferSize.store(0); outputChannels.store(0); transportSamples.store(0);
    for (auto& track : tracks)
    {
        track.loaded.store(false, std::memory_order_release);
        track.lengthSeconds.store(0.0);
        track.buffer.reset(); track.numSamples = 0; track.fileName.clear();
    }
}

juce::String AudioEngine::getDeviceName() const { const juce::ScopedLock lock(stateLock); return deviceName; }
juce::String AudioEngine::getLastError() const { const juce::ScopedLock lock(stateLock); return lastError; }

std::int64_t AudioEngine::getProjectLengthSamples() const noexcept
{
    std::int64_t length = 0;
    for (const auto& track : tracks) length = juce::jmax(length, track.numSamples);
    return length;
}

void AudioEngine::setCurrentTimeSeconds(double seconds) noexcept
{
    const auto rate = sampleRate.load();
    if (rate <= 0.0) return;
    const auto requested = static_cast<std::int64_t>(std::llround(juce::jmax(0.0, seconds) * rate));
    const auto projectLength = getProjectLengthSamples();
    transportSamples.store(projectLength > 0 ? juce::jlimit<std::int64_t>(0, projectLength, requested) : juce::jmax<std::int64_t>(0, requested));
}

double AudioEngine::getCurrentTimeSeconds() const noexcept
{
    const auto rate = sampleRate.load();
    return rate > 0.0 ? static_cast<double>(transportSamples.load()) / rate : 0.0;
}

void AudioEngine::setTrackGain(int trackIndex, float gain) noexcept { if (isValidTrackIndex(trackIndex)) tracks[(size_t)trackIndex].gain.store(juce::jlimit(0.0f, 2.0f, gain)); }
float AudioEngine::getTrackGain(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex].gain.load() : 0.0f; }
void AudioEngine::setTrackPan(int trackIndex, float pan) noexcept { if (isValidTrackIndex(trackIndex)) tracks[(size_t)trackIndex].pan.store(juce::jlimit(-1.0f, 1.0f, pan)); }
float AudioEngine::getTrackPan(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex].pan.load() : 0.0f; }
void AudioEngine::setTrackMuted(int trackIndex, bool muted) noexcept { if (isValidTrackIndex(trackIndex)) tracks[(size_t)trackIndex].muted.store(muted); }
bool AudioEngine::isTrackMuted(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) && tracks[(size_t)trackIndex].muted.load(); }
void AudioEngine::setTrackSolo(int trackIndex, bool solo) noexcept { if (isValidTrackIndex(trackIndex)) tracks[(size_t)trackIndex].solo.store(solo); }
bool AudioEngine::isTrackSolo(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) && tracks[(size_t)trackIndex].solo.load(); }
bool AudioEngine::isAnyTrackSolo() const noexcept { for (const auto& track : tracks) if (track.solo.load()) return true; return false; }

bool AudioEngine::loadAudioFileIntoTrack(int trackIndex, const juce::File& file, juce::String& error)
{
    error.clear();
    if (!isValidTrackIndex(trackIndex)) { error = "Invalid audio track."; return false; }
    if (!file.existsAsFile()) { error = "The selected audio file does not exist."; return false; }
    juce::AudioFormatManager formatManager; formatManager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr) { error = "BSM DAW could not read this audio format. Use WAV, AIFF or AIF."; return false; }
    const auto outputRate = sampleRate.load();
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

    const bool wasInitialised = initialised.load();
    playing.store(false); resetTransport();
    if (wasInitialised) deviceManager.removeAudioCallback(this);
    auto& track = tracks[(size_t)trackIndex];
    track.loaded.store(false, std::memory_order_release);
    track.buffer = std::move(newBuffer);
    track.numSamples = outputSamples;
    track.fileName = file.getFileName();
    track.lengthSeconds.store(static_cast<double>(outputSamples) / outputRate);
    track.loaded.store(true, std::memory_order_release);
    { const juce::ScopedLock lock(stateLock); lastError.clear(); }
    if (wasInitialised) deviceManager.addAudioCallback(this);
    return true;
}

void AudioEngine::clearAudioTrack(int trackIndex)
{
    if (!isValidTrackIndex(trackIndex)) return;
    const bool wasInitialised = initialised.load();
    playing.store(false); resetTransport();
    if (wasInitialised) deviceManager.removeAudioCallback(this);
    auto& track = tracks[(size_t)trackIndex];
    track.loaded.store(false, std::memory_order_release);
    track.buffer.reset(); track.numSamples = 0; track.lengthSeconds.store(0.0); track.fileName.clear();
    if (wasInitialised) deviceManager.addAudioCallback(this);
}

bool AudioEngine::hasAudioFile(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) && tracks[(size_t)trackIndex].loaded.load(std::memory_order_acquire); }
juce::String AudioEngine::getAudioFileName(int trackIndex) const { return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex].fileName : juce::String{}; }
double AudioEngine::getAudioFileLengthSeconds(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex].lengthSeconds.load() : 0.0; }
const juce::AudioBuffer<float>* AudioEngine::getAudioBuffer(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex].buffer.get() : nullptr; }

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device == nullptr) return;
    sampleRate.store(device->getCurrentSampleRate()); bufferSize.store(device->getCurrentBufferSizeSamples()); outputChannels.store(device->getActiveOutputChannels().countNumberOfSetBits());
    const auto rate = device->getCurrentSampleRate(); phase = 0.0; phaseIncrement = rate > 0.0 ? (440.0 * juce::MathConstants<double>::twoPi / rate) : 0.0;
}

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputChannelData, int numOutputChannels, int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    for (int channel = 0; channel < numOutputChannels; ++channel) if (outputChannelData[channel] != nullptr) juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
    if (!playing.load()) return;

    const auto position = transportSamples.load();
    const auto projectLength = getProjectLengthSamples();
    if (projectLength > 0)
    {
        const bool anySolo = isAnyTrackSolo();
        for (int trackIndex = 0; trackIndex < maxAudioTracks; ++trackIndex)
        {
            auto& track = tracks[(size_t)trackIndex];
            if (!track.loaded.load(std::memory_order_acquire) || track.buffer == nullptr || track.muted.load() || (anySolo && !track.solo.load()) || position >= track.numSamples) continue;
            const auto samplesToMix = static_cast<int>(juce::jmin<std::int64_t>(numSamples, track.numSamples - position));
            if (samplesToMix <= 0) continue;
            const auto gain = track.gain.load(); const auto pan = track.pan.load();
            const auto leftGain = gain * (pan > 0.0f ? 1.0f - pan : 1.0f); const auto rightGain = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);
            const auto sourceChannels = track.buffer->getNumChannels(); const auto sourceOffset = static_cast<int>(position);
            if (numOutputChannels > 0 && outputChannelData[0] != nullptr && sourceChannels > 0) juce::FloatVectorOperations::addWithMultiply(outputChannelData[0], track.buffer->getReadPointer(0) + sourceOffset, leftGain, samplesToMix);
            if (numOutputChannels > 1 && outputChannelData[1] != nullptr && sourceChannels > 0) juce::FloatVectorOperations::addWithMultiply(outputChannelData[1], track.buffer->getReadPointer(sourceChannels == 1 ? 0 : 1) + sourceOffset, rightGain, samplesToMix);
        }
        const auto master = masterGain.load();
        for (int channel = 0; channel < numOutputChannels; ++channel) if (outputChannelData[channel] != nullptr) juce::FloatVectorOperations::multiply(outputChannelData[channel], master, numSamples);
        const auto advance = juce::jmin<std::int64_t>(numSamples, juce::jmax<std::int64_t>(0, projectLength - position));
        if (advance > 0) transportSamples.fetch_add(advance);
        if (position + advance >= projectLength) playing.store(false);
        return;
    }

    const auto master = masterGain.load();
    for (int channel = 0; channel < numOutputChannels; ++channel)
    {
        auto* output = outputChannelData[channel]; if (output == nullptr) continue;
        for (int sample = 0; sample < numSamples; ++sample) { output[sample] = static_cast<float>(0.05 * master * std::sin(phase)); phase += phaseIncrement; if (phase >= juce::MathConstants<double>::twoPi) phase -= juce::MathConstants<double>::twoPi; }
    }
    transportSamples.fetch_add(numSamples);
}

void AudioEngine::audioDeviceStopped() { playing.store(false); }
