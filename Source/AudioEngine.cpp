#include "AudioEngine.h"
#include <algorithm>
#include <cmath>
#include <limits>

AudioEngine::AudioEngine() = default;
AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::initialise()
{
    const auto error = deviceManager.initialiseWithDefaultDevices(8, 2);
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
    sampleRate.store(0.0); bufferSize.store(0); outputChannels.store(0); transportSamples.store(0); projectExtraLengthSeconds.store(0.0);
    playbackClockBaseSeconds.store(0.0, std::memory_order_relaxed);
    playbackClockStartMilliseconds.store(0.0, std::memory_order_relaxed);
    midiPlaybackNoteCount.store(0, std::memory_order_release);
    midiClipStartSeconds.store(0.0, std::memory_order_relaxed);
    midiClipLengthSeconds.store(0.0, std::memory_order_relaxed);
    midiTrackMuted.store(false, std::memory_order_relaxed);
    midiTrackSolo.store(false, std::memory_order_relaxed);
    instrumentTrackMuted.store(false, std::memory_order_relaxed);
    instrumentTrackSolo.store(false, std::memory_order_relaxed);
    for (auto& track : tracks)
    {
        track.loaded.store(false, std::memory_order_release);
        track.lengthSeconds.store(0.0); track.startSeconds.store(0.0);
        track.warpEnabled.store(false, std::memory_order_relaxed);
        track.warpMode.store(0, std::memory_order_relaxed);
        track.warpMarkerCount.store(0, std::memory_order_relaxed);
        track.buffer.reset(); track.numSamples = 0; track.fileName.clear();
    }
}

juce::String AudioEngine::getDeviceName() const { const juce::ScopedLock lock(stateLock); return deviceName; }
juce::String AudioEngine::getLastError() const { const juce::ScopedLock lock(stateLock); return lastError; }

std::int64_t AudioEngine::getProjectLengthSamples() const noexcept
{
    const auto rate = sampleRate.load();
    if (rate <= 0.0) return 0;
    std::int64_t length = 0;
    for (const auto& track : tracks)
    {
        if (!track.loaded.load(std::memory_order_acquire)) continue;
        const auto start = static_cast<std::int64_t>(std::llround(track.startSeconds.load() * rate));
        length = juce::jmax(length, start + track.numSamples);
    }
    const auto extraLength = static_cast<std::int64_t>(std::llround(projectExtraLengthSeconds.load(std::memory_order_relaxed) * rate));
    length = juce::jmax(length, extraLength);
    return length;
}

void AudioEngine::setCurrentTimeSeconds(double seconds) noexcept
{
    const auto rate = sampleRate.load();
    if (rate <= 0.0) return;
    const auto requested = static_cast<std::int64_t>(std::llround(juce::jmax(0.0, seconds) * rate));
    const auto projectLength = getProjectLengthSamples();
    const auto clamped = projectLength > 0
        ? juce::jlimit<std::int64_t>(0, projectLength, requested)
        : juce::jmax<std::int64_t>(0, requested);
    transportSamples.store(clamped, std::memory_order_relaxed);
    if (playing.load(std::memory_order_relaxed))
    {
        playbackClockBaseSeconds.store(static_cast<double>(clamped) / rate, std::memory_order_relaxed);
        playbackClockStartMilliseconds.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
    }
}

double AudioEngine::getCurrentTimeSeconds() const noexcept
{
    const auto rate = sampleRate.load();
    if (rate <= 0.0) return 0.0;
    const auto transportSeconds = static_cast<double>(transportSamples.load(std::memory_order_relaxed)) / rate;
    if (!playing.load(std::memory_order_relaxed)) return transportSeconds;
    const auto elapsedSeconds = juce::jmax(0.0,
        (juce::Time::getMillisecondCounterHiRes() - playbackClockStartMilliseconds.load(std::memory_order_relaxed)) / 1000.0);
    const auto clockSeconds = playbackClockBaseSeconds.load(std::memory_order_relaxed) + elapsedSeconds;
    const auto projectLength = getProjectLengthSamples();
    if (projectLength > 0)
        return juce::jmin(static_cast<double>(projectLength) / rate, juce::jmax(transportSeconds, clockSeconds));
    return juce::jmax(transportSeconds, clockSeconds);
}

void AudioEngine::setMidiNotes(const std::vector<MidiEngine::NoteEvent>& notes,
                               double clipStartSeconds,
                               double clipLengthSeconds,
                               double tempoBpm) noexcept
{
    const auto rate = juce::jmax(1.0, tempoBpm);
    const auto start = juce::jmax(0.0, clipStartSeconds);
    const auto length = juce::jmax(0.0, clipLengthSeconds);
    const auto count = std::min(notes.size(), maxMidiPlaybackNotes);
    midiClipStartSeconds.store(start, std::memory_order_relaxed);
    midiClipLengthSeconds.store(length, std::memory_order_relaxed);
    midiTempoBpm.store(rate, std::memory_order_relaxed);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& note = notes[i];
        const auto noteStart = MidiEngine::tickToSeconds(note.startTick, rate);
        const auto noteEnd = MidiEngine::tickToSeconds(note.startTick + note.lengthTicks, rate);
        const auto frequency = 440.0 * std::pow(2.0, (static_cast<int>(note.pitch) - 69) / 12.0);
        const auto amplitude = 0.045f * (static_cast<float>(note.velocity) / 127.0f);
        midiPlaybackNotes[i].startSeconds.store(noteStart, std::memory_order_relaxed);
        midiPlaybackNotes[i].endSeconds.store(noteEnd, std::memory_order_relaxed);
        midiPlaybackNotes[i].frequency.store(frequency, std::memory_order_relaxed);
        midiPlaybackNotes[i].amplitude.store(amplitude, std::memory_order_relaxed);
    }
    midiPlaybackNoteCount.store(count, std::memory_order_release);
}

void AudioEngine::setTrackGain(int trackIndex, float gain) noexcept { if (isValidTrackIndex(trackIndex)) tracks[(size_t)trackIndex].gain.store(juce::jlimit(0.0f, 2.0f, gain)); }
float AudioEngine::getTrackGain(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex].gain.load() : 0.0f; }
void AudioEngine::setTrackPan(int trackIndex, float pan) noexcept { if (isValidTrackIndex(trackIndex)) tracks[(size_t)trackIndex].pan.store(juce::jlimit(-1.0f, 1.0f, pan)); }
float AudioEngine::getTrackPan(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex].pan.load() : 0.0f; }
void AudioEngine::setTrackMuted(int trackIndex, bool muted) noexcept { if (isValidTrackIndex(trackIndex)) tracks[(size_t)trackIndex].muted.store(muted); }
bool AudioEngine::isTrackMuted(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) && tracks[(size_t)trackIndex].muted.load(); }
void AudioEngine::setTrackSolo(int trackIndex, bool solo) noexcept { if (isValidTrackIndex(trackIndex)) tracks[(size_t)trackIndex].solo.store(solo); }
bool AudioEngine::isTrackSolo(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) && tracks[(size_t)trackIndex].solo.load(); }
bool AudioEngine::isAnyTrackSolo() const noexcept
{
    for (const auto& track : tracks) if (track.solo.load()) return true;
    return midiTrackSolo.load(std::memory_order_relaxed) || instrumentTrackSolo.load(std::memory_order_relaxed);
}

double AudioEngine::getTrackStartSeconds(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex].startSeconds.load() : 0.0; }
void AudioEngine::setTrackStartSeconds(int trackIndex, double seconds) noexcept { if (isValidTrackIndex(trackIndex)) tracks[(size_t)trackIndex].startSeconds.store(juce::jmax(0.0, seconds)); }

void AudioEngine::setTrackWarpEnabled(int trackIndex, bool enabled) noexcept
{
    if (isValidTrackIndex(trackIndex)) tracks[(size_t)trackIndex].warpEnabled.store(enabled, std::memory_order_relaxed);
}

bool AudioEngine::isTrackWarpEnabled(int trackIndex) const noexcept
{
    return isValidTrackIndex(trackIndex) && tracks[(size_t)trackIndex].warpEnabled.load(std::memory_order_relaxed);
}

void AudioEngine::setTrackWarpMode(int trackIndex, int mode) noexcept
{
    if (isValidTrackIndex(trackIndex)) tracks[(size_t)trackIndex].warpMode.store(juce::jlimit(0, 4, mode), std::memory_order_relaxed);
}

int AudioEngine::getTrackWarpMode(int trackIndex) const noexcept
{
    return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex].warpMode.load(std::memory_order_relaxed) : 0;
}

void AudioEngine::resetTrackWarpMarkers(int trackIndex) noexcept
{
    if (!isValidTrackIndex(trackIndex)) return;
    auto& track = tracks[(size_t)trackIndex];
    const double length = juce::jmax(0.0, track.lengthSeconds.load(std::memory_order_relaxed));
    if (length <= 0.0)
    {
        track.warpMarkerCount.store(0, std::memory_order_release);
        return;
    }
    track.warpSourceSeconds[0].store(0.0, std::memory_order_relaxed);
    track.warpTargetSeconds[0].store(0.0, std::memory_order_relaxed);
    track.warpSourceSeconds[1].store(length, std::memory_order_relaxed);
    track.warpTargetSeconds[1].store(length, std::memory_order_relaxed);
    track.warpMarkerCount.store(2, std::memory_order_release);
}

bool AudioEngine::addTrackWarpMarker(int trackIndex, double sourceSeconds, double targetSeconds) noexcept
{
    if (!isValidTrackIndex(trackIndex)) return false;
    auto& track = tracks[(size_t)trackIndex];
    int count = track.warpMarkerCount.load(std::memory_order_acquire);
    if (count < 2) { resetTrackWarpMarkers(trackIndex); count = track.warpMarkerCount.load(std::memory_order_acquire); }
    if (count < 2 || count >= maxWarpMarkers) return false;

    const double length = track.lengthSeconds.load(std::memory_order_relaxed);
    sourceSeconds = juce::jlimit(0.001, juce::jmax(0.001, length - 0.001), sourceSeconds);
    int insertAt = 1;
    while (insertAt < count && track.warpSourceSeconds[(size_t)insertAt].load(std::memory_order_relaxed) < sourceSeconds) ++insertAt;
    if (insertAt <= 0 || insertAt >= count) return false;

    const double prevSource = track.warpSourceSeconds[(size_t)(insertAt - 1)].load(std::memory_order_relaxed);
    const double nextSource = track.warpSourceSeconds[(size_t)insertAt].load(std::memory_order_relaxed);
    if (sourceSeconds - prevSource < 0.001 || nextSource - sourceSeconds < 0.001) return false;

    const double prevTarget = track.warpTargetSeconds[(size_t)(insertAt - 1)].load(std::memory_order_relaxed);
    const double nextTarget = track.warpTargetSeconds[(size_t)insertAt].load(std::memory_order_relaxed);
    targetSeconds = juce::jlimit(prevTarget + 0.001, nextTarget - 0.001, targetSeconds);

    for (int i = count; i > insertAt; --i)
    {
        track.warpSourceSeconds[(size_t)i].store(track.warpSourceSeconds[(size_t)(i - 1)].load(std::memory_order_relaxed), std::memory_order_relaxed);
        track.warpTargetSeconds[(size_t)i].store(track.warpTargetSeconds[(size_t)(i - 1)].load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    track.warpSourceSeconds[(size_t)insertAt].store(sourceSeconds, std::memory_order_relaxed);
    track.warpTargetSeconds[(size_t)insertAt].store(targetSeconds, std::memory_order_relaxed);
    track.warpMarkerCount.store(count + 1, std::memory_order_release);
    return true;
}

bool AudioEngine::moveTrackWarpMarker(int trackIndex, int markerIndex, double targetSeconds) noexcept
{
    if (!isValidTrackIndex(trackIndex)) return false;
    auto& track = tracks[(size_t)trackIndex];
    const int count = track.warpMarkerCount.load(std::memory_order_acquire);
    if (markerIndex <= 0 || markerIndex >= count - 1) return false;
    const double prev = track.warpTargetSeconds[(size_t)(markerIndex - 1)].load(std::memory_order_relaxed);
    const double next = track.warpTargetSeconds[(size_t)(markerIndex + 1)].load(std::memory_order_relaxed);
    if (next - prev <= 0.002) return false;
    track.warpTargetSeconds[(size_t)markerIndex].store(juce::jlimit(prev + 0.001, next - 0.001, targetSeconds), std::memory_order_relaxed);
    return true;
}

bool AudioEngine::removeTrackWarpMarker(int trackIndex, int markerIndex) noexcept
{
    if (!isValidTrackIndex(trackIndex)) return false;
    auto& track = tracks[(size_t)trackIndex];
    const int count = track.warpMarkerCount.load(std::memory_order_acquire);
    if (markerIndex <= 0 || markerIndex >= count - 1) return false;
    for (int i = markerIndex; i < count - 1; ++i)
    {
        track.warpSourceSeconds[(size_t)i].store(track.warpSourceSeconds[(size_t)(i + 1)].load(std::memory_order_relaxed), std::memory_order_relaxed);
        track.warpTargetSeconds[(size_t)i].store(track.warpTargetSeconds[(size_t)(i + 1)].load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    track.warpMarkerCount.store(count - 1, std::memory_order_release);
    return true;
}

int AudioEngine::getTrackWarpMarkerCount(int trackIndex) const noexcept
{
    return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex].warpMarkerCount.load(std::memory_order_acquire) : 0;
}

double AudioEngine::getTrackWarpMarkerSourceSeconds(int trackIndex, int markerIndex) const noexcept
{
    if (!isValidTrackIndex(trackIndex)) return 0.0;
    const int count = tracks[(size_t)trackIndex].warpMarkerCount.load(std::memory_order_acquire);
    return markerIndex >= 0 && markerIndex < count ? tracks[(size_t)trackIndex].warpSourceSeconds[(size_t)markerIndex].load(std::memory_order_relaxed) : 0.0;
}

double AudioEngine::getTrackWarpMarkerTargetSeconds(int trackIndex, int markerIndex) const noexcept
{
    if (!isValidTrackIndex(trackIndex)) return 0.0;
    const int count = tracks[(size_t)trackIndex].warpMarkerCount.load(std::memory_order_acquire);
    return markerIndex >= 0 && markerIndex < count ? tracks[(size_t)trackIndex].warpTargetSeconds[(size_t)markerIndex].load(std::memory_order_relaxed) : 0.0;
}

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
        for (int channel = 0; channel < inputChannels; ++channel) { juce::LagrangeInterpolator interpolator; interpolator.process(ratio, decodedBuffer->getReadPointer(channel), newBuffer->getWritePointer(channel), outputSamples); }
    else newBuffer->makeCopyOf(*decodedBuffer);

    const bool wasInitialised = initialised.load();
    playing.store(false); resetTransport();
    if (wasInitialised) deviceManager.removeAudioCallback(this);
    auto& track = tracks[(size_t)trackIndex];
    track.loaded.store(false, std::memory_order_release);
    track.buffer = std::move(newBuffer); track.numSamples = outputSamples;
    track.fileName = file.getFileName(); track.lengthSeconds.store(static_cast<double>(outputSamples) / outputRate); track.startSeconds.store(0.0);
    track.warpEnabled.store(false, std::memory_order_relaxed);
    track.warpMode.store(0, std::memory_order_relaxed);
    track.loaded.store(true, std::memory_order_release);
    resetTrackWarpMarkers(trackIndex);
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
    track.buffer.reset(); track.numSamples = 0; track.lengthSeconds.store(0.0); track.startSeconds.store(0.0); track.fileName.clear();
    track.warpEnabled.store(false, std::memory_order_relaxed);
    track.warpMarkerCount.store(0, std::memory_order_release);
    if (wasInitialised) deviceManager.addAudioCallback(this);
}

bool AudioEngine::splitAudioTrack(int trackIndex, double splitProjectSeconds, int& newTrackIndex, juce::String& error)
{
    error.clear();
    newTrackIndex = -1;
    if (!isValidTrackIndex(trackIndex) || !hasAudioFile(trackIndex)) { error = "Select a loaded audio clip first."; return false; }
    const auto rate = sampleRate.load();
    if (rate <= 0.0) { error = "No audio device is available."; return false; }
    auto& source = tracks[(size_t)trackIndex];
    const auto startSeconds = source.startSeconds.load();
    const auto lengthSeconds = source.lengthSeconds.load();
    const auto splitOffsetSeconds = splitProjectSeconds - startSeconds;
    if (splitOffsetSeconds <= 0.01 || splitOffsetSeconds >= lengthSeconds - 0.01) { error = "Place the playhead inside the audio clip to split it."; return false; }
    for (int i = 0; i < maxAudioTracks; ++i)
        if (i != trackIndex && !tracks[(size_t)i].loaded.load(std::memory_order_acquire)) { newTrackIndex = i; break; }
    if (newTrackIndex < 0) { error = "No empty audio track is available for the second clip segment."; return false; }
    const auto splitSample = static_cast<int>(std::llround(splitOffsetSeconds * rate));
    if (splitSample <= 0 || splitSample >= source.numSamples) { error = "The split position is outside the audio clip."; return false; }
    const auto rightSamples = source.numSamples - splitSample;
    const auto channels = source.buffer->getNumChannels();
    auto rightBuffer = std::make_unique<juce::AudioBuffer<float>>(channels, rightSamples);
    rightBuffer->clear();
    for (int channel = 0; channel < channels; ++channel)
        rightBuffer->copyFrom(channel, 0, *source.buffer, channel, splitSample, rightSamples);
    const bool wasInitialised = initialised.load();
    const auto savedPlaying = playing.load();
    if (wasInitialised) deviceManager.removeAudioCallback(this);
    playing.store(false);
    source.buffer->setSize(channels, splitSample, true, false, false);
    source.numSamples = splitSample;
    source.lengthSeconds.store(static_cast<double>(splitSample) / rate);
    auto& right = tracks[(size_t)newTrackIndex];
    right.loaded.store(false, std::memory_order_release);
    right.buffer = std::move(rightBuffer);
    right.numSamples = rightSamples;
    right.fileName = source.fileName + " - Split";
    right.lengthSeconds.store(static_cast<double>(rightSamples) / rate);
    right.startSeconds.store(startSeconds + splitOffsetSeconds);
    right.gain.store(source.gain.load());
    right.pan.store(source.pan.load());
    right.muted.store(source.muted.load());
    right.solo.store(source.solo.load());
    right.warpEnabled.store(false, std::memory_order_relaxed);
    right.warpMode.store(source.warpMode.load(std::memory_order_relaxed), std::memory_order_relaxed);
    right.loaded.store(true, std::memory_order_release);
    resetTrackWarpMarkers(trackIndex);
    resetTrackWarpMarkers(newTrackIndex);
    if (wasInitialised) deviceManager.addAudioCallback(this);
    if (savedPlaying) playing.store(true);
    return true;
}

bool AudioEngine::hasAudioFile(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) && tracks[(size_t)trackIndex].loaded.load(std::memory_order_acquire); }
juce::String AudioEngine::getAudioFileName(int trackIndex) const { return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex].fileName : juce::String{}; }
double AudioEngine::getAudioFileLengthSeconds(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex].lengthSeconds.load() : 0.0; }
const juce::AudioBuffer<float>* AudioEngine::getAudioBuffer(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex].buffer.get() : nullptr; }

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device == nullptr) return;
    sampleRate.store(device->getCurrentSampleRate()); bufferSize.store(device->getCurrentBufferSizeSamples()); outputChannels.store(device->getActiveOutputChannels().countNumberOfSetBits());
}

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputChannelData, int numOutputChannels, int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    for (int channel = 0; channel < numOutputChannels; ++channel) if (outputChannelData[channel] != nullptr) juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
    if (!playing.load()) return;

    const auto position = transportSamples.load();
    const auto projectLength = getProjectLengthSamples();
    const bool hasBoundedAudioProject = projectLength > 0;
    const bool anySolo = isAnyTrackSolo();
    const auto rate = sampleRate.load();

    for (int trackIndex = 0; trackIndex < maxAudioTracks; ++trackIndex)
    {
        auto& track = tracks[(size_t)trackIndex];
        if (!track.loaded.load(std::memory_order_acquire) || track.buffer == nullptr || track.muted.load() || (anySolo && !track.solo.load())) continue;
        const auto startSample = static_cast<std::int64_t>(std::llround(track.startSeconds.load() * rate));
        const auto clipEnd = startSample + track.numSamples;
        const auto blockEnd = position + numSamples;
        if (blockEnd <= startSample || position >= clipEnd) continue;
        const auto mixStart = juce::jmax(position, startSample);
        const auto mixEnd = juce::jmin(blockEnd, clipEnd);
        const auto samplesToMix = static_cast<int>(juce::jmax<std::int64_t>(0, mixEnd - mixStart));
        if (samplesToMix <= 0) continue;
        const auto outputOffset = static_cast<int>(mixStart - position);
        const auto sourceOffset = static_cast<int>(mixStart - startSample);
        const auto gain = track.gain.load(); const auto pan = track.pan.load();
        const auto leftGain = gain * (pan > 0.0f ? 1.0f - pan : 1.0f); const auto rightGain = gain * (pan < 0.0f ? 1.0f + pan : 1.0f);
        const auto sourceChannels = track.buffer->getNumChannels();

        if (!track.warpEnabled.load(std::memory_order_relaxed) || track.warpMarkerCount.load(std::memory_order_acquire) < 2)
        {
            if (numOutputChannels > 0 && outputChannelData[0] != nullptr && sourceChannels > 0)
                juce::FloatVectorOperations::addWithMultiply(outputChannelData[0] + outputOffset, track.buffer->getReadPointer(0) + sourceOffset, leftGain, samplesToMix);
            if (numOutputChannels > 1 && outputChannelData[1] != nullptr && sourceChannels > 0)
                juce::FloatVectorOperations::addWithMultiply(outputChannelData[1] + outputOffset, track.buffer->getReadPointer(sourceChannels == 1 ? 0 : 1) + sourceOffset, rightGain, samplesToMix);
            continue;
        }

        const int markerCount = juce::jlimit(2, maxWarpMarkers, track.warpMarkerCount.load(std::memory_order_acquire));
        const int mode = track.warpMode.load(std::memory_order_relaxed);
        const int lastSample = juce::jmax(0, track.buffer->getNumSamples() - 1);

        auto readWarpedSample = [&](int channel, double samplePosition) -> float
        {
            const float* data = track.buffer->getReadPointer(juce::jlimit(0, sourceChannels - 1, channel));
            samplePosition = juce::jlimit(0.0, (double)lastSample, samplePosition);
            if (mode == 0)
                return data[juce::jlimit(0, lastSample, (int)std::llround(samplePosition))];

            const int i1 = juce::jlimit(0, lastSample, (int)std::floor(samplePosition));
            const int i2 = juce::jmin(lastSample, i1 + 1);
            const float frac = (float)(samplePosition - (double)i1);
            const float linear = data[i1] + (data[i2] - data[i1]) * frac;

            if (mode == 2)
            {
                const int ip = juce::jmax(0, i1 - 1);
                const int in = juce::jmin(lastSample, i2 + 1);
                return 0.25f * data[ip] + 0.5f * linear + 0.25f * data[in];
            }
            if (mode == 4)
            {
                const int i0 = juce::jmax(0, i1 - 1);
                const int i3 = juce::jmin(lastSample, i2 + 1);
                const float p0 = data[i0], p1 = data[i1], p2 = data[i2], p3 = data[i3];
                const float f2 = frac * frac, f3 = f2 * frac;
                return 0.5f * ((2.0f * p1) + (-p0 + p2) * frac
                    + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * f2
                    + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * f3);
            }
            return linear;
        };

        for (int s = 0; s < samplesToMix; ++s)
        {
            const double targetSeconds = (double)(mixStart - startSample + s) / rate;
            double sourceSeconds = targetSeconds;

            for (int marker = 0; marker < markerCount - 1; ++marker)
            {
                const double ta = track.warpTargetSeconds[(size_t)marker].load(std::memory_order_relaxed);
                const double tb = track.warpTargetSeconds[(size_t)(marker + 1)].load(std::memory_order_relaxed);
                if (targetSeconds < ta || targetSeconds > tb) continue;
                const double sa = track.warpSourceSeconds[(size_t)marker].load(std::memory_order_relaxed);
                const double sb = track.warpSourceSeconds[(size_t)(marker + 1)].load(std::memory_order_relaxed);
                const double span = juce::jmax(0.000001, tb - ta);
                const double alpha = juce::jlimit(0.0, 1.0, (targetSeconds - ta) / span);
                sourceSeconds = sa + (sb - sa) * alpha;
                break;
            }

            const double sourceSamplePosition = sourceSeconds * rate;
            if (numOutputChannels > 0 && outputChannelData[0] != nullptr && sourceChannels > 0)
                outputChannelData[0][outputOffset + s] += readWarpedSample(0, sourceSamplePosition) * leftGain;
            if (numOutputChannels > 1 && outputChannelData[1] != nullptr && sourceChannels > 0)
                outputChannelData[1][outputOffset + s] += readWarpedSample(sourceChannels == 1 ? 0 : 1, sourceSamplePosition) * rightGain;
        }
    }

    const auto midiCount = midiPlaybackNoteCount.load(std::memory_order_acquire);
    const auto midiStart = midiClipStartSeconds.load(std::memory_order_relaxed);
    const auto midiLength = midiClipLengthSeconds.load(std::memory_order_relaxed);
    const bool synthMuted = midiTrackMuted.load(std::memory_order_relaxed) || instrumentTrackMuted.load(std::memory_order_relaxed);
    const bool synthSolo = midiTrackSolo.load(std::memory_order_relaxed) || instrumentTrackSolo.load(std::memory_order_relaxed);
    if (!synthMuted && (!anySolo || synthSolo) && midiCount > 0 && midiLength > 0.0 && rate > 0.0)
    {
        constexpr double twoPi = 6.28318530717958647692;
        constexpr double attackSeconds = 0.005;
        constexpr double releaseSeconds = 0.010;
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const double projectTime = static_cast<double>(position + sample) / rate;
            const double localTime = projectTime - midiStart;
            if (localTime < 0.0 || localTime >= midiLength) continue;
            float sampleValue = 0.0f;
            for (std::size_t noteIndex = 0; noteIndex < midiCount; ++noteIndex)
            {
                const auto noteStart = midiPlaybackNotes[noteIndex].startSeconds.load(std::memory_order_relaxed);
                const auto noteEnd = midiPlaybackNotes[noteIndex].endSeconds.load(std::memory_order_relaxed);
                const auto noteTime = localTime - noteStart;
                const auto noteDuration = noteEnd - noteStart;
                if (noteTime < 0.0 || noteTime >= noteDuration) continue;
                float envelope = 1.0f;
                if (noteTime < attackSeconds) envelope = static_cast<float>(noteTime / attackSeconds);
                const auto remaining = noteDuration - noteTime;
                if (remaining < releaseSeconds) envelope = juce::jmin(envelope, static_cast<float>(remaining / releaseSeconds));
                const auto frequency = midiPlaybackNotes[noteIndex].frequency.load(std::memory_order_relaxed);
                const auto amplitude = midiPlaybackNotes[noteIndex].amplitude.load(std::memory_order_relaxed);
                sampleValue += static_cast<float>(std::sin(twoPi * frequency * noteTime) * static_cast<double>(amplitude * envelope));
            }
            if (numOutputChannels > 0 && outputChannelData[0] != nullptr) outputChannelData[0][sample] += sampleValue;
            if (numOutputChannels > 1 && outputChannelData[1] != nullptr) outputChannelData[1][sample] += sampleValue;
        }
    }

    const auto master = masterGain.load();
    for (int channel = 0; channel < numOutputChannels; ++channel) if (outputChannelData[channel] != nullptr) juce::FloatVectorOperations::multiply(outputChannelData[channel], master, numSamples);
    if (!hasBoundedAudioProject)
    {
        transportSamples.fetch_add(numSamples);
        return;
    }
    const auto advance = juce::jmin<std::int64_t>(numSamples, juce::jmax<std::int64_t>(0, projectLength - position));
    if (advance > 0) transportSamples.fetch_add(advance);
    if (position + advance >= projectLength) playing.store(false);
}

void AudioEngine::audioDeviceStopped() { playing.store(false); }
