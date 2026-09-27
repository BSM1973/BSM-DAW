#include "AudioEngine.h"
#include "PluginHost.h"
#include "OneKnobEffects.h"
#include <algorithm>
#include <cmath>
#include <limits>

AudioEngine::AudioEngine()
{
    tracks.reserve(initialAudioTracks);
    for (int i = 0; i < initialAudioTracks; ++i)
        tracks.push_back(std::make_unique<AudioTrackState>());
}
AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::initialise()
{
    // Playback must not open live hardware inputs by default. The stable Liberty
    // engine used 0 inputs / 2 outputs; opening eight inputs can create a
    // hardware monitoring feedback loop on interfaces such as the Studio 1824C.
    const auto error = deviceManager.initialiseWithDefaultDevices(0, 2);
    if (error.isNotEmpty()) { const juce::ScopedLock lock(stateLock); lastError = error; initialised.store(false); return false; }
    auto* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr) { const juce::ScopedLock lock(stateLock); lastError = "No audio output device is available."; initialised.store(false); return false; }
    deviceManager.addAudioCallback(this);
    { const juce::ScopedLock lock(stateLock); deviceName = device->getName(); lastError.clear(); }
    sampleRate.store(device->getCurrentSampleRate());
    bufferSize.store(device->getCurrentBufferSizeSamples());
    outputChannels.store(device->getActiveOutputChannels().countNumberOfSetBits());
    LibertyPluginHost::instance().initialise(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
    LibertyOneKnobManager::instance().prepare(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
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
    for (auto& trackPtr : tracks)
    {
        auto& track = *trackPtr;
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
    for (const auto& trackPtr : tracks)
    {
        const auto& track = *trackPtr;
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

void AudioEngine::setInstrumentTrackNotes(int instrumentTrack, const std::vector<MidiEngine::NoteEvent>& notes,
                                          double clipStartSeconds, double clipLengthSeconds, double tempoBpm) noexcept
{
    if (instrumentTrack < 0) return;
    const juce::ScopedLock lock(stateLock);
    while ((int) instrumentPlayback.size() <= instrumentTrack)
        instrumentPlayback.push_back(std::make_unique<InstrumentPlaybackState>());
    auto& state = *instrumentPlayback[(size_t)instrumentTrack];
    const auto rate = juce::jmax(1.0, tempoBpm);
    const auto count = std::min(notes.size(), maxMidiPlaybackNotes);
    state.clipStartSeconds.store(juce::jmax(0.0, clipStartSeconds));
    state.clipLengthSeconds.store(juce::jmax(0.0, clipLengthSeconds));
    state.tempoBpm.store(rate);
    for (std::size_t i=0;i<count;++i)
    {
        const auto& note=notes[i];
        state.notes[i].startSeconds.store(MidiEngine::tickToSeconds(note.startTick,rate));
        state.notes[i].endSeconds.store(MidiEngine::tickToSeconds(note.startTick+note.lengthTicks,rate));
        state.notes[i].frequency.store(440.0*std::pow(2.0,(static_cast<int>(note.pitch)-69)/12.0));
        state.notes[i].amplitude.store(0.045f*(static_cast<float>(note.velocity)/127.0f));
    }
    state.noteCount.store(count,std::memory_order_release);
}

void AudioEngine::setInstrumentTrackGain(int t,float v) noexcept { const juce::ScopedLock l(stateLock); if(t>=0&&t<(int)instrumentPlayback.size()) instrumentPlayback[(size_t)t]->gain.store(juce::jlimit(0.f,2.f,v)); }
float AudioEngine::getInstrumentTrackGain(int t) const noexcept { const juce::ScopedLock l(stateLock); return t>=0&&t<(int)instrumentPlayback.size()?instrumentPlayback[(size_t)t]->gain.load():1.f; }
void AudioEngine::setInstrumentTrackPan(int t,float v) noexcept { const juce::ScopedLock l(stateLock); if(t>=0&&t<(int)instrumentPlayback.size()) instrumentPlayback[(size_t)t]->pan.store(juce::jlimit(-1.f,1.f,v)); }
float AudioEngine::getInstrumentTrackPan(int t) const noexcept { const juce::ScopedLock l(stateLock); return t>=0&&t<(int)instrumentPlayback.size()?instrumentPlayback[(size_t)t]->pan.load():0.f; }
void AudioEngine::setInstrumentTrackMuted(int t,bool v) noexcept { const juce::ScopedLock l(stateLock); if(t>=0&&t<(int)instrumentPlayback.size()) instrumentPlayback[(size_t)t]->muted.store(v); }
bool AudioEngine::isInstrumentTrackMuted(int t) const noexcept { const juce::ScopedLock l(stateLock); return t>=0&&t<(int)instrumentPlayback.size()&&instrumentPlayback[(size_t)t]->muted.load(); }
void AudioEngine::setInstrumentTrackSolo(int t,bool v) noexcept { const juce::ScopedLock l(stateLock); if(t>=0&&t<(int)instrumentPlayback.size()) instrumentPlayback[(size_t)t]->solo.store(v); }
bool AudioEngine::isInstrumentTrackSolo(int t) const noexcept { const juce::ScopedLock l(stateLock); return t>=0&&t<(int)instrumentPlayback.size()&&instrumentPlayback[(size_t)t]->solo.load(); }

int AudioEngine::getAudioTrackCount() const noexcept
{
    const juce::ScopedLock lock(stateLock);
    return (int) tracks.size();
}

int AudioEngine::addAudioTrack()
{
    // Dynamic track creation must not mutate the vector while the realtime
    // callback is traversing it. The callback is stopped first, then the
    // vector is changed under the state lock, and finally audio is restarted.
    const bool wasInitialised = initialised.load();
    const bool wasPlaying = playing.load();
    playing.store(false);
    if (wasInitialised)
        deviceManager.removeAudioCallback(this);

    int index = -1;
    {
        const juce::ScopedLock lock(stateLock);
        tracks.push_back(std::make_unique<AudioTrackState>());
        index = (int) tracks.size() - 1;
    }

    if (wasInitialised)
        deviceManager.addAudioCallback(this);
    playing.store(wasPlaying);
    return index;
}

bool AudioEngine::removeAudioTrack(int trackIndex)
{
    const juce::ScopedLock lock(stateLock);
    if (!isValidTrackIndex(trackIndex)) return false;
    tracks.erase(tracks.begin() + trackIndex);
    return true;
}

void AudioEngine::setTrackGain(int trackIndex, float gain) noexcept { if (isValidTrackIndex(trackIndex)) tracks[(size_t)trackIndex]->gain.store(juce::jlimit(0.0f, 2.0f, gain)); }
float AudioEngine::getTrackGain(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex]->gain.load() : 0.0f; }
void AudioEngine::setTrackPan(int trackIndex, float pan) noexcept { if (isValidTrackIndex(trackIndex)) tracks[(size_t)trackIndex]->pan.store(juce::jlimit(-1.0f, 1.0f, pan)); }
float AudioEngine::getTrackPan(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex]->pan.load() : 0.0f; }
void AudioEngine::setTrackMuted(int trackIndex, bool muted) noexcept { if (isValidTrackIndex(trackIndex)) tracks[(size_t)trackIndex]->muted.store(muted); }
bool AudioEngine::isTrackMuted(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) && tracks[(size_t)trackIndex]->muted.load(); }
void AudioEngine::setTrackSolo(int trackIndex, bool solo) noexcept { if (isValidTrackIndex(trackIndex)) tracks[(size_t)trackIndex]->solo.store(solo); }
bool AudioEngine::isTrackSolo(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) && tracks[(size_t)trackIndex]->solo.load(); }
bool AudioEngine::isAnyTrackSolo() const noexcept
{
    for (const auto& track : tracks)
        if (track && track->solo.load(std::memory_order_relaxed)) return true;
    for (const auto& state : instrumentPlayback)
        if (state && state->solo.load(std::memory_order_relaxed)) return true;
    return false;
}

double AudioEngine::getTrackStartSeconds(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex]->startSeconds.load() : 0.0; }
void AudioEngine::setTrackStartSeconds(int trackIndex, double seconds) noexcept { if (isValidTrackIndex(trackIndex)) tracks[(size_t)trackIndex]->startSeconds.store(juce::jmax(0.0, seconds)); }

void AudioEngine::setTrackWarpEnabled(int trackIndex, bool enabled) noexcept
{
    if (isValidTrackIndex(trackIndex)) tracks[(size_t)trackIndex]->warpEnabled.store(enabled, std::memory_order_relaxed);
}

bool AudioEngine::isTrackWarpEnabled(int trackIndex) const noexcept
{
    return isValidTrackIndex(trackIndex) && tracks[(size_t)trackIndex]->warpEnabled.load(std::memory_order_relaxed);
}

void AudioEngine::setTrackWarpMode(int trackIndex, int mode) noexcept
{
    if (isValidTrackIndex(trackIndex)) tracks[(size_t)trackIndex]->warpMode.store(juce::jlimit(0, 4, mode), std::memory_order_relaxed);
}

int AudioEngine::getTrackWarpMode(int trackIndex) const noexcept
{
    return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex]->warpMode.load(std::memory_order_relaxed) : 0;
}

void AudioEngine::resetTrackWarpMarkers(int trackIndex) noexcept
{
    if (!isValidTrackIndex(trackIndex)) return;
    auto& track = *tracks[(size_t)trackIndex];
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
    auto& track = *tracks[(size_t)trackIndex];
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
    track.warpEnabled.store(true, std::memory_order_release);
    return true;
}

bool AudioEngine::moveTrackWarpMarker(int trackIndex, int markerIndex, double targetSeconds) noexcept
{
    if (!isValidTrackIndex(trackIndex)) return false;
    auto& track = *tracks[(size_t)trackIndex];
    const int count = track.warpMarkerCount.load(std::memory_order_acquire);
    if (markerIndex <= 0 || markerIndex >= count - 1) return false;
    const double prev = track.warpTargetSeconds[(size_t)(markerIndex - 1)].load(std::memory_order_relaxed);
    const double next = track.warpTargetSeconds[(size_t)(markerIndex + 1)].load(std::memory_order_relaxed);
    if (next - prev <= 0.002) return false;
    const double clamped = juce::jlimit(prev + 0.001, next - 0.001, targetSeconds);
    track.warpTargetSeconds[(size_t)markerIndex].store(clamped, std::memory_order_release);
    track.warpEnabled.store(true, std::memory_order_release);
    return true;
}

bool AudioEngine::removeTrackWarpMarker(int trackIndex, int markerIndex) noexcept
{
    if (!isValidTrackIndex(trackIndex)) return false;
    auto& track = *tracks[(size_t)trackIndex];
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
    return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex]->warpMarkerCount.load(std::memory_order_acquire) : 0;
}

double AudioEngine::getTrackWarpMarkerSourceSeconds(int trackIndex, int markerIndex) const noexcept
{
    if (!isValidTrackIndex(trackIndex)) return 0.0;
    const int count = tracks[(size_t)trackIndex]->warpMarkerCount.load(std::memory_order_acquire);
    return markerIndex >= 0 && markerIndex < count ? tracks[(size_t)trackIndex]->warpSourceSeconds[(size_t)markerIndex].load(std::memory_order_relaxed) : 0.0;
}

double AudioEngine::getTrackWarpMarkerTargetSeconds(int trackIndex, int markerIndex) const noexcept
{
    if (!isValidTrackIndex(trackIndex)) return 0.0;
    const int count = tracks[(size_t)trackIndex]->warpMarkerCount.load(std::memory_order_acquire);
    return markerIndex >= 0 && markerIndex < count ? tracks[(size_t)trackIndex]->warpTargetSeconds[(size_t)markerIndex].load(std::memory_order_relaxed) : 0.0;
}

bool AudioEngine::loadAudioFileIntoTrack(int trackIndex, const juce::File& file, juce::String& error)
{
    error.clear();
    if (!isValidTrackIndex(trackIndex)) { error = "Invalid audio track."; return false; }
    if (!file.existsAsFile()) { error = "The selected audio file does not exist."; return false; }
    const auto outputRate = sampleRate.load(std::memory_order_relaxed);
    if (outputRate <= 0.0) { error = "No audio device is available."; return false; }

    // Isolation stage 7: publish a newly-created track AudioBuffer without
    // decoding or copying anything from the selected file.
    const int outputSamples = juce::jmax(1, static_cast<int>(std::llround(outputRate * 10.0)));
    auto newBuffer = std::make_shared<juce::AudioBuffer<float>>(2, outputSamples);
    constexpr double twoPi = 6.28318530717958647692;
    for (int sample = 0; sample < outputSamples; ++sample)
    {
        const float value = 0.05f * static_cast<float>(std::sin(twoPi * 220.0 * static_cast<double>(sample) / outputRate));
        newBuffer->setSample(0, sample, value);
        newBuffer->setSample(1, sample, value);
    }

    const bool wasInitialised = initialised.load(std::memory_order_relaxed);
    playing.store(false, std::memory_order_relaxed);
    resetTransport();
    if (wasInitialised) deviceManager.removeAudioCallback(this);
    {
        const juce::ScopedLock lock(stateLock);
        auto& track = *tracks[(size_t)trackIndex];
        track.loaded.store(false, std::memory_order_release);
        std::atomic_store(&track.buffer, newBuffer);
        track.numSamples = outputSamples;
        track.fileName = file.getFileName();
        track.lengthSeconds.store(10.0, std::memory_order_relaxed);
        track.startSeconds.store(0.0, std::memory_order_relaxed);
        track.warpEnabled.store(false, std::memory_order_relaxed);
        track.warpMode.store(0, std::memory_order_relaxed);
        track.loaded.store(true, std::memory_order_release);
        lastError.clear();
    }
    if (wasInitialised) deviceManager.addAudioCallback(this);
    return true;
}

void AudioEngine::clearAudioTrack(int trackIndex)
{
    if (!isValidTrackIndex(trackIndex)) return;
    const bool wasInitialised = initialised.load();
    playing.store(false); resetTransport();
    if (wasInitialised) deviceManager.removeAudioCallback(this);
    auto& track = *tracks[(size_t)trackIndex];
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
    auto& source = *tracks[(size_t)trackIndex];
    const auto startSeconds = source.startSeconds.load();
    const auto lengthSeconds = source.lengthSeconds.load();
    const auto splitOffsetSeconds = splitProjectSeconds - startSeconds;
    if (splitOffsetSeconds <= 0.01 || splitOffsetSeconds >= lengthSeconds - 0.01) { error = "Place the playhead inside the audio clip to split it."; return false; }
    for (int i = 0; i < getAudioTrackCount(); ++i)
        if (i != trackIndex && !tracks[(size_t)i]->loaded.load(std::memory_order_acquire)) { newTrackIndex = i; break; }
    if (newTrackIndex < 0) newTrackIndex = addAudioTrack();
    if (newTrackIndex < 0) { error = "Could not create an audio track for the second clip segment."; return false; }
    const auto splitSample = static_cast<int>(std::llround(splitOffsetSeconds * rate));
    if (splitSample <= 0 || splitSample >= source.numSamples) { error = "The split position is outside the audio clip."; return false; }
    const auto rightSamples = source.numSamples - splitSample;
    const auto channels = source.buffer->getNumChannels();
    auto rightBuffer = std::make_shared<juce::AudioBuffer<float>>(channels, rightSamples);
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
    auto& right = *tracks[(size_t)newTrackIndex];
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

bool AudioEngine::hasAudioFile(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) && tracks[(size_t)trackIndex]->loaded.load(std::memory_order_acquire); }
juce::String AudioEngine::getAudioFileName(int trackIndex) const { return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex]->fileName : juce::String{}; }
double AudioEngine::getAudioFileLengthSeconds(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex]->lengthSeconds.load() : 0.0; }
const juce::AudioBuffer<float>* AudioEngine::getAudioBuffer(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex]->buffer.get() : nullptr; }

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device == nullptr) return;
    sampleRate.store(device->getCurrentSampleRate());
    bufferSize.store(device->getCurrentBufferSizeSamples());
    outputChannels.store(device->getActiveOutputChannels().countNumberOfSetBits());
    LibertyPluginHost::instance().initialise(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
    LibertyOneKnobManager::instance().prepare(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputChannelData, int numOutputChannels, int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    // Isolation stage 6: read PCM from track 0, but do not iterate the dynamic
    // track vector and do not consult any track metadata. This distinguishes
    // AudioBuffer ownership from concurrent access to the track container/state.
    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);

    if (!playing.load(std::memory_order_relaxed))
        return;

    std::shared_ptr<juce::AudioBuffer<float>> audioBuffer;
    {
        const juce::ScopedLock lock(stateLock);
        if (!tracks.empty())
            audioBuffer = tracks.front()->buffer;
    }
    if (audioBuffer == nullptr || audioBuffer->getNumChannels() <= 0 || audioBuffer->getNumSamples() <= 0)
        return;

    const auto position = transportSamples.load(std::memory_order_relaxed);
    const int sourceChannels = audioBuffer->getNumChannels();
    const auto sourceSamples = static_cast<std::int64_t>(audioBuffer->getNumSamples());
    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto source = (position + sample) % sourceSamples;
        if (numOutputChannels > 0 && outputChannelData[0] != nullptr)
            outputChannelData[0][sample] = audioBuffer->getReadPointer(0)[source];
        if (numOutputChannels > 1 && outputChannelData[1] != nullptr)
            outputChannelData[1][sample] = audioBuffer->getReadPointer(sourceChannels > 1 ? 1 : 0)[source];
    }
    transportSamples.fetch_add(numSamples, std::memory_order_relaxed);
}

void AudioEngine::audioDeviceStopped() { playing.store(false); }
