#include "AudioEngine.h"

// PERFORM contributes audio to this engine-owned hardware callback. It never
// registers its own AudioIODeviceCallback.
void renderLibertyPerformAudio(AudioEngine*, float* const*, int, int);
void processLibertyRecordingInput(AudioEngine*, const float* const*, int, int);
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
    instrumentResumePending.store(false, std::memory_order_release);
    instrumentPanicPending.store(false, std::memory_order_release);
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
        track.lengthSeconds.store(0.0); track.bufferSampleRate.store(0.0); track.startSeconds.store(0.0);
        track.warpEnabled.store(false, std::memory_order_relaxed);
        track.warpMode.store(0, std::memory_order_relaxed);
        track.warpMarkerCount.store(0, std::memory_order_relaxed);
        std::atomic_store(&track.warpMarkerSnapshot, std::shared_ptr<WarpMarkerSnapshot>{});
        std::atomic_store(&track.buffer, std::shared_ptr<juce::AudioBuffer<float>>{}); track.fileName.clear();
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
        const auto audioBuffer = std::atomic_load(&track.buffer);
        if (audioBuffer == nullptr) continue;
        const auto start = static_cast<std::int64_t>(std::llround(track.startSeconds.load() * rate));
        const auto duration = static_cast<std::int64_t>(std::llround(track.lengthSeconds.load() * rate));
        length = juce::jmax(length, start + duration);
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
    instrumentPanicPending.store(true, std::memory_order_release);
    if (playing.load(std::memory_order_relaxed))
    {
        instrumentResumePending.store(true, std::memory_order_release);
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
    const auto rate = juce::jmax(1.0, tempoBpm);
    std::shared_ptr<InstrumentNoteSnapshot> snapshot;
    try
    {
        ensureInstrumentPlaybackTracks(instrumentTrack + 1);
        snapshot = std::make_shared<InstrumentNoteSnapshot>();
        snapshot->notes.reserve(notes.size());
        for (const auto& note : notes)
        {
            InstrumentPlaybackNote playbackNote;
            playbackNote.startSeconds = MidiEngine::tickToSeconds(note.startTick, rate);
            playbackNote.endSeconds = MidiEngine::tickToSeconds(note.startTick + note.lengthTicks, rate);
            playbackNote.frequency = 440.0 * std::pow(2.0, (static_cast<int>(note.pitch) - 69) / 12.0);
            playbackNote.amplitude = 0.045f * (static_cast<float>(note.velocity) / 127.0f);
            playbackNote.channel = juce::jlimit(1, 16, (int) note.channel);
            snapshot->notes.push_back(playbackNote);
        }
    }
    catch (...)
    {
        return;
    }

    const juce::ScopedLock lock(stateLock);
    if (instrumentTrack >= (int) instrumentPlayback.size()) return;
    auto& state = *instrumentPlayback[(size_t)instrumentTrack];
    state.clipStartSeconds.store(juce::jmax(0.0, clipStartSeconds));
    state.clipLengthSeconds.store(juce::jmax(0.0, clipLengthSeconds));
    state.tempoBpm.store(rate);
    std::atomic_store(&state.noteSnapshot, std::move(snapshot));
    if (playing.load(std::memory_order_relaxed))
    {
        instrumentPanicPending.store(true, std::memory_order_release);
        instrumentResumePending.store(true, std::memory_order_release);
    }
}

void AudioEngine::setInstrumentArrangementClips(int instrumentTrack,
                                                const std::vector<InstrumentArrangementClip>& clips,
                                                double tempoBpm) noexcept
{
    if (instrumentTrack < 0) return;
    const double rate = juce::jmax(1.0, tempoBpm);
    std::shared_ptr<InstrumentNoteSnapshot> snapshot;
    try
    {
        ensureInstrumentPlaybackTracks(instrumentTrack + 1);
        snapshot = std::make_shared<InstrumentNoteSnapshot>();
        for (const auto& clip : clips)
        {
            if (!std::isfinite(clip.startSeconds) || !std::isfinite(clip.lengthSeconds)
                || clip.startSeconds < 0.0 || clip.lengthSeconds <= 0.0) continue;
            const double clipEnd = clip.startSeconds + clip.lengthSeconds;
            if (!std::isfinite(clipEnd)) continue;
            for (const auto& note : clip.notes)
            {
                if (note.startTick < 0 || note.lengthTicks <= 0
                    || note.startTick > std::numeric_limits<std::int64_t>::max() - note.lengthTicks)
                    continue;
                InstrumentPlaybackNote playbackNote;
                playbackNote.startSeconds = clip.startSeconds + MidiEngine::tickToSeconds(note.startTick, rate);
                if (!std::isfinite(playbackNote.startSeconds) || playbackNote.startSeconds >= clipEnd) continue;
                playbackNote.endSeconds = juce::jmin(clipEnd,
                    clip.startSeconds + MidiEngine::tickToSeconds(note.startTick + note.lengthTicks, rate));
                if (!std::isfinite(playbackNote.endSeconds)
                    || playbackNote.endSeconds <= playbackNote.startSeconds) continue;
                playbackNote.frequency = 440.0 * std::pow(2.0, (static_cast<int>(note.pitch) - 69) / 12.0);
                playbackNote.amplitude = 0.045f * (static_cast<float>(note.velocity) / 127.0f);
                playbackNote.channel = juce::jlimit(1, 16, (int) note.channel);
                snapshot->notes.push_back(playbackNote);
            }
        }
    }
    catch (...) { return; }
    const juce::ScopedLock lock(stateLock);
    if (instrumentTrack >= (int) instrumentPlayback.size()) return;
    auto& state = *instrumentPlayback[(size_t) instrumentTrack];
    std::atomic_store(&state.arrangementSnapshot, std::move(snapshot));
    if (playing.load(std::memory_order_relaxed))
    {
        instrumentPanicPending.store(true, std::memory_order_release);
        instrumentResumePending.store(true, std::memory_order_release);
    }
}

void AudioEngine::setInstrumentTrackGain(int t,float v) noexcept { const juce::ScopedLock l(stateLock); if(t>=0&&t<(int)instrumentPlayback.size()) instrumentPlayback[(size_t)t]->gain.store(juce::jlimit(0.f,2.f,v)); }
float AudioEngine::getInstrumentTrackGain(int t) const noexcept { const juce::ScopedLock l(stateLock); return t>=0&&t<(int)instrumentPlayback.size()?instrumentPlayback[(size_t)t]->gain.load():1.f; }
void AudioEngine::setInstrumentTrackPan(int t,float v) noexcept { const juce::ScopedLock l(stateLock); if(t>=0&&t<(int)instrumentPlayback.size()) instrumentPlayback[(size_t)t]->pan.store(juce::jlimit(-1.f,1.f,v)); }
float AudioEngine::getInstrumentTrackPan(int t) const noexcept { const juce::ScopedLock l(stateLock); return t>=0&&t<(int)instrumentPlayback.size()?instrumentPlayback[(size_t)t]->pan.load():0.f; }
void AudioEngine::setInstrumentTrackMuted(int t,bool v) noexcept { const juce::ScopedLock l(stateLock); if(t>=0&&t<(int)instrumentPlayback.size()) { instrumentPlayback[(size_t)t]->muted.store(v); instrumentPanicPending.store(true,std::memory_order_release); instrumentResumePending.store(playing.load(std::memory_order_relaxed),std::memory_order_release); } }
bool AudioEngine::isInstrumentTrackMuted(int t) const noexcept { const juce::ScopedLock l(stateLock); return t>=0&&t<(int)instrumentPlayback.size()&&instrumentPlayback[(size_t)t]->muted.load(); }
void AudioEngine::setInstrumentTrackSolo(int t,bool v) noexcept { const juce::ScopedLock l(stateLock); if(t>=0&&t<(int)instrumentPlayback.size()) { instrumentPlayback[(size_t)t]->solo.store(v); instrumentPanicPending.store(true,std::memory_order_release); instrumentResumePending.store(playing.load(std::memory_order_relaxed),std::memory_order_release); } }
bool AudioEngine::isInstrumentTrackSolo(int t) const noexcept { const juce::ScopedLock l(stateLock); return t>=0&&t<(int)instrumentPlayback.size()&&instrumentPlayback[(size_t)t]->solo.load(); }

int AudioEngine::getAudioTrackCount() const noexcept
{
    const juce::ScopedLock lock(stateLock);
    return (int) tracks.size();
}

bool AudioEngine::ensureInstrumentPlaybackTracks(int trackCount) noexcept
{
    const int safeCount = juce::jmax(1, trackCount);
    int currentCount = 0;
    {
        const juce::ScopedLock lock(stateLock);
        currentCount = (int) instrumentPlayback.size();
        if (currentCount >= safeCount)
            return true;
    }

    std::vector<std::unique_ptr<InstrumentPlaybackState>> additions;
    std::vector<std::unique_ptr<InstrumentPlaybackState>> replacement;
    try
    {
        additions.reserve((size_t) (safeCount - currentCount));
        replacement.reserve((size_t) safeCount);
        for (int i = currentCount; i < safeCount; ++i)
            additions.push_back(std::make_unique<InstrumentPlaybackState>());
    }
    catch (...)
    {
        return false;
    }

    const bool wasInitialised = initialised.load();
    const bool wasPlaying = playing.load();
    playing.store(false);
    if (wasInitialised)
        deviceManager.removeAudioCallback(this);

    {
        const juce::ScopedLock lock(stateLock);
        if ((int) instrumentPlayback.size() < safeCount)
        {
            for (auto& state : instrumentPlayback)
                replacement.push_back(std::move(state));
            for (auto& state : additions)
                replacement.push_back(std::move(state));
            instrumentPlayback.swap(replacement);
        }
    }

    if (wasPlaying)
    {
        instrumentPanicPending.store(true, std::memory_order_release);
        instrumentResumePending.store(true, std::memory_order_release);
    }
    if (wasInitialised)
        deviceManager.addAudioCallback(this);
    if (wasPlaying)
        setPlaying(true);
    return true;
}

bool AudioEngine::resetInstrumentPlayback(int trackCount) noexcept
{
    const int safeCount = juce::jmax(1, trackCount);
    int currentCount = 0;
    {
        const juce::ScopedLock lock(stateLock);
        currentCount = (int) instrumentPlayback.size();
    }

    std::vector<std::unique_ptr<InstrumentPlaybackState>> replacement;
    if (currentCount != safeCount)
    {
        try
        {
            replacement.reserve((size_t) safeCount);
            for (int i = 0; i < safeCount; ++i)
                replacement.push_back(std::make_unique<InstrumentPlaybackState>());
        }
        catch (...)
        {
            return false;
        }
    }

    const bool wasInitialised = initialised.load();
    const bool wasPlaying = playing.load();
    playing.store(false);
    if (wasInitialised)
        deviceManager.removeAudioCallback(this);

    bool published = false;
    {
        const juce::ScopedLock lock(stateLock);
        if (currentCount == safeCount && (int) instrumentPlayback.size() == safeCount)
        {
            for (auto& statePtr : instrumentPlayback)
            {
                auto& state = *statePtr;
                std::atomic_store(&state.noteSnapshot, std::shared_ptr<InstrumentNoteSnapshot>{});
                state.clipStartSeconds.store(0.0, std::memory_order_relaxed);
                state.clipLengthSeconds.store(0.0, std::memory_order_relaxed);
                state.tempoBpm.store(120.0, std::memory_order_relaxed);
                state.gain.store(1.0f, std::memory_order_relaxed);
                state.pan.store(0.0f, std::memory_order_relaxed);
                state.muted.store(false, std::memory_order_relaxed);
                state.solo.store(false, std::memory_order_relaxed);
            }
            published = true;
        }
        else if ((int) instrumentPlayback.size() == currentCount)
        {
            instrumentPlayback.swap(replacement);
            published = true;
        }
    }

    if (wasPlaying && published)
    {
        instrumentPanicPending.store(true, std::memory_order_release);
        instrumentResumePending.store(true, std::memory_order_release);
    }
    if (wasInitialised)
        deviceManager.addAudioCallback(this);
    if (wasPlaying && published)
        setPlaying(true);
    return published;
}

int AudioEngine::addAudioTrack()
{
    int currentCount = 0;
    {
        const juce::ScopedLock lock(stateLock);
        currentCount = (int) tracks.size();
    }

    std::unique_ptr<AudioTrackState> newTrack;
    std::vector<std::unique_ptr<AudioTrackState>> replacement;
    try
    {
        newTrack = std::make_unique<AudioTrackState>();
        replacement.reserve((size_t) currentCount + 1);
    }
    catch (...)
    {
        return -1;
    }

    const bool wasInitialised = initialised.load();
    const bool wasPlaying = playing.load();
    playing.store(false);
    if (wasInitialised)
        deviceManager.removeAudioCallback(this);

    int index = -1;
    {
        const juce::ScopedLock lock(stateLock);
        for (auto& track : tracks)
            replacement.push_back(std::move(track));
        replacement.push_back(std::move(newTrack));
        tracks.swap(replacement);
        index = (int) tracks.size() - 1;
    }

    if (wasPlaying)
    {
        instrumentPanicPending.store(true, std::memory_order_release);
        instrumentResumePending.store(true, std::memory_order_release);
    }
    if (wasInitialised)
        deviceManager.addAudioCallback(this);
    if (wasPlaying)
        setPlaying(true);
    return index;
}

bool AudioEngine::removeAudioTrack(int trackIndex)
{
    {
        const juce::ScopedLock lock(stateLock);
        if (trackIndex < 0 || trackIndex != (int) tracks.size() - 1)
            return false;
    }

    const bool wasInitialised = initialised.load();
    const bool wasPlaying = playing.load();
    playing.store(false);
    if (wasInitialised)
        deviceManager.removeAudioCallback(this);

    bool removed = false;
    {
        const juce::ScopedLock lock(stateLock);
        if (trackIndex >= 0 && trackIndex < (int) tracks.size())
        {
            tracks.erase(tracks.begin() + trackIndex);
            removed = true;
        }
    }

    if (wasPlaying)
    {
        instrumentPanicPending.store(true, std::memory_order_release);
        instrumentResumePending.store(true, std::memory_order_release);
    }
    if (wasInitialised)
        deviceManager.addAudioCallback(this);
    if (wasPlaying)
        setPlaying(true);
    return removed;
}

void AudioEngine::setTrackGain(int trackIndex, float gain) noexcept { if (isValidTrackIndex(trackIndex)) tracks[(size_t)trackIndex]->gain.store(juce::jlimit(0.0f, 2.0f, gain)); }
float AudioEngine::getTrackGain(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex]->gain.load() : 0.0f; }
void AudioEngine::setTrackPan(int trackIndex, float pan) noexcept { if (isValidTrackIndex(trackIndex)) tracks[(size_t)trackIndex]->pan.store(juce::jlimit(-1.0f, 1.0f, pan)); }
float AudioEngine::getTrackPan(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex]->pan.load() : 0.0f; }
void AudioEngine::setTrackMuted(int trackIndex, bool muted) noexcept { if (isValidTrackIndex(trackIndex)) tracks[(size_t)trackIndex]->muted.store(muted); }
bool AudioEngine::isTrackMuted(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) && tracks[(size_t)trackIndex]->muted.load(); }
void AudioEngine::setTrackSolo(int trackIndex, bool solo) noexcept
{
    if (!isValidTrackIndex(trackIndex)) return;
    tracks[(size_t)trackIndex]->solo.store(solo);
    instrumentPanicPending.store(true, std::memory_order_release);
    instrumentResumePending.store(playing.load(std::memory_order_relaxed), std::memory_order_release);
}
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
    if (!isValidTrackIndex(trackIndex)) return;
    auto& track = *tracks[(size_t)trackIndex];
    const auto current = std::atomic_load(&track.warpMarkerSnapshot);
    std::shared_ptr<WarpMarkerSnapshot> snapshot;
    if (current != nullptr)
    {
        try { snapshot = std::make_shared<WarpMarkerSnapshot>(*current); }
        catch (...) { return; }
        snapshot->enabled = enabled;
    }
    track.warpEnabled.store(enabled, std::memory_order_relaxed);
    if (snapshot != nullptr)
        std::atomic_store(&track.warpMarkerSnapshot, std::move(snapshot));
}

bool AudioEngine::isTrackWarpEnabled(int trackIndex) const noexcept
{
    return isValidTrackIndex(trackIndex) && tracks[(size_t)trackIndex]->warpEnabled.load(std::memory_order_relaxed);
}

void AudioEngine::setTrackWarpMode(int trackIndex, int mode) noexcept
{
    if (!isValidTrackIndex(trackIndex)) return;
    auto& track = *tracks[(size_t)trackIndex];
    const int clampedMode = juce::jlimit(0, 4, mode);
    const auto current = std::atomic_load(&track.warpMarkerSnapshot);
    std::shared_ptr<WarpMarkerSnapshot> snapshot;
    if (current != nullptr)
    {
        try { snapshot = std::make_shared<WarpMarkerSnapshot>(*current); }
        catch (...) { return; }
        snapshot->mode = clampedMode;
    }
    track.warpMode.store(clampedMode, std::memory_order_relaxed);
    if (snapshot != nullptr)
        std::atomic_store(&track.warpMarkerSnapshot, std::move(snapshot));
}

int AudioEngine::getTrackWarpMode(int trackIndex) const noexcept
{
    return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex]->warpMode.load(std::memory_order_relaxed) : 0;
}

void AudioEngine::resetTrackWarpMarkers(int trackIndex) noexcept
{
    if (!isValidTrackIndex(trackIndex)) return;
    auto& track = *tracks[(size_t)trackIndex];
    const double targetLength = juce::jmax(0.0, track.lengthSeconds.load(std::memory_order_relaxed));
    const auto buffer = std::atomic_load(&track.buffer);
    double sourceLength = targetLength;
    if (buffer != nullptr && targetLength > 0.0)
    {
        const double retainedRate = getAudioBufferSampleRate(trackIndex);
        if (retainedRate > 0.0)
            sourceLength = static_cast<double>(buffer->getNumSamples()) / retainedRate;
    }
    if (sourceLength <= 0.0 || targetLength <= 0.0)
    {
        track.warpMarkerCount.store(0, std::memory_order_release);
        std::atomic_store(&track.warpMarkerSnapshot, std::shared_ptr<WarpMarkerSnapshot>{});
        return;
    }
    std::shared_ptr<WarpMarkerSnapshot> snapshot;
    try { snapshot = std::make_shared<WarpMarkerSnapshot>(); }
    catch (...) { return; }
    snapshot->enabled = track.warpEnabled.load(std::memory_order_relaxed);
    snapshot->mode = track.warpMode.load(std::memory_order_relaxed);
    snapshot->count = 2;
    snapshot->source[0] = 0.0; snapshot->target[0] = 0.0;
    snapshot->source[1] = sourceLength; snapshot->target[1] = targetLength;
    track.warpSourceSeconds[0].store(0.0, std::memory_order_relaxed);
    track.warpTargetSeconds[0].store(0.0, std::memory_order_relaxed);
    track.warpSourceSeconds[1].store(sourceLength, std::memory_order_relaxed);
    track.warpTargetSeconds[1].store(targetLength, std::memory_order_relaxed);
    track.warpMarkerCount.store(2, std::memory_order_release);
    std::atomic_store(&track.warpMarkerSnapshot, std::move(snapshot));
}

bool AudioEngine::restoreTrackWarpState(int trackIndex, bool enabled, int mode,
                                            const std::vector<std::pair<double, double>>& markers) noexcept
{
    if (!isValidTrackIndex(trackIndex)) return false;
    auto& track = *tracks[(size_t)trackIndex];
    const auto buffer = std::atomic_load(&track.buffer);
    const double rate = track.bufferSampleRate.load(std::memory_order_relaxed);
    const double sourceLength = buffer != nullptr && rate > 0.0
        ? static_cast<double>(buffer->getNumSamples()) / rate : 0.0;
    const double targetLength = juce::jmax(0.0, track.lengthSeconds.load(std::memory_order_relaxed));
    if (sourceLength <= 0.0 || targetLength <= 0.0) return false;

    std::shared_ptr<WarpMarkerSnapshot> snapshot;
    try { snapshot = std::make_shared<WarpMarkerSnapshot>(); }
    catch (...) { return false; }

    snapshot->enabled = enabled;
    snapshot->mode = juce::jlimit(0, 4, mode);
    snapshot->count = 2;
    snapshot->source[0] = 0.0;
    snapshot->target[0] = 0.0;
    snapshot->source[1] = sourceLength;
    snapshot->target[1] = targetLength;

    for (const auto& marker : markers)
    {
        if (snapshot->count >= maxWarpMarkers) break;
        const double sourceSeconds = marker.first;
        const double targetSeconds = marker.second;
        const int endIndex = snapshot->count - 1;
        if (!std::isfinite(sourceSeconds) || !std::isfinite(targetSeconds)
            || sourceSeconds <= snapshot->source[(size_t)(endIndex - 1)]
            || targetSeconds <= snapshot->target[(size_t)(endIndex - 1)]
            || sourceSeconds >= sourceLength || targetSeconds >= targetLength)
            continue;
        snapshot->source[(size_t)snapshot->count] = snapshot->source[(size_t)endIndex];
        snapshot->target[(size_t)snapshot->count] = snapshot->target[(size_t)endIndex];
        snapshot->source[(size_t)endIndex] = sourceSeconds;
        snapshot->target[(size_t)endIndex] = targetSeconds;
        ++snapshot->count;
    }

    for (int i = 0; i < snapshot->count; ++i)
    {
        track.warpSourceSeconds[(size_t)i].store(snapshot->source[(size_t)i], std::memory_order_relaxed);
        track.warpTargetSeconds[(size_t)i].store(snapshot->target[(size_t)i], std::memory_order_relaxed);
    }
    track.warpMarkerCount.store(snapshot->count, std::memory_order_release);
    track.warpMode.store(snapshot->mode, std::memory_order_relaxed);
    track.warpEnabled.store(enabled, std::memory_order_relaxed);
    std::atomic_store(&track.warpMarkerSnapshot, std::move(snapshot));
    return true;
}

bool AudioEngine::addTrackWarpMarker(int trackIndex, double sourceSeconds, double targetSeconds) noexcept
{
    if (!isValidTrackIndex(trackIndex)) return false;
    auto& track = *tracks[(size_t)trackIndex];
    int count = track.warpMarkerCount.load(std::memory_order_acquire);
    if (count < 2) { resetTrackWarpMarkers(trackIndex); count = track.warpMarkerCount.load(std::memory_order_acquire); }
    if (count < 2 || count >= maxWarpMarkers) return false;

    const auto buffer = std::atomic_load(&track.buffer);
    const double retainedRate = track.bufferSampleRate.load(std::memory_order_relaxed);
    const double sourceLength = buffer != nullptr && retainedRate > 0.0
        ? static_cast<double>(buffer->getNumSamples()) / retainedRate
        : 0.0;
    if (!std::isfinite(sourceLength) || sourceLength <= 0.002) return false;
    sourceSeconds = juce::jlimit(0.001, sourceLength - 0.001, sourceSeconds);
    int insertAt = 1;
    while (insertAt < count && track.warpSourceSeconds[(size_t)insertAt].load(std::memory_order_relaxed) < sourceSeconds) ++insertAt;
    if (insertAt <= 0 || insertAt >= count) return false;

    const double prevSource = track.warpSourceSeconds[(size_t)(insertAt - 1)].load(std::memory_order_relaxed);
    const double nextSource = track.warpSourceSeconds[(size_t)insertAt].load(std::memory_order_relaxed);
    if (sourceSeconds - prevSource < 0.001 || nextSource - sourceSeconds < 0.001) return false;
    const double prevTarget = track.warpTargetSeconds[(size_t)(insertAt - 1)].load(std::memory_order_relaxed);
    const double nextTarget = track.warpTargetSeconds[(size_t)insertAt].load(std::memory_order_relaxed);
    targetSeconds = juce::jlimit(prevTarget + 0.001, nextTarget - 0.001, targetSeconds);

    std::shared_ptr<WarpMarkerSnapshot> snapshot;
    try { snapshot = std::make_shared<WarpMarkerSnapshot>(); }
    catch (...) { return false; }
    snapshot->enabled = true;
    snapshot->mode = track.warpMode.load(std::memory_order_relaxed);
    snapshot->count = count + 1;
    for (int i = 0, src = 0; i < snapshot->count; ++i)
    {
        if (i == insertAt)
        {
            snapshot->source[(size_t)i] = sourceSeconds;
            snapshot->target[(size_t)i] = targetSeconds;
        }
        else
        {
            snapshot->source[(size_t)i] = track.warpSourceSeconds[(size_t)src].load(std::memory_order_relaxed);
            snapshot->target[(size_t)i] = track.warpTargetSeconds[(size_t)src].load(std::memory_order_relaxed);
            ++src;
        }
    }
    for (int i = 0; i < snapshot->count; ++i)
    {
        track.warpSourceSeconds[(size_t)i].store(snapshot->source[(size_t)i], std::memory_order_relaxed);
        track.warpTargetSeconds[(size_t)i].store(snapshot->target[(size_t)i], std::memory_order_relaxed);
    }
    std::atomic_store(&track.warpMarkerSnapshot, snapshot);
    track.warpMarkerCount.store(snapshot->count, std::memory_order_release);
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

    std::shared_ptr<WarpMarkerSnapshot> snapshot;
    try { snapshot = std::make_shared<WarpMarkerSnapshot>(); }
    catch (...) { return false; }
    snapshot->enabled = true;
    snapshot->mode = track.warpMode.load(std::memory_order_relaxed);
    snapshot->count = count;
    for (int i = 0; i < count; ++i)
    {
        snapshot->source[(size_t)i] = track.warpSourceSeconds[(size_t)i].load(std::memory_order_relaxed);
        snapshot->target[(size_t)i] = i == markerIndex ? clamped
            : track.warpTargetSeconds[(size_t)i].load(std::memory_order_relaxed);
    }
    track.warpTargetSeconds[(size_t)markerIndex].store(clamped, std::memory_order_relaxed);
    std::atomic_store(&track.warpMarkerSnapshot, std::move(snapshot));
    track.warpEnabled.store(true, std::memory_order_release);
    return true;
}

bool AudioEngine::removeTrackWarpMarker(int trackIndex, int markerIndex) noexcept
{
    if (!isValidTrackIndex(trackIndex)) return false;
    auto& track = *tracks[(size_t)trackIndex];
    const int count = track.warpMarkerCount.load(std::memory_order_acquire);
    if (markerIndex <= 0 || markerIndex >= count - 1) return false;

    std::shared_ptr<WarpMarkerSnapshot> snapshot;
    try { snapshot = std::make_shared<WarpMarkerSnapshot>(); }
    catch (...) { return false; }
    snapshot->enabled = true;
    snapshot->mode = track.warpMode.load(std::memory_order_relaxed);
    snapshot->count = count - 1;
    for (int i = 0, src = 0; i < snapshot->count; ++i, ++src)
    {
        if (src == markerIndex) ++src;
        snapshot->source[(size_t)i] = track.warpSourceSeconds[(size_t)src].load(std::memory_order_relaxed);
        snapshot->target[(size_t)i] = track.warpTargetSeconds[(size_t)src].load(std::memory_order_relaxed);
    }
    for (int i = 0; i < snapshot->count; ++i)
    {
        track.warpSourceSeconds[(size_t)i].store(snapshot->source[(size_t)i], std::memory_order_relaxed);
        track.warpTargetSeconds[(size_t)i].store(snapshot->target[(size_t)i], std::memory_order_relaxed);
    }
    std::atomic_store(&track.warpMarkerSnapshot, snapshot);
    track.warpMarkerCount.store(snapshot->count, std::memory_order_release);
    return true;
}

AudioEngine::WarpSnapshot AudioEngine::getTrackWarpSnapshot(int trackIndex) const noexcept
{
    WarpSnapshot result;
    if (!isValidTrackIndex(trackIndex)) return result;
    const auto snapshot = std::atomic_load(&tracks[(size_t)trackIndex]->warpMarkerSnapshot);
    if (snapshot == nullptr) return result;
    result.enabled = snapshot->enabled;
    result.mode = juce::jlimit(0, 4, snapshot->mode);
    result.count = juce::jlimit(0, maxWarpMarkers, snapshot->count);
    for (int i = 0; i < result.count; ++i)
    {
        result.source[(size_t)i] = snapshot->source[(size_t)i];
        result.target[(size_t)i] = snapshot->target[(size_t)i];
    }
    return result;
}

int AudioEngine::getTrackWarpMarkerCount(int trackIndex) const noexcept
{
    if (!isValidTrackIndex(trackIndex)) return 0;
    const auto snapshot = std::atomic_load(&tracks[(size_t)trackIndex]->warpMarkerSnapshot);
    return snapshot != nullptr ? snapshot->count : 0;
}

double AudioEngine::getTrackWarpMarkerSourceSeconds(int trackIndex, int markerIndex) const noexcept
{
    if (!isValidTrackIndex(trackIndex)) return 0.0;
    const auto snapshot = std::atomic_load(&tracks[(size_t)trackIndex]->warpMarkerSnapshot);
    return snapshot != nullptr && markerIndex >= 0 && markerIndex < snapshot->count ? snapshot->source[(size_t)markerIndex] : 0.0;
}

double AudioEngine::getTrackWarpMarkerTargetSeconds(int trackIndex, int markerIndex) const noexcept
{
    if (!isValidTrackIndex(trackIndex)) return 0.0;
    const auto snapshot = std::atomic_load(&tracks[(size_t)trackIndex]->warpMarkerSnapshot);
    return snapshot != nullptr && markerIndex >= 0 && markerIndex < snapshot->count ? snapshot->target[(size_t)markerIndex] : 0.0;
}

AudioEngine::AudioBufferSnapshot AudioEngine::getAudioTrackSnapshot(int trackIndex) const noexcept
{
    AudioBufferSnapshot snapshot;
    if (!isValidTrackIndex(trackIndex)) return snapshot;

    const auto& track = *tracks[(size_t)trackIndex];
    snapshot.loaded = track.loaded.load(std::memory_order_acquire);
    if (!snapshot.loaded) return snapshot;

    snapshot.buffer = std::atomic_load(&track.buffer);
    snapshot.numSamples = snapshot.buffer != nullptr ? snapshot.buffer->getNumSamples() : 0;
    snapshot.lengthSeconds = track.lengthSeconds.load(std::memory_order_relaxed);
    snapshot.sampleRate = track.bufferSampleRate.load(std::memory_order_relaxed);
    if (!track.loaded.load(std::memory_order_acquire))
    {
        snapshot.loaded = false;
        snapshot.valid = false;
        snapshot.buffer.reset();
        snapshot.numSamples = 0;
        snapshot.lengthSeconds = 0.0;
        snapshot.sampleRate = 0.0;
        return snapshot;
    }
    snapshot.valid = snapshot.buffer != nullptr
        && snapshot.numSamples > 0
        && std::isfinite(snapshot.lengthSeconds) && snapshot.lengthSeconds > 0.0
        && std::isfinite(snapshot.sampleRate) && snapshot.sampleRate > 0.0;
    return snapshot;
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
    std::shared_ptr<juce::AudioBuffer<float>> decodedBuffer;
    try
    {
        decodedBuffer = std::make_shared<juce::AudioBuffer<float>>(inputChannels, inputSamples);
        decodedBuffer->clear();
    }
    catch (...)
    {
        error = "Not enough memory to decode the selected audio file.";
        return false;
    }
    if (!reader->read(decodedBuffer.get(), 0, inputSamples, 0, true, true)) { error = "Failed to decode the selected audio file."; return false; }
    const auto sourceRate = reader->sampleRate;
    const auto ratio = sourceRate / outputRate;
    if (ratio <= 0.0) { error = "The selected audio file has an invalid sample rate."; return false; }
    const auto outputSamples64 = static_cast<std::int64_t>(std::floor(static_cast<double>(inputSamples) / ratio));
    if (outputSamples64 <= 0 || outputSamples64 > std::numeric_limits<int>::max()) { error = "The resampled audio file is too large to load into memory."; return false; }
    const auto outputSamples = static_cast<int>(outputSamples64);
    std::shared_ptr<juce::AudioBuffer<float>> newBuffer;
    try
    {
        newBuffer = std::make_shared<juce::AudioBuffer<float>>(inputChannels, outputSamples);
        newBuffer->clear();
        if (std::abs(sourceRate - outputRate) > 0.01)
            for (int channel = 0; channel < inputChannels; ++channel) { juce::LagrangeInterpolator interpolator; interpolator.process(ratio, decodedBuffer->getReadPointer(channel), newBuffer->getWritePointer(channel), outputSamples); }
        else newBuffer->makeCopyOf(*decodedBuffer);
    }
    catch (...)
    {
        error = "Not enough memory to prepare the selected audio file.";
        return false;
    }

    std::shared_ptr<WarpMarkerSnapshot> newWarpSnapshot;
    try
    {
        newWarpSnapshot = std::make_shared<WarpMarkerSnapshot>();
    }
    catch (...)
    {
        error = "Not enough memory to prepare the audio warp state.";
        return false;
    }
    juce::String newFileName;
    try { newFileName = file.getFileName(); }
    catch (...) { error = "Not enough memory to prepare the audio file name."; return false; }
    const double newLengthSeconds = static_cast<double>(outputSamples) / outputRate;
    newWarpSnapshot->enabled = false;
    newWarpSnapshot->mode = 0;
    newWarpSnapshot->count = 2;
    newWarpSnapshot->source[0] = 0.0;
    newWarpSnapshot->target[0] = 0.0;
    newWarpSnapshot->source[1] = newLengthSeconds;
    newWarpSnapshot->target[1] = newLengthSeconds;

    const bool wasInitialised = initialised.load();
    instrumentResumePending.store(false, std::memory_order_release);
    instrumentPanicPending.store(true, std::memory_order_release);
    playing.store(false); resetTransport();
    if (wasInitialised) deviceManager.removeAudioCallback(this);
    auto& track = *tracks[(size_t)trackIndex];
    track.loaded.store(false, std::memory_order_release);
    std::atomic_store(&track.buffer, std::move(newBuffer));
    track.contentRevision.fetch_add(1, std::memory_order_relaxed);
    track.fileName = std::move(newFileName); track.lengthSeconds.store(newLengthSeconds); track.bufferSampleRate.store(outputRate); track.startSeconds.store(0.0);
    track.warpSourceSeconds[0].store(0.0, std::memory_order_relaxed);
    track.warpTargetSeconds[0].store(0.0, std::memory_order_relaxed);
    track.warpSourceSeconds[1].store(newLengthSeconds, std::memory_order_relaxed);
    track.warpTargetSeconds[1].store(newLengthSeconds, std::memory_order_relaxed);
    track.warpMarkerCount.store(2, std::memory_order_release);
    track.warpEnabled.store(false, std::memory_order_relaxed);
    track.warpMode.store(0, std::memory_order_relaxed);
    std::atomic_store(&track.warpMarkerSnapshot, std::move(newWarpSnapshot));
    track.loaded.store(true, std::memory_order_release);
    { const juce::ScopedLock lock(stateLock); lastError.clear(); }
    if (wasInitialised) deviceManager.addAudioCallback(this);
    return true;
}

void AudioEngine::clearAudioTrack(int trackIndex)
{
    if (!isValidTrackIndex(trackIndex)) return;
    const bool wasInitialised = initialised.load();
    instrumentResumePending.store(false, std::memory_order_release);
    instrumentPanicPending.store(true, std::memory_order_release);
    playing.store(false); resetTransport();
    if (wasInitialised) deviceManager.removeAudioCallback(this);
    auto& track = *tracks[(size_t)trackIndex];
    track.loaded.store(false, std::memory_order_release);
    std::atomic_store(&track.buffer, std::shared_ptr<juce::AudioBuffer<float>>{}); track.contentRevision.fetch_add(1, std::memory_order_relaxed); track.lengthSeconds.store(0.0); track.bufferSampleRate.store(0.0); track.startSeconds.store(0.0); track.fileName.clear();
    track.warpEnabled.store(false, std::memory_order_relaxed);
    track.warpMode.store(0, std::memory_order_relaxed);
    track.warpMarkerCount.store(0, std::memory_order_release);
    std::atomic_store(&track.warpMarkerSnapshot, std::shared_ptr<WarpMarkerSnapshot>{});
    if (wasInitialised) deviceManager.addAudioCallback(this);
}

bool AudioEngine::splitAudioTrack(int trackIndex, double splitProjectSeconds, int targetTrackIndex, juce::String& error)
{
    error.clear();
    if (!isValidTrackIndex(trackIndex) || !hasAudioFile(trackIndex)) { error = "Select a loaded audio clip first."; return false; }
    const auto rate = getAudioBufferSampleRate(trackIndex);
    if (rate <= 0.0) { error = "The loaded audio clip has no valid sample rate."; return false; }
    auto& source = *tracks[(size_t)trackIndex];
    const auto startSeconds = source.startSeconds.load();
    const auto lengthSeconds = source.lengthSeconds.load();
    const auto splitOffsetSeconds = splitProjectSeconds - startSeconds;
    if (splitOffsetSeconds <= 0.01 || splitOffsetSeconds >= lengthSeconds - 0.01) { error = "Place the playhead inside the audio clip to split it."; return false; }
    if (!isValidTrackIndex(targetTrackIndex) || targetTrackIndex == trackIndex
        || tracks[(size_t)targetTrackIndex]->loaded.load(std::memory_order_acquire))
    {
        error = "Select an empty audio track for the second clip segment.";
        return false;
    }
    const auto sourceWarp = getTrackWarpSnapshot(trackIndex);
    double splitSourceSeconds = splitOffsetSeconds;
    if (sourceWarp.enabled && sourceWarp.count >= 2)
    {
        int segment = 0;
        while (segment < sourceWarp.count - 2 && splitOffsetSeconds > sourceWarp.target[(size_t)(segment + 1)])
            ++segment;
        const double ta = sourceWarp.target[(size_t)segment];
        const double tb = sourceWarp.target[(size_t)(segment + 1)];
        const double sa = sourceWarp.source[(size_t)segment];
        const double sb = sourceWarp.source[(size_t)(segment + 1)];
        const double span = juce::jmax(0.000001, tb - ta);
        const double alpha = juce::jlimit(0.0, 1.0, (splitOffsetSeconds - ta) / span);
        splitSourceSeconds = sa + (sb - sa) * alpha;
    }
    const auto splitSample = static_cast<int>(std::llround(splitSourceSeconds * rate));
    const double actualSplitSourceSeconds = static_cast<double>(splitSample) / rate;
    const auto sourceBuffer = std::atomic_load(&source.buffer);
    if (sourceBuffer == nullptr) { error = "The loaded audio clip buffer is unavailable."; return false; }
    const auto sourceSamples = sourceBuffer->getNumSamples();
    if (splitSample <= 0 || splitSample >= sourceSamples) { error = "The split position is outside the audio clip."; return false; }
    const auto rightSamples = sourceSamples - splitSample;
    const auto channels = sourceBuffer->getNumChannels();
    std::shared_ptr<juce::AudioBuffer<float>> leftBuffer;
    std::shared_ptr<juce::AudioBuffer<float>> rightBuffer;
    try
    {
        leftBuffer = std::make_shared<juce::AudioBuffer<float>>(channels, splitSample);
        rightBuffer = std::make_shared<juce::AudioBuffer<float>>(channels, rightSamples);
        leftBuffer->clear();
        rightBuffer->clear();
        for (int channel = 0; channel < channels; ++channel)
        {
            leftBuffer->copyFrom(channel, 0, *sourceBuffer, channel, 0, splitSample);
            rightBuffer->copyFrom(channel, 0, *sourceBuffer, channel, splitSample, rightSamples);
        }
    }
    catch (...)
    {
        error = "Not enough memory to split the audio clip.";
        return false;
    }
    const double leftTargetLength = sourceWarp.enabled ? splitOffsetSeconds : static_cast<double>(splitSample) / rate;
    const double rightTargetLength = sourceWarp.enabled ? juce::jmax(0.0, lengthSeconds - splitOffsetSeconds) : static_cast<double>(rightSamples) / rate;
    std::shared_ptr<WarpMarkerSnapshot> leftWarpSnapshot;
    std::shared_ptr<WarpMarkerSnapshot> rightWarpSnapshot;
    try
    {
        leftWarpSnapshot = std::make_shared<WarpMarkerSnapshot>();
        rightWarpSnapshot = std::make_shared<WarpMarkerSnapshot>();
    }
    catch (...)
    {
        error = "Not enough memory to prepare the split warp state.";
        return false;
    }

    leftWarpSnapshot->enabled = sourceWarp.enabled;
    leftWarpSnapshot->mode = sourceWarp.mode;
    leftWarpSnapshot->count = 2;
    leftWarpSnapshot->source[0] = 0.0;
    leftWarpSnapshot->target[0] = 0.0;
    leftWarpSnapshot->source[1] = actualSplitSourceSeconds;
    leftWarpSnapshot->target[1] = leftTargetLength;

    rightWarpSnapshot->enabled = sourceWarp.enabled;
    rightWarpSnapshot->mode = sourceWarp.mode;
    rightWarpSnapshot->count = 2;
    rightWarpSnapshot->source[0] = 0.0;
    rightWarpSnapshot->target[0] = 0.0;
    rightWarpSnapshot->source[1] = static_cast<double>(rightSamples) / rate;
    rightWarpSnapshot->target[1] = rightTargetLength;

    if (sourceWarp.count >= 2)
    {
        for (int markerIndex = 1; markerIndex < sourceWarp.count - 1; ++markerIndex)
        {
            const double markerSource = sourceWarp.source[(size_t)markerIndex];
            const double markerTarget = sourceWarp.target[(size_t)markerIndex];
            if (markerTarget < splitOffsetSeconds - 0.001 && markerSource < actualSplitSourceSeconds - 0.001)
            {
                const int index = leftWarpSnapshot->count - 1;
                if (leftWarpSnapshot->count < maxWarpMarkers)
                {
                    leftWarpSnapshot->source[(size_t)leftWarpSnapshot->count] = leftWarpSnapshot->source[(size_t)index];
                    leftWarpSnapshot->target[(size_t)leftWarpSnapshot->count] = leftWarpSnapshot->target[(size_t)index];
                    leftWarpSnapshot->source[(size_t)index] = markerSource;
                    leftWarpSnapshot->target[(size_t)index] = markerTarget;
                    ++leftWarpSnapshot->count;
                }
            }
            else if (markerTarget > splitOffsetSeconds + 0.001 && markerSource > actualSplitSourceSeconds + 0.001)
            {
                const int index = rightWarpSnapshot->count - 1;
                if (rightWarpSnapshot->count < maxWarpMarkers)
                {
                    rightWarpSnapshot->source[(size_t)rightWarpSnapshot->count] = rightWarpSnapshot->source[(size_t)index];
                    rightWarpSnapshot->target[(size_t)rightWarpSnapshot->count] = rightWarpSnapshot->target[(size_t)index];
                    rightWarpSnapshot->source[(size_t)index] = markerSource - actualSplitSourceSeconds;
                    rightWarpSnapshot->target[(size_t)index] = markerTarget - splitOffsetSeconds;
                    ++rightWarpSnapshot->count;
                }
            }
        }
    }

    juce::String rightFileName;
    try { rightFileName = source.fileName + " - Split"; }
    catch (...) { error = "Not enough memory to prepare the split clip name."; return false; }

    const bool wasInitialised = initialised.load();
    const auto savedPlaying = playing.load();
    if (wasInitialised) deviceManager.removeAudioCallback(this);
    playing.store(false);
    source.loaded.store(false, std::memory_order_release);
    std::atomic_store(&source.buffer, std::move(leftBuffer));
    source.contentRevision.fetch_add(1, std::memory_order_relaxed);

    source.lengthSeconds.store(leftTargetLength);
    source.bufferSampleRate.store(rate);
    source.loaded.store(true, std::memory_order_release);
    auto& right = *tracks[(size_t)targetTrackIndex];
    right.loaded.store(false, std::memory_order_release);
    std::atomic_store(&right.buffer, std::move(rightBuffer));
    right.contentRevision.fetch_add(1, std::memory_order_relaxed);
    right.fileName = std::move(rightFileName);
    right.lengthSeconds.store(rightTargetLength);
    right.bufferSampleRate.store(rate);
    right.startSeconds.store(startSeconds + splitOffsetSeconds);
    right.gain.store(source.gain.load());
    right.pan.store(source.pan.load());
    right.muted.store(source.muted.load());
    right.solo.store(source.solo.load());
    right.warpEnabled.store(false, std::memory_order_relaxed);
    right.warpMode.store(sourceWarp.mode, std::memory_order_relaxed);
    right.loaded.store(true, std::memory_order_release);
    for (int i = 0; i < leftWarpSnapshot->count; ++i)
    {
        source.warpSourceSeconds[(size_t)i].store(leftWarpSnapshot->source[(size_t)i], std::memory_order_relaxed);
        source.warpTargetSeconds[(size_t)i].store(leftWarpSnapshot->target[(size_t)i], std::memory_order_relaxed);
    }
    source.warpMarkerCount.store(leftWarpSnapshot->count, std::memory_order_release);
    source.warpMode.store(sourceWarp.mode, std::memory_order_relaxed);
    source.warpEnabled.store(sourceWarp.enabled, std::memory_order_relaxed);
    std::atomic_store(&source.warpMarkerSnapshot, std::move(leftWarpSnapshot));

    for (int i = 0; i < rightWarpSnapshot->count; ++i)
    {
        right.warpSourceSeconds[(size_t)i].store(rightWarpSnapshot->source[(size_t)i], std::memory_order_relaxed);
        right.warpTargetSeconds[(size_t)i].store(rightWarpSnapshot->target[(size_t)i], std::memory_order_relaxed);
    }
    right.warpMarkerCount.store(rightWarpSnapshot->count, std::memory_order_release);
    right.warpMode.store(sourceWarp.mode, std::memory_order_relaxed);
    right.warpEnabled.store(sourceWarp.enabled, std::memory_order_relaxed);
    std::atomic_store(&right.warpMarkerSnapshot, std::move(rightWarpSnapshot));
    if (savedPlaying)
    {
        instrumentPanicPending.store(true, std::memory_order_release);
        instrumentResumePending.store(true, std::memory_order_release);
    }
    if (wasInitialised) deviceManager.addAudioCallback(this);
    if (savedPlaying) setPlaying(true);
    return true;
}

bool AudioEngine::hasAudioFile(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) && tracks[(size_t)trackIndex]->loaded.load(std::memory_order_acquire); }
juce::String AudioEngine::getAudioFileName(int trackIndex) const { return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex]->fileName : juce::String{}; }
void AudioEngine::setAudioFileName(int trackIndex, const juce::String& name) { if (isValidTrackIndex(trackIndex)) tracks[(size_t)trackIndex]->fileName = name; }
double AudioEngine::getAudioFileLengthSeconds(int trackIndex) const noexcept { return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex]->lengthSeconds.load() : 0.0; }
void AudioEngine::setAudioFileLengthSeconds(int trackIndex, double seconds) noexcept
{
    if (!isValidTrackIndex(trackIndex) || !std::isfinite(seconds) || seconds <= 0.0) return;
    tracks[(size_t)trackIndex]->lengthSeconds.store(seconds, std::memory_order_relaxed);
}
std::shared_ptr<const juce::AudioBuffer<float>> AudioEngine::getAudioBufferSnapshot(int trackIndex) const noexcept
{
    return isValidTrackIndex(trackIndex) ? std::atomic_load(&tracks[(size_t)trackIndex]->buffer) : nullptr;
}
double AudioEngine::getAudioBufferSampleRate(int trackIndex) const noexcept
{
    return getAudioTrackSnapshot(trackIndex).getSampleRate();
}

std::uint64_t AudioEngine::getAudioContentRevision(int trackIndex) const noexcept
{
    return isValidTrackIndex(trackIndex) ? tracks[(size_t)trackIndex]->contentRevision.load(std::memory_order_relaxed) : 0;
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device == nullptr) return;
    const double newRate = device->getCurrentSampleRate();
    const double oldRate = sampleRate.exchange(newRate);
    if (oldRate > 0.0 && newRate > 0.0 && std::abs(oldRate - newRate) > 0.01)
    {
        const auto oldPosition = transportSamples.load(std::memory_order_relaxed);
        const double positionSeconds = static_cast<double>(oldPosition) / oldRate;
        transportSamples.store(static_cast<std::int64_t>(std::llround(positionSeconds * newRate)),
                               std::memory_order_relaxed);
        playbackClockBaseSeconds.store(positionSeconds, std::memory_order_relaxed);
        playbackClockStartMilliseconds.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
    }
    bufferSize.store(device->getCurrentBufferSizeSamples());
    outputChannels.store(device->getActiveOutputChannels().countNumberOfSetBits());
    LibertyPluginHost::instance().initialise(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
    LibertyOneKnobManager::instance().prepare(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels, float* const* outputChannelData, int numOutputChannels, int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    processLibertyRecordingInput(this, inputChannelData, numInputChannels, numSamples);
    for (int channel = 0; channel < numOutputChannels; ++channel) if (outputChannelData[channel] != nullptr) juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
    renderLibertyPerformAudio(this, outputChannelData, numOutputChannels, numSamples);
    // Drum Sampler audition bus. Runs on the audio callback, even while
    // arrangement transport is stopped. This is not yet track routing.
    if (auto sampler = std::atomic_load(&drumSampler))
    {
        if (!playing.load(std::memory_order_relaxed) || drumSamplerTrack.load(std::memory_order_acquire) < 0)
        {
        sampler->processAuditions();
        if (numOutputChannels > 0 && numSamples > 0)
        {
            juce::AudioBuffer<float> drumBus(juce::jmin(2, numOutputChannels), numSamples);
            drumBus.clear();
            sampler->render(drumBus, 0, numSamples);
            for (int ch = 0; ch < drumBus.getNumChannels(); ++ch)
                if (outputChannelData[ch] != nullptr)
                    juce::FloatVectorOperations::add(outputChannelData[ch],
                        drumBus.getReadPointer(ch), numSamples);
        }
        }
    }

    if (inputMonitoringEnabled.load(std::memory_order_acquire) && inputChannelData != nullptr && numInputChannels > 0)
    {
        const int left = juce::jlimit(0, numInputChannels - 1, monitorInputLeft.load(std::memory_order_relaxed));
        const int right = juce::jlimit(0, numInputChannels - 1, monitorInputRight.load(std::memory_order_relaxed));
        const float* leftIn = inputChannelData[left];
        const float* rightIn = inputChannelData[right];
        if (numOutputChannels > 0 && outputChannelData[0] != nullptr && leftIn != nullptr)
            juce::FloatVectorOperations::add(outputChannelData[0], leftIn, numSamples);
        if (numOutputChannels > 1 && outputChannelData[1] != nullptr)
        {
            const float* source = rightIn != nullptr ? rightIn : leftIn;
            if (source != nullptr) juce::FloatVectorOperations::add(outputChannelData[1], source, numSamples);
        }
    }

    if (!playing.load() && stepPreviewTrack.load(std::memory_order_acquire) >= 0)
    {
        // Audition only the selected instrument, leaving arrangement time untouched.
        const auto previewTrack = stepPreviewTrack.load(std::memory_order_acquire);
        const auto rate = sampleRate.load(std::memory_order_relaxed);
        auto& host = LibertyPluginHost::instance();
        const bool panic = instrumentPanicPending.exchange(false, std::memory_order_acq_rel);
        if (rate > 0.0 && previewTrack >= 0 && previewTrack < (int) instrumentPlayback.size())
        {
            auto& state = *instrumentPlayback[(size_t) previewTrack];
            const auto snapshot = std::atomic_load(&state.noteSnapshot);
            const double cycle = state.clipLengthSeconds.load(std::memory_order_relaxed);
            const auto position = stepPreviewSamples.load(std::memory_order_relaxed);
            juce::MidiBuffer midi;
            if (panic)
                for (int channel = 1; channel <= 16; ++channel)
                {
                    midi.addEvent(juce::MidiMessage::allNotesOff(channel), 0);
                    midi.addEvent(juce::MidiMessage::allSoundOff(channel), 0);
                }
            if (snapshot != nullptr && cycle > 0.0 && host.hasInstrumentForTrack(previewTrack))
            {
                const double blockStart = (double) position / rate;
                const double blockEnd = (double) (position + numSamples) / rate;
                for (const auto& note : snapshot->notes)
                {
                    const int channel = juce::jlimit(1, 16, note.channel);
                    const int pitch = juce::jlimit(0, 127, (int) std::llround(69.0 + 12.0 * std::log2(juce::jmax(0.0001, note.frequency / 440.0))));
                    const float velocity = juce::jlimit(0.0f, 1.0f, note.amplitude / 0.045f);
                    const auto firstCycle = (std::int64_t) std::floor(blockStart / cycle);
                    const auto lastCycle = (std::int64_t) std::floor(blockEnd / cycle);
                    for (auto iteration = firstCycle; iteration <= lastCycle; ++iteration)
                    {
                        const double offset = (double) iteration * cycle;
                        const double start = offset + note.startSeconds;
                        const double end = offset + note.endSeconds;
                        if (start >= blockStart && start < blockEnd)
                            midi.addEvent(juce::MidiMessage::noteOn(channel, pitch, velocity),
                                          juce::jlimit(0, numSamples - 1, (int) std::llround((start - blockStart) * rate)));
                        if (end >= blockStart && end < blockEnd)
                            midi.addEvent(juce::MidiMessage::noteOff(channel, pitch),
                                          juce::jlimit(0, numSamples - 1, (int) std::llround((end - blockStart) * rate)));
                    }
                }
                juce::AudioBuffer<float> bus;
                if (host.renderInstrumentForTrack(previewTrack, bus, numSamples, midi,
                                                  state.gain.load(std::memory_order_relaxed),
                                                  state.pan.load(std::memory_order_relaxed)))
                {
                    for (int ch = 0; ch < juce::jmin(2, numOutputChannels); ++ch)
                        if (outputChannelData[ch] != nullptr)
                            juce::FloatVectorOperations::add(outputChannelData[ch], bus.getReadPointer(ch), numSamples);
                }
            }
            stepPreviewSamples.fetch_add(numSamples, std::memory_order_relaxed);
        }
        return;
    }

    if (!playing.load())
    {
        instrumentResumePending.store(false, std::memory_order_release);
        if (instrumentPanicPending.exchange(false, std::memory_order_acq_rel))
        {
            auto& pluginHost = LibertyPluginHost::instance();
            for (int instrumentTrack = 0; instrumentTrack < (int) instrumentPlayback.size(); ++instrumentTrack)
            {
                if (!pluginHost.hasInstrumentForTrack(instrumentTrack)) continue;
                juce::MidiBuffer panicMidi;
                for (int channel = 1; channel <= 16; ++channel)
                {
                    panicMidi.addEvent(juce::MidiMessage::allNotesOff(channel), 0);
                    panicMidi.addEvent(juce::MidiMessage::allSoundOff(channel), 0);
                }
                juce::AudioBuffer<float> panicBuffer;
                pluginHost.renderInstrumentForTrack(instrumentTrack, panicBuffer, numSamples, panicMidi, 0.0f, 0.0f);
            }
        }
        return;
    }

    const auto position = transportSamples.load();
    const auto projectLength = getProjectLengthSamples();
    const bool hasBoundedAudioProject = projectLength > 0;
    const bool anyPlaybackSolo = isAnyTrackSolo();
    const auto rate = sampleRate.load();
    auto& pluginHost = LibertyPluginHost::instance();
    auto& oneKnob = LibertyOneKnobManager::instance();
    const bool panicInstruments = instrumentPanicPending.exchange(false, std::memory_order_acq_rel);
    const bool resumeInstruments = instrumentResumePending.exchange(false, std::memory_order_acq_rel);

    for (int trackIndex = 0; trackIndex < (int) tracks.size(); ++trackIndex)
    {
        auto& track = *tracks[(size_t)trackIndex];
        if (!track.loaded.load(std::memory_order_acquire)) continue;
        const auto audioBuffer = std::atomic_load(&track.buffer);
        if (audioBuffer == nullptr || track.muted.load() || (anyPlaybackSolo && !track.solo.load())) continue;
        const auto startSample = static_cast<std::int64_t>(std::llround(track.startSeconds.load() * rate));
        const auto warpSnapshot = std::atomic_load(&track.warpMarkerSnapshot);
        const bool warpActive = warpSnapshot != nullptr && warpSnapshot->enabled;
        const int rawMarkerCount = warpSnapshot != nullptr ? warpSnapshot->count : 0;
        const auto clipEnd = startSample + static_cast<std::int64_t>(std::llround(track.lengthSeconds.load() * rate));
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
        const auto sourceChannels = audioBuffer->getNumChannels();

        const int insertChannels = juce::jlimit(1, 2, numOutputChannels);
        juce::AudioBuffer<float> preInsertBaseline(insertChannels, numSamples);
        for (int ch = 0; ch < insertChannels; ++ch)
            if (outputChannelData[ch] != nullptr) preInsertBaseline.copyFrom(ch, 0, outputChannelData[ch], numSamples);
            else preInsertBaseline.clear(ch, 0, numSamples);
        auto processTrackInsertChain = [&]()
        {
            juce::AudioBuffer<float> trackBus(insertChannels, numSamples);
            for (int ch = 0; ch < insertChannels; ++ch)
            {
                if (outputChannelData[ch] == nullptr) { trackBus.clear(ch, 0, numSamples); continue; }
                trackBus.copyFrom(ch, 0, outputChannelData[ch], numSamples);
                trackBus.addFrom(ch, 0, preInsertBaseline, ch, 0, numSamples, -1.0f);
                juce::FloatVectorOperations::copy(outputChannelData[ch], preInsertBaseline.getReadPointer(ch), numSamples);
            }
            for (int slot = 0; slot < LibertyPluginHost::effectSlotsPerTrack; ++slot)
            {
                pluginHost.processAudioEffectSlot(trackIndex, slot, trackBus);
                oneKnob.processInsertSlot(trackIndex, slot, trackBus);
            }
            for (int ch = 0; ch < insertChannels; ++ch)
                if (outputChannelData[ch] != nullptr)
                    juce::FloatVectorOperations::add(outputChannelData[ch], trackBus.getReadPointer(ch), numSamples);
        };

        if (!warpActive || rawMarkerCount < 2)
        {
            const double sourceRate = track.bufferSampleRate.load(std::memory_order_relaxed);
            if (sourceRate <= 0.0) continue;
            const int lastSample = juce::jmax(0, audioBuffer->getNumSamples() - 1);
            const double sourceStep = sourceRate / rate;
            for (int s = 0; s < samplesToMix; ++s)
            {
                const double sourcePosition = juce::jlimit(0.0, static_cast<double>(lastSample),
                                                          (static_cast<double>(sourceOffset) + s) * sourceStep);
                const int i1 = static_cast<int>(std::floor(sourcePosition));
                const int i2 = juce::jmin(lastSample, i1 + 1);
                const float frac = static_cast<float>(sourcePosition - static_cast<double>(i1));
                auto readLinear = [&](int channel)
                {
                    const float* data = audioBuffer->getReadPointer(juce::jlimit(0, sourceChannels - 1, channel));
                    return data[i1] + (data[i2] - data[i1]) * frac;
                };
                if (numOutputChannels > 0 && outputChannelData[0] != nullptr && sourceChannels > 0)
                    outputChannelData[0][outputOffset + s] += readLinear(0) * leftGain;
                if (numOutputChannels > 1 && outputChannelData[1] != nullptr && sourceChannels > 0)
                    outputChannelData[1][outputOffset + s] += readLinear(sourceChannels == 1 ? 0 : 1) * rightGain;
            }
            processTrackInsertChain();
            continue;
        }

        const int markerCount = juce::jlimit(2, maxWarpMarkers, rawMarkerCount);
        std::array<double, maxWarpMarkers> sourceMarkers {};
        std::array<double, maxWarpMarkers> targetMarkers {};
        for (int marker = 0; marker < markerCount; ++marker)
        {
            sourceMarkers[(size_t)marker] = warpSnapshot->source[(size_t)marker];
            targetMarkers[(size_t)marker] = warpSnapshot->target[(size_t)marker];
        }

        const int mode = juce::jlimit(0, 4, warpSnapshot->mode);
        const int lastSample = juce::jmax(0, audioBuffer->getNumSamples() - 1);

        auto readWarpedSample = [&](int channel, double samplePosition) -> float
        {
            const float* data = audioBuffer->getReadPointer(juce::jlimit(0, sourceChannels - 1, channel));
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

        int currentSegment = 0;
        for (int s = 0; s < samplesToMix; ++s)
        {
            const double targetSeconds = (double)(mixStart - startSample + s) / rate;

            while (currentSegment < markerCount - 2 && targetSeconds > targetMarkers[(size_t)(currentSegment + 1)])
                ++currentSegment;

            const double ta = targetMarkers[(size_t)currentSegment];
            const double tb = targetMarkers[(size_t)(currentSegment + 1)];
            const double sa = sourceMarkers[(size_t)currentSegment];
            const double sb = sourceMarkers[(size_t)(currentSegment + 1)];
            const double span = juce::jmax(0.000001, tb - ta);
            const double alpha = juce::jlimit(0.0, 1.0, (targetSeconds - ta) / span);
            const double sourceSeconds = sa + (sb - sa) * alpha;

            const double sourceRate = track.bufferSampleRate.load(std::memory_order_relaxed);
            const double sourceSamplePosition = sourceSeconds * sourceRate;
            if (numOutputChannels > 0 && outputChannelData[0] != nullptr && sourceChannels > 0)
                outputChannelData[0][outputOffset + s] += readWarpedSample(0, sourceSamplePosition) * leftGain;
            if (numOutputChannels > 1 && outputChannelData[1] != nullptr && sourceChannels > 0)
                outputChannelData[1][outputOffset + s] += readWarpedSample(sourceChannels == 1 ? 0 : 1, sourceSamplePosition) * rightGain;
        }

        processTrackInsertChain();
    }

    const bool midiLaneMuted = midiTrackMuted.load(std::memory_order_relaxed);
    const bool midiLaneSolo = midiTrackSolo.load(std::memory_order_relaxed);
    if (rate > 0.0)
    {
        for (int instrumentTrack=0; instrumentTrack<(int)instrumentPlayback.size(); ++instrumentTrack)
        {
            auto& state=*instrumentPlayback[(size_t)instrumentTrack];
            const bool trackMuted = midiLaneMuted || state.muted.load(std::memory_order_relaxed);
            const bool trackSolo = midiLaneSolo || state.solo.load(std::memory_order_relaxed);
            const auto nativeDrums = instrumentTrack == drumSamplerTrack.load(std::memory_order_acquire)
                ? std::atomic_load(&drumSampler) : std::shared_ptr<LibertyDrumSampler>{};
            const bool hasInstrument = nativeDrums != nullptr || pluginHost.hasInstrumentForTrack(instrumentTrack);
            if (panicInstruments && hasInstrument)
            {
                juce::MidiBuffer panicMidi;
                for (int channel = 1; channel <= 16; ++channel)
                {
                    panicMidi.addEvent(juce::MidiMessage::allNotesOff(channel), 0);
                    panicMidi.addEvent(juce::MidiMessage::allSoundOff(channel), 0);
                }
                juce::AudioBuffer<float> panicBuffer;
                pluginHost.renderInstrumentForTrack(instrumentTrack, panicBuffer, numSamples, panicMidi, 0.0f, 0.0f);
            }
            if (trackMuted || (anyPlaybackSolo && !trackSolo)) continue;
            // Arrangement notes already contain absolute timeline positions.
            // The independent Step Sequencer preview uses its own note snapshot
            // and duration, never a fabricated one-second arrangement length.
            const auto arrangementNotes = std::atomic_load(&state.arrangementSnapshot);
            const auto noteSnapshot = arrangementNotes != nullptr ? arrangementNotes : std::atomic_load(&state.noteSnapshot);
            const auto clipStart = arrangementNotes != nullptr ? 0.0 : state.clipStartSeconds.load(std::memory_order_relaxed);
            if (!hasInstrument || noteSnapshot == nullptr || noteSnapshot->notes.empty()) continue;
            if (arrangementNotes == nullptr && state.clipLengthSeconds.load(std::memory_order_relaxed) <= 0.0) continue;
            juce::MidiBuffer midi;
            const double blockStart=static_cast<double>(position)/rate;
            const double blockEnd=static_cast<double>(position+numSamples)/rate;
            for (const auto& note : noteSnapshot->notes)
            {
                const auto absoluteStart=clipStart+note.startSeconds;
                const auto absoluteEnd=clipStart+note.endSeconds;
                const auto frequency=note.frequency;
                const auto amplitude=note.amplitude;
                const int channel=juce::jlimit(1,16,note.channel);
                const int pitch=juce::jlimit(0,127,(int)std::llround(69.0+12.0*std::log2(juce::jmax(0.0001,frequency/440.0))));
                const float velocity=juce::jlimit(0.0f,1.0f,amplitude/0.045f);
                const bool noteStartsInBlock = absoluteStart >= blockStart && absoluteStart < blockEnd;
                const bool noteSpansResume = resumeInstruments && absoluteStart < blockStart && absoluteEnd > blockStart;
                if (noteSpansResume)
                    midi.addEvent(juce::MidiMessage::noteOn(channel, pitch, velocity), 0);
                else if (noteStartsInBlock)
                    midi.addEvent(juce::MidiMessage::noteOn(channel,pitch,velocity),juce::jlimit(0,numSamples-1,(int)std::llround((absoluteStart-blockStart)*rate)));
                if(absoluteEnd>=blockStart&&absoluteEnd<blockEnd)midi.addEvent(juce::MidiMessage::noteOff(channel,pitch),juce::jlimit(0,numSamples-1,(int)std::llround((absoluteEnd-blockStart)*rate)));
            }
            juce::AudioBuffer<float> instrumentBus;
            bool instrumentRendered = false;
            if (nativeDrums != nullptr)
            {
                instrumentBus.setSize(2, numSamples, false, true, true);
                instrumentBus.clear();
                if (panicInstruments) nativeDrums->reset();
                nativeDrums->renderMidi(instrumentBus, midi);
                const float gain = state.gain.load(std::memory_order_relaxed);
                const float pan = state.pan.load(std::memory_order_relaxed);
                instrumentBus.applyGain(0, 0, numSamples, gain * juce::jmin(1.0f, 1.0f - pan));
                instrumentBus.applyGain(1, 0, numSamples, gain * juce::jmin(1.0f, 1.0f + pan));
                instrumentRendered = true;
            }
            else
                instrumentRendered = pluginHost.renderInstrumentForTrack(instrumentTrack, instrumentBus, numSamples, midi,
                                                    state.gain.load(std::memory_order_relaxed),
                                                    state.pan.load(std::memory_order_relaxed));
            if (instrumentRendered)
            {
                for (int slot = 0; slot < LibertyPluginHost::effectSlotsPerTrack; ++slot)
                {
                    pluginHost.processInstrumentEffectSlot(instrumentTrack, slot, instrumentBus);
                    oneKnob.process(100000 + instrumentTrack * 8 + slot, instrumentBus);
                }
                const int channels = juce::jmin(2, numOutputChannels);
                for (int channel = 0; channel < channels; ++channel)
                    if (outputChannelData[channel] != nullptr)
                        juce::FloatVectorOperations::add(outputChannelData[channel], instrumentBus.getReadPointer(channel), numSamples);
            }
        }
    }

    if (metronomeEnabled.load(std::memory_order_relaxed) && rate > 0.0)
    {
        const double bpm = juce::jmax(1.0, metronomeBpm.load(std::memory_order_relaxed));
        const int numerator = juce::jmax(1, metronomeNumerator.load(std::memory_order_relaxed));
        const int denominator = juce::jmax(1, metronomeDenominator.load(std::memory_order_relaxed));
        const double secondsPerBeat = 60.0 / bpm * (4.0 / static_cast<double>(denominator));
        const auto beatSamples = juce::jmax<std::int64_t>(1, static_cast<std::int64_t>(std::llround(secondsPerBeat * rate)));
        const auto clickSamples = juce::jmax<std::int64_t>(1, static_cast<std::int64_t>(std::llround(0.035 * rate)));
        constexpr double twoPi = 6.28318530717958647692;
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const std::int64_t projectSample = position + sample;
            const std::int64_t beatIndex = projectSample / beatSamples;
            const std::int64_t offset = projectSample % beatSamples;
            if (offset >= clickSamples) continue;
            const bool accent = (beatIndex % numerator) == 0;
            const double t = static_cast<double>(offset) / rate;
            const double frequency = accent ? 1760.0 : 1200.0;
            const double decay = std::exp(-t * 95.0);
            const float value = static_cast<float>(std::sin(twoPi * frequency * t) * decay * (accent ? 0.34 : 0.22));
            if (numOutputChannels > 0 && outputChannelData[0] != nullptr) outputChannelData[0][sample] += value;
            if (numOutputChannels > 1 && outputChannelData[1] != nullptr) outputChannelData[1][sample] += value;
        }
    }

    const auto master = masterGain.load();
    bool unsafeOutput = !std::isfinite(master) || std::abs(master) > 4.0f;
    float peak = 0.0f;
    for (int channel = 0; channel < numOutputChannels; ++channel)
    {
        if (outputChannelData[channel] == nullptr) continue;
        juce::FloatVectorOperations::multiply(outputChannelData[channel], std::isfinite(master) ? juce::jlimit(0.0f, 4.0f, master) : 0.0f, numSamples);
        const auto magnitude = juce::FloatVectorOperations::findMinAndMax(outputChannelData[channel], numSamples);
        if (!std::isfinite(magnitude.getStart()) || !std::isfinite(magnitude.getEnd())) unsafeOutput = true;
        peak = juce::jmax(peak, std::abs(magnitude.getStart()), std::abs(magnitude.getEnd()));
    }
    // Last-resort realtime protection: a DAW must never be allowed to emit
    // runaway/non-finite output. Stop transport and mute the current block.
    if (unsafeOutput || peak > 8.0f)
    {
        for (int channel = 0; channel < numOutputChannels; ++channel)
            if (outputChannelData[channel] != nullptr) juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
        instrumentResumePending.store(false, std::memory_order_release);
        instrumentPanicPending.store(true, std::memory_order_release);
        playing.store(false, std::memory_order_relaxed);
        return;
    }
    if (!hasBoundedAudioProject)
    {
        transportSamples.fetch_add(numSamples);
        return;
    }
    const auto advance = juce::jmin<std::int64_t>(numSamples, juce::jmax<std::int64_t>(0, projectLength - position));
    if (advance > 0) transportSamples.fetch_add(advance);
    if (position + advance >= projectLength)
    {
        instrumentResumePending.store(false, std::memory_order_release);
        instrumentPanicPending.store(true, std::memory_order_release);
        playing.store(false, std::memory_order_release);
    }
}

void AudioEngine::audioDeviceStopped()
{
    instrumentResumePending.store(false, std::memory_order_release);
    instrumentPanicPending.store(true, std::memory_order_release);
    playing.store(false, std::memory_order_release);
}
