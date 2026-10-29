#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "MidiEngine.h"
#include "LibertyDrumSampler.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <map>
#include <vector>
#include <thread>

class AudioEngine final : private juce::AudioIODeviceCallback, private juce::MidiInputCallback
{
public:
    // Legacy visible-row count while the UI is migrated to dynamic tracks.
    static constexpr int maxAudioTracks = 4;
    static constexpr int initialAudioTracks = 4;
    static constexpr int maxWarpMarkers = 32;

    AudioEngine();
    ~AudioEngine() override;

    void setDrumSampler(std::shared_ptr<LibertyDrumSampler> sampler) noexcept
    {
        std::atomic_store(&drumSampler, std::move(sampler));
    }
    std::shared_ptr<LibertyDrumSampler> getDrumSampler() const noexcept
    {
        return std::atomic_load(&drumSampler);
    }
    void setDrumSamplerTrack(int track) noexcept { drumSamplerTrack.store(track, std::memory_order_release); }
    int getDrumSamplerTrack() const noexcept { return drumSamplerTrack.load(std::memory_order_acquire); }
    bool initialise();
    void shutdown();

    // Headless regression entry point: render the existing audio callback
    // without opening an audio device. Use on a stopped, dedicated engine.
    bool renderOfflineForTesting(juce::AudioBuffer<float>& destination,
                                 double renderSampleRate, int blockSamples);

    bool isInitialised() const noexcept { return initialised.load(std::memory_order_relaxed); }
    // Select a physical MIDI input independently of the audio output device.
    void selectMidiInput(const juce::String& identifier);
    juce::String getSelectedMidiInput() const { return selectedMidiInput; }
    void setInstrumentMidiInput(int track, const juce::String& identifier);
    juce::String getInstrumentMidiInput(int track) const;
    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    const juce::AudioDeviceManager& getDeviceManager() const noexcept { return deviceManager; }
    juce::String getDeviceName() const;
    double getSampleRate() const noexcept { return sampleRate.load(std::memory_order_relaxed); }
    int getBufferSize() const noexcept { return bufferSize.load(std::memory_order_relaxed); }
    int getOutputChannels() const noexcept { return outputChannels.load(std::memory_order_relaxed); }
    juce::String getLastError() const;

    void setPlaying(bool shouldPlay) noexcept
    {
        const auto rate = sampleRate.load(std::memory_order_relaxed);
        if (shouldPlay)
        {
            instrumentResumePending.store(true, std::memory_order_release);
            const auto current = rate > 0.0
                ? static_cast<double>(transportSamples.load(std::memory_order_relaxed)) / rate
                : 0.0;
            playbackClockBaseSeconds.store(current, std::memory_order_relaxed);
            playbackClockStartMilliseconds.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
        }
        else if (playing.load(std::memory_order_relaxed) && rate > 0.0)
        {
            const auto current = getCurrentTimeSeconds();
            transportSamples.store(static_cast<std::int64_t>(std::llround(current * rate)), std::memory_order_relaxed);
            playbackClockBaseSeconds.store(current, std::memory_order_relaxed);
        }
        playing.store(shouldPlay, std::memory_order_relaxed);
        if (!shouldPlay)
            instrumentPanicPending.store(true, std::memory_order_release);
    }
    bool isPlaying() const noexcept { return playing.load(std::memory_order_relaxed); }
    // Independent Step Sequencer audition state; never starts the arrangement transport.
    void setStepPreview(int instrumentTrack, bool enabled) noexcept
    {
        stepPreviewTrack.store(enabled ? instrumentTrack : -1, std::memory_order_release);
        stepPreviewSamples.store(0, std::memory_order_release);
        instrumentPanicPending.store(true, std::memory_order_release);
    }
    bool isStepPreviewPlaying() const noexcept { return stepPreviewTrack.load(std::memory_order_acquire) >= 0; }
    int getStepPreviewTrack() const noexcept { return stepPreviewTrack.load(std::memory_order_acquire); }

    void notifyInstrumentChanged() noexcept
    {
        instrumentPanicPending.store(true, std::memory_order_release);
        if (playing.load(std::memory_order_relaxed))
            instrumentResumePending.store(true, std::memory_order_release);
        else
            instrumentResumePending.store(false, std::memory_order_release);
    }
    void setInputMonitoring(bool enabled, int leftInput = 0, int rightInput = 1) noexcept
    {
        monitorInputLeft.store(juce::jmax(0, leftInput), std::memory_order_relaxed);
        monitorInputRight.store(juce::jmax(0, rightInput), std::memory_order_relaxed);
        inputMonitoringEnabled.store(enabled, std::memory_order_release);
    }
    bool isInputMonitoringEnabled() const noexcept { return inputMonitoringEnabled.load(std::memory_order_acquire); }
    void setMonitoredInstrumentTrack(int track) noexcept
    {
        if (track >= 0 && !ensureInstrumentPlaybackTracks(track + 1)) return;
        monitoredInstrumentTrack.store(track, std::memory_order_release);
        instrumentPanicPending.store(true, std::memory_order_release);
    }
    void sendPianoPreviewMidi(int track, const juce::MidiMessage& message)
    {
        if (track < 0 || track >= (int) instrumentPlayback.size()) return;
        pianoPreviewTrack.store(track, std::memory_order_release);
        pianoPreviewCollector.addMessageToQueue(message);
    }
    int getMonitoredInstrumentTrack() const noexcept { return monitoredInstrumentTrack.load(std::memory_order_acquire); }
    struct CapturedMidiEvent { juce::MidiMessage message; double seconds = 0.0; };
    void beginMidiCapture(int instrumentTrack) noexcept;
    std::vector<CapturedMidiEvent> endMidiCapture();
    int getMidiCaptureTrack() const noexcept { return midiCaptureTrack.load(std::memory_order_acquire); }

    void setMetronomeEnabled(bool enabled) noexcept { metronomeEnabled.store(enabled, std::memory_order_relaxed); }
    bool isMetronomeEnabled() const noexcept { return metronomeEnabled.load(std::memory_order_relaxed); }
    void setMetronomeTiming(double bpm, int numerator, int denominator) noexcept
    {
        metronomeBpm.store(juce::jmax(1.0, bpm), std::memory_order_relaxed);
        metronomeNumerator.store(juce::jmax(1, numerator), std::memory_order_relaxed);
        metronomeDenominator.store(juce::jmax(1, denominator), std::memory_order_relaxed);
    }
    void resetTransport() noexcept
    {
        transportSamples.store(0, std::memory_order_relaxed);
        playbackClockBaseSeconds.store(0.0, std::memory_order_relaxed);
        playbackClockStartMilliseconds.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
        instrumentPanicPending.store(true, std::memory_order_release);
    }
    void setCurrentTimeSeconds(double seconds) noexcept;
    double getCurrentTimeSeconds() const noexcept;

    void setProjectExtraLengthSeconds(double seconds) noexcept { projectExtraLengthSeconds.store(juce::jmax(0.0, seconds), std::memory_order_relaxed); }
    double getProjectExtraLengthSeconds() const noexcept { return projectExtraLengthSeconds.load(std::memory_order_relaxed); }
    // Keep transport running until the UI has wrapped an enabled loop.
    void setLoopTransportEndSeconds(double seconds) noexcept { loopTransportEndSeconds.store(juce::jmax(0.0, seconds), std::memory_order_relaxed); }

    void setMidiNotes(const std::vector<MidiEngine::NoteEvent>& notes,
                      double clipStartSeconds,
                      double clipLengthSeconds,
                      double tempoBpm) noexcept;
    void setInstrumentTrackControllers(int instrumentTrack, const std::vector<MidiEngine::ControllerEvent>& events,
                                       double tempoBpm) noexcept;
    void setInstrumentTrackNotes(int instrumentTrack, const std::vector<MidiEngine::NoteEvent>& notes,
                                 double clipStartSeconds, double clipLengthSeconds, double tempoBpm) noexcept;

    struct InstrumentArrangementClip
    {
        double startSeconds = 0.0;
        double lengthSeconds = 0.0;
        std::vector<MidiEngine::NoteEvent> notes;
    };
    void setInstrumentArrangementClips(int instrumentTrack,
                                       const std::vector<InstrumentArrangementClip>& clips,
                                       double tempoBpm) noexcept;

    int getAudioTrackCount() const noexcept;
    int addAudioTrack();
    bool removeAudioTrack(int trackIndex);
    bool ensureInstrumentPlaybackTracks(int trackCount) noexcept;
    bool resetInstrumentPlayback(int trackCount) noexcept;

    void setTrackGain(int trackIndex, float gain) noexcept;
    float getTrackGain(int trackIndex) const noexcept;
    void setTrackClipGain(int trackIndex, float gain) noexcept;
    float getTrackClipGain(int trackIndex) const noexcept;
    void setTrackClipPitch(int trackIndex, double semitones) noexcept;
    double getTrackClipPitch(int trackIndex) const noexcept;
    void setTrackClipReverse(int trackIndex, bool enabled) noexcept;
    bool isTrackClipReversed(int trackIndex) const noexcept;
    void setTrackPan(int trackIndex, float pan) noexcept;
    float getTrackPan(int trackIndex) const noexcept;
    void setTrackMuted(int trackIndex, bool muted) noexcept;
    bool isTrackMuted(int trackIndex) const noexcept;
    void setTrackSolo(int trackIndex, bool solo) noexcept;
    bool isTrackSolo(int trackIndex) const noexcept;
    bool isAnyTrackSolo() const noexcept;

    void setMidiTrackMuted(bool muted) noexcept { midiTrackMuted.store(muted, std::memory_order_relaxed); instrumentPanicPending.store(true, std::memory_order_release); instrumentResumePending.store(playing.load(std::memory_order_relaxed), std::memory_order_release); }
    bool isMidiTrackMuted() const noexcept { return midiTrackMuted.load(std::memory_order_relaxed); }
    void setMidiTrackSolo(bool solo) noexcept { midiTrackSolo.store(solo, std::memory_order_relaxed); instrumentPanicPending.store(true, std::memory_order_release); instrumentResumePending.store(playing.load(std::memory_order_relaxed), std::memory_order_release); }
    bool isMidiTrackSolo() const noexcept { return midiTrackSolo.load(std::memory_order_relaxed); }
    void setInstrumentTrackGain(int instrumentTrack, float gain) noexcept;
    float getInstrumentTrackGain(int instrumentTrack) const noexcept;
    void setInstrumentTrackPan(int instrumentTrack, float pan) noexcept;
    float getInstrumentTrackPan(int instrumentTrack) const noexcept;
    void setInstrumentTrackMuted(int instrumentTrack, bool muted) noexcept;
    bool isInstrumentTrackMuted(int instrumentTrack) const noexcept;
    void setInstrumentTrackSolo(int instrumentTrack, bool solo) noexcept;
    bool isInstrumentTrackSolo(int instrumentTrack) const noexcept;
    void setInstrumentTrackMuted(bool muted) noexcept { setInstrumentTrackMuted(0, muted); }
    bool isInstrumentTrackMuted() const noexcept { return isInstrumentTrackMuted(0); }
    void setInstrumentTrackSolo(bool solo) noexcept { setInstrumentTrackSolo(0, solo); }
    bool isInstrumentTrackSolo() const noexcept { return isInstrumentTrackSolo(0); }

    struct AudioBufferSnapshot
    {
        bool loaded = false;
        bool valid = true;
        std::shared_ptr<const juce::AudioBuffer<float>> buffer;
        std::int64_t numSamples = 0;
        double lengthSeconds = 0.0;
        double sampleRate = 0.0;

        double getSampleRate() const noexcept { return sampleRate; }
    };

    AudioBufferSnapshot getAudioTrackSnapshot(int trackIndex) const noexcept;

    struct WarpSnapshot
    {
        bool enabled = false;
        int mode = 0;
        int count = 0;
        std::array<double, maxWarpMarkers> source {};
        std::array<double, maxWarpMarkers> target {};
    };

    WarpSnapshot getTrackWarpSnapshot(int trackIndex) const noexcept;

    bool loadAudioFileIntoTrack(int trackIndex, const juce::File& file, juce::String& error);
    void clearAudioTrack(int trackIndex);
    bool splitAudioTrack(int trackIndex, double splitProjectSeconds, int targetTrackIndex, juce::String& error);
    bool hasAudioFile(int trackIndex) const noexcept;
    juce::String getAudioFileName(int trackIndex) const;
    // Clip metadata model: supports multiple independently positioned clips
    // without changing the existing single-buffer playback path.
    struct AudioClipMetadata
    {
        juce::String id;
        juce::String sourceName;
        juce::String sourcePath;
        double startSeconds = 0.0;
        double lengthSeconds = 0.0;
        double trimStartFraction = 0.0;
        double trimEndFraction = 1.0;
        // Non-destructive clip controls for the dock audio editor.
        float clipGain = 1.0f;
        double pitchSemitones = 0.0;
        bool reverse = false;
        bool muted = false;
        double offsetSeconds = 0.0;
        double envelopeLengthSeconds = 0.0;
        WarpSnapshot warp;
        double formant = 0.0;
        std::shared_ptr<juce::AudioBuffer<float>> spectralCache;
        double fadeInSeconds = 0.0;
        double fadeOutSeconds = 0.0;
        // Derived from neighbouring clips; never stored in project XML.
        // Use absolute timeline seconds so split clips retain their own offsets.
        double autoCrossfadeInStart = -1.0, autoCrossfadeInEnd = -1.0;
        double autoCrossfadeOutStart = -1.0, autoCrossfadeOutEnd = -1.0;
        // Decoded source prepared outside the real-time audio callback.
        std::shared_ptr<const juce::AudioBuffer<float>> sourceBuffer;
        double sourceSampleRate = 0.0;
    };
    static bool validAudioClip(const AudioClipMetadata&);
    static void writeAudioClipProperties(juce::XmlElement&, const AudioClipMetadata&);
    static AudioClipMetadata readAudioClipProperties(const juce::XmlElement&);
    std::vector<AudioClipMetadata> getAudioClips(int trackIndex) const;
    bool addAudioClipMetadata(int trackIndex, const AudioClipMetadata& clip);
    bool replaceAudioClips(int trackIndex, const std::vector<AudioClipMetadata>& clips);
    std::vector<AudioClipMetadata> getArrangementAudioClips(int trackIndex) const;
    bool replaceArrangementAudioClips(int trackIndex,const std::vector<AudioClipMetadata>& clips);
    bool splitArrangementAudioClip(int trackIndex,const juce::String& id,double seconds);
    bool promoteAudioClip(int trackIndex,const juce::String& id);
    bool stretchArrangementAudioClip(int trackIndex,const juce::String& id,double seconds);
    std::shared_ptr<juce::AudioBuffer<float>> renderArrangementAudioClip(const AudioClipMetadata& clip);
    bool loadAudioClipSource(int trackIndex, const juce::String& clipId,
                             const juce::File& sourceFile, juce::String& error);
    void clearAudioClipMetadata(int trackIndex);
    juce::String getAudioClipId(int trackIndex) const;
    void setAudioClipId(int trackIndex, const juce::String& id);
    void setAudioFileName(int trackIndex, const juce::String& name);
    double getAudioFileLengthSeconds(int trackIndex) const noexcept;
    void setAudioFileLengthSeconds(int trackIndex, double seconds) noexcept;
    double getTrackStartSeconds(int trackIndex) const noexcept;
    void setTrackStartSeconds(int trackIndex, double seconds) noexcept;
    std::shared_ptr<const juce::AudioBuffer<float>> getAudioBufferSnapshot(int trackIndex) const noexcept;
    double getAudioBufferSampleRate(int trackIndex) const noexcept;
    std::uint64_t getAudioContentRevision(int trackIndex) const noexcept;

    void setTrackWarpEnabled(int trackIndex, bool enabled) noexcept;
    bool isTrackWarpEnabled(int trackIndex) const noexcept;
    void setTrackWarpMode(int trackIndex, int mode) noexcept;
    int getTrackWarpMode(int trackIndex) const noexcept;
    static juce::AudioBuffer<float> renderSpectralWarpOffline(const juce::AudioBuffer<float>& input,
        double durationRatio, double pitchSemitones, double formantSemitones);
    void setTrackWarpFormant(int trackIndex, double semitones) noexcept;
    double getTrackWarpFormant(int trackIndex) const noexcept;
    void resetTrackWarpMarkers(int trackIndex) noexcept;
    bool restoreTrackWarpState(int trackIndex, bool enabled, int mode, const std::vector<std::pair<double, double>>& markers) noexcept;
    bool addTrackWarpMarker(int trackIndex, double sourceSeconds, double targetSeconds) noexcept;
    bool moveTrackWarpMarker(int trackIndex, int markerIndex, double targetSeconds) noexcept;
    bool removeTrackWarpMarker(int trackIndex, int markerIndex) noexcept;
    int getTrackWarpMarkerCount(int trackIndex) const noexcept;
    double getTrackWarpMarkerSourceSeconds(int trackIndex, int markerIndex) const noexcept;
    double getTrackWarpMarkerTargetSeconds(int trackIndex, int markerIndex) const noexcept;

    void setMasterGain(float gain) noexcept { masterGain.store(juce::jlimit(0.0f, 2.0f, gain), std::memory_order_relaxed); }
    float getMasterGain() const noexcept { return masterGain.load(std::memory_order_relaxed); }

    void setTrackGain(float gain) noexcept { setTrackGain(0, gain); }
    float getTrackGain() const noexcept { return getTrackGain(0); }
    void setTrackPan(float pan) noexcept { setTrackPan(0, pan); }
    float getTrackPan() const noexcept { return getTrackPan(0); }
    void setTrackMuted(bool muted) noexcept { setTrackMuted(0, muted); }
    bool isTrackMuted() const noexcept { return isTrackMuted(0); }
    bool loadAudioFile(const juce::File& file, juce::String& error) { return loadAudioFileIntoTrack(0, file, error); }
    void clearAudioFile() { clearAudioTrack(0); }
    bool hasAudioFile() const noexcept { return hasAudioFile(0); }
    juce::String getAudioFileName() const { return getAudioFileName(0); }
    double getAudioFileLengthSeconds() const noexcept { return getAudioFileLengthSeconds(0); }

private:
    struct WarpMarkerSnapshot
    {
        bool enabled = false;
        int mode = 0;
        int count = 0;
        std::array<double, maxWarpMarkers> source {};
        std::array<double, maxWarpMarkers> target {};
    };

    struct AudioTrackState
    {
        std::atomic<bool> clipMuted { false };
        std::atomic<double> clipOffset { 0.0 }, clipEnvelopeLength { 0.0 };
        std::atomic<double> clipFadeIn { 0.0 }, clipFadeOut { 0.0 };
        std::atomic<double> autoCrossfadeInStart { -1.0 }, autoCrossfadeInEnd { -1.0 };
        std::atomic<double> autoCrossfadeOutStart { -1.0 }, autoCrossfadeOutEnd { -1.0 };
        std::atomic<double> clipTrimStart { 0.0 }, clipTrimEnd { 1.0 };
        std::shared_ptr<const std::vector<std::shared_ptr<AudioTrackState>>> regionVoices;
        std::atomic<float> gain { 1.0f };
        std::atomic<float> clipGain { 1.0f };
        std::atomic<double> clipPitch { 0.0 };
        std::atomic<bool> clipReverse { false };
        std::atomic<float> pan { 0.0f };
        std::atomic<bool> muted { false };
        std::atomic<bool> solo { false };
        std::atomic<bool> loaded { false };
        std::atomic<double> lengthSeconds { 0.0 };
        std::atomic<double> bufferSampleRate { 0.0 };
        std::atomic<double> startSeconds { 0.0 };
        std::atomic<bool> warpEnabled { false };
        std::atomic<int> warpMode { 0 };
        std::atomic<double> warpFormant { 0.0 };
        std::atomic<int> warpMarkerCount { 0 };
        std::array<std::atomic<double>, maxWarpMarkers> warpSourceSeconds {};
        std::array<std::atomic<double>, maxWarpMarkers> warpTargetSeconds {};
        std::shared_ptr<WarpMarkerSnapshot> warpMarkerSnapshot;
        std::shared_ptr<juce::AudioBuffer<float>> buffer;
        std::shared_ptr<juce::AudioBuffer<float>> spectralCache;
        std::atomic<std::uint64_t> spectralGeneration { 0 };
        std::atomic<std::uint64_t> spectralReadyGeneration { 0 };
        // Callback-owned crossfade state.
        float spectralBlend = 0.0f;
        std::atomic<std::uint64_t> contentRevision { 0 };
        // Audio-callback-owned Riche envelope; never accessed by the UI thread.
        float richeTransientEnvelope = 0.0f;
        bool richeTransientPrimed = false;
        std::int64_t richeNextSample = -1;
        std::uint64_t richeContentRevision = 0;
        juce::String fileName;
        juce::String clipId;
        std::vector<AudioClipMetadata> clipMetadata;
        std::shared_ptr<const std::vector<AudioClipMetadata>> playbackClips;
    };

    static std::shared_ptr<AudioTrackState> prepareAudioVoice(const AudioClipMetadata&);
    void mixAudioVoice(AudioTrackState&,float,float,double,std::int64_t,float* const*,int,int);

    static constexpr std::size_t maxMidiPlaybackNotes = 256;
    struct MidiPlaybackNote
    {
        std::atomic<double> startSeconds { 0.0 };
        std::atomic<double> endSeconds { 0.0 };
        std::atomic<double> frequency { 440.0 };
        std::atomic<float> amplitude { 0.0f };
        std::atomic<int> channel { 1 };
    };
    struct InstrumentPlaybackNote
    {
        double startSeconds = 0.0;
        double endSeconds = 0.0;
        double frequency = 440.0;
        float amplitude = 0.0f;
        int channel = 1;
        int probability = 100;
    };
    struct InstrumentNoteSnapshot
    {
        std::vector<InstrumentPlaybackNote> notes;
    };
    struct InstrumentControllerPlayback
    {
        double seconds = 0.0;
        int channel = 1;
        int controller = 1;
        int value = 0;
    };
    struct InstrumentPlaybackState
    {
        std::shared_ptr<InstrumentNoteSnapshot> noteSnapshot;
        std::shared_ptr<std::vector<InstrumentControllerPlayback>> controllerSnapshot;
        std::shared_ptr<InstrumentNoteSnapshot> arrangementSnapshot;
        std::atomic<double> clipStartSeconds { 0.0 };
        std::atomic<double> clipLengthSeconds { 0.0 };
        std::atomic<double> tempoBpm { 120.0 };
        std::atomic<float> gain { 1.0f };
        std::atomic<float> pan { 0.0f };
        std::atomic<bool> muted { false };
        std::atomic<bool> solo { false };
    };

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) override;
    juce::MidiMessageCollector liveMidiCollector;
    // Piano-roll keyboard audition is independent of external MIDI monitoring.
    juce::MidiMessageCollector pianoPreviewCollector;
    std::atomic<int> pianoPreviewTrack { -1 };

    juce::String selectedMidiInput;
    mutable juce::CriticalSection instrumentMidiInputLock;
    std::map<int, juce::String> instrumentMidiInputs;
    juce::CriticalSection midiCaptureLock;
    std::vector<CapturedMidiEvent> capturedMidiEvents;
    std::atomic<int> midiCaptureTrack { -1 };
    double midiCaptureStartMs = 0.0;

    std::atomic<int> monitoredInstrumentTrack { -1 };
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    bool isValidTrackIndex(int trackIndex) const noexcept { return trackIndex >= 0 && trackIndex < (int) tracks.size(); }
    std::int64_t getProjectLengthSamples() const noexcept;

    std::shared_ptr<LibertyDrumSampler> drumSampler;
    std::atomic<int> drumSamplerTrack { -1 };
    juce::AudioDeviceManager deviceManager;
    std::vector<std::unique_ptr<AudioTrackState>> tracks;
    std::atomic<bool> spectralWorkerStop { false };
    std::thread spectralWorker;
    void runSpectralWorker();
    std::shared_ptr<juce::AudioBuffer<float>> diagnosticPublishedBuffer;
    std::array<MidiPlaybackNote, maxMidiPlaybackNotes> midiPlaybackNotes;
    std::vector<std::unique_ptr<InstrumentPlaybackState>> instrumentPlayback;
    std::atomic<std::size_t> midiPlaybackNoteCount { 0 };
    std::atomic<double> midiClipStartSeconds { 0.0 };
    std::atomic<double> midiClipLengthSeconds { 0.0 };
    std::atomic<double> midiTempoBpm { 120.0 };
    std::atomic<bool> midiTrackMuted { false };
    std::atomic<bool> midiTrackSolo { false };
    std::atomic<bool> initialised { false };
    std::atomic<bool> playing { false };
    std::atomic<int> stepPreviewTrack { -1 };
    std::atomic<std::int64_t> stepPreviewSamples { 0 };
    std::atomic<bool> instrumentPanicPending { false };
    std::atomic<bool> instrumentResumePending { false };
    std::atomic<double> sampleRate { 0.0 };
    std::atomic<int> bufferSize { 0 };
    std::atomic<int> outputChannels { 0 };
    std::atomic<std::int64_t> transportSamples { 0 };
    std::atomic<double> loopTransportEndSeconds { 0.0 };
    std::atomic<double> projectExtraLengthSeconds { 0.0 };
    std::atomic<double> playbackClockBaseSeconds { 0.0 };
    std::atomic<double> playbackClockStartMilliseconds { 0.0 };
    std::atomic<float> masterGain { 1.0f };
    std::atomic<bool> inputMonitoringEnabled { false };
    std::atomic<int> monitorInputLeft { 0 };
    std::atomic<int> monitorInputRight { 1 };
    std::atomic<bool> metronomeEnabled { false };
    std::atomic<double> metronomeBpm { 120.0 };
    std::atomic<int> metronomeNumerator { 4 };
    std::atomic<int> metronomeDenominator { 4 };
    mutable juce::CriticalSection stateLock;
    juce::String deviceName;
    juce::String lastError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};