#pragma once
#include "MixConsoleMeter.h"
#include "LibertyImmersiveAnalysis.h"
#include "MixerBusRouting.h"
#include "LibertyCpuMeter.h"
#include "LibertyOfflinePool.h"
#include "LibertyRealtimePlan.h"
#include "LibertyDelayCompensation.h"

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
#include <set>
#include <vector>
#include <thread>

class AudioEngine final : private juce::AudioIODeviceCallback, private juce::MidiInputCallback
{
public:
    // Legacy visible-row count while the UI is migrated to dynamic tracks.
    static constexpr int maxAudioTracks = 4;
    static constexpr int initialAudioTracks = 4;
    static constexpr int maxWarpMarkers = 32;
    static constexpr int maxMixerBuses = LibertyMixerBusRouting::maxBuses;

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
    // Experimental hardware PDC: opt in before opening/restarting the device.
    // No UI or default behaviour change. Bounded dynamic plan; still experimental.
    bool requestExperimentalHardwarePdc(bool enabled) noexcept {
        if(isInitialised()||isAudioDeviceRunning())return false;
        hardwarePdcRequested.store(enabled,std::memory_order_release);return true;
    }
    bool isExperimentalHardwarePdcActive() const noexcept {return hardwarePdcPublished.load(std::memory_order_acquire);}
    int getExperimentalHardwarePdcLatency() const noexcept {return hardwarePdcReportedLatency.load(std::memory_order_relaxed);}
    std::uint64_t getExperimentalHardwarePdcRevisions() const noexcept {return hardwarePdcRevisions.load(std::memory_order_relaxed);}
    std::uint64_t getExperimentalHardwarePdcFallbacks() const noexcept {return hardwarePdcFallbacks.load(std::memory_order_relaxed);}
    void shutdown();

    // Headless regression entry point: render the existing audio callback
    // without opening an audio device. Use on a stopped, dedicated engine.
    // The optional deterministic mode excludes asynchronous spectral cache
    // transitions when checking sample-by-sample parity between clip paths.
    bool renderOfflineForTesting(juce::AudioBuffer<float>& destination,
                                 double renderSampleRate, int blockSamples,
                                 bool allowSpectralCache = true,
                                 void (*beforeBlock)(AudioEngine&,int,void*)=nullptr,void* context=nullptr);
    // Dedicated stopped offline engines only; never enabled by hardware.
    bool configureOfflineMulticoreForTesting(unsigned workers,bool adaptive=false);
    std::uint64_t getParallelBlocksForTesting() const noexcept {return parallelBlocks;}
    std::uint64_t getWorkerJobsForTesting() const noexcept {return parallelWorkerJobs;}
    std::uint64_t getParallelFallbacksForTesting() const noexcept {return parallelFallbacks;}
    std::uint64_t getAdaptiveSerialBlocksForTesting() const noexcept {return adaptiveSerialBlocks;}
    unsigned getSelectedWorkersForTesting() const noexcept {return selectedOfflineWorkers;}
    // Stopped dedicated engine only; snapshots hosted latencies outside callback.
    bool buildRealtimePlanForTesting(LibertyRealtime::Plan&) const;
    // Experimental offline PDC. Test latencies model delayed audio fixtures,
    // never override real plugin reports in the hardware callback.
    bool configureOfflinePdcForTesting(bool enabled,bool bypass=false,const std::vector<int>& simulatedAudioLatencies={});
    bool configureDynamicOfflinePdcForTesting(int capacity,int transitionFrames=128);
    std::uint64_t getDynamicPdcUpdatesForTesting() const noexcept {return dynamicPdcUpdates;}
    int getOfflinePdcLatencyForTesting() const noexcept {return offlinePdcLatency;}
    std::uint64_t getOfflinePdcBlocksForTesting() const noexcept {return offlinePdcBlocks;}
    // Headless diagnostic only: confirms that the worker published an FFT
    // result for this track's current generation (never used in audio callback).
    bool isSpectralCacheReadyForTesting(int trackIndex);
    // Monotonic diagnostic count; no audio-thread reads or locks.
    std::uint64_t getSpectralFftStartsForTesting() const noexcept
    {
        return spectralFftStarts.load(std::memory_order_acquire);
    }
    // Duration of the most recent spectral-track mutex acquisition; diagnostics
    // only. Includes OS scheduling around the lock, so not a pure wait metric.
    std::int64_t getLastSpectralAddLockAcquisitionNsForTesting() const noexcept
    {
        return spectralAddLockAcquisitionNs.load(std::memory_order_relaxed);
    }
    std::int64_t getLastSpectralRemoveLockAcquisitionNsForTesting() const noexcept
    {
        return spectralRemoveLockAcquisitionNs.load(std::memory_order_relaxed);
    }

    // Wall time relative to block deadline, not operating-system CPU or hardware xruns.
    static constexpr int cpuProfileTrackCapacity=128;
    void setCpuProfiling(bool enabled,bool perTrack=false) noexcept {cpuProfiling.store(enabled,std::memory_order_relaxed);cpuTrackProfiling.store(perTrack,std::memory_order_relaxed);}
    LibertyCpu::Snapshot getCpuPerformance() const noexcept {return cpuTotal.snapshot();}
    LibertyCpu::Snapshot getTrackCpuPerformance(int index,bool instrument) const noexcept {return index>=0&&index<cpuProfileTrackCapacity?(instrument?cpuInstrument:cpuAudio)[(size_t)index].snapshot():LibertyCpu::Snapshot{};}
    // Only on a quiescent engine (no hardware callback).
    void resetCpuPerformanceWhileStopped() noexcept {cpuTotal.reset();for(auto& x:cpuAudio)x.reset();for(auto& x:cpuInstrument)x.reset();}
    bool isInitialised() const noexcept { return initialised.load(std::memory_order_relaxed); }
    // Select a physical MIDI input independently of the audio output device.
    void selectMidiInput(const juce::String& identifier);
    juce::String getSelectedMidiInput() const { return selectedMidiInput; }
    // Lock-free snapshot of the last physical CC or note-on for PERFORMANCE
    // MIDI Learn. The serial number distinguishes repeated identical messages.
    std::uint64_t getPerformMidiLearnEvent() const noexcept
    {return performMidiLearnEvent.load(std::memory_order_acquire);}
    // Dedicated CC mailbox per channel/controller. The accumulated signed
    // relative movement survives GUI polling intervals; no event queue allocation.
    static std::uint64_t advanceGroupMacroCCWord(std::uint64_t old,int value,int format=0) noexcept;
    std::uint64_t getGroupMacroCCWord(int channel,int cc,int format=0) const noexcept {
        return channel>=1&&channel<=16&&cc>=0&&cc<=127
            ?(format==1?groupMacroSignCCWords:groupMacroCCWords)[(size_t)((channel-1)*128+cc)].load(std::memory_order_acquire):0;
    }
    std::uint64_t getGroupMacroLearnEvent() const noexcept {
        return groupMacroLearnEvent.load(std::memory_order_acquire);
    }
    void setInstrumentMidiInput(int track, const juce::String& identifier);
    juce::String getInstrumentMidiInput(int track) const;
    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    const juce::AudioDeviceManager& getDeviceManager() const noexcept { return deviceManager; }
    juce::String getDeviceName() const;
    double getSampleRate() const noexcept { return sampleRate.load(std::memory_order_relaxed); }
    int getBufferSize() const noexcept { return bufferSize.load(std::memory_order_relaxed); }
    // Driver-reported latency, not a physical loopback measurement.
    bool isAudioDeviceRunning() const noexcept { return deviceRunning.load(std::memory_order_acquire); }
    int getInputLatencySamples() const noexcept { return inputLatencySamples.load(std::memory_order_relaxed); }
    int getOutputLatencySamples() const noexcept { return outputLatencySamples.load(std::memory_order_relaxed); }
    std::uint64_t getDeviceStartCount() const noexcept { return deviceStarts.load(std::memory_order_relaxed); }
    std::uint64_t getDeviceStopCount() const noexcept { return deviceStops.load(std::memory_order_relaxed); }
    int getPreparedScratchFrames() const noexcept { return preparedScratchFrames.load(std::memory_order_relaxed); }
    std::uint64_t getOversizedCallbackCount() const noexcept { return oversizedCallbacks.load(std::memory_order_relaxed); }
    int getOutputChannels() const noexcept { return outputChannels.load(std::memory_order_relaxed); }
    juce::String getLastError() const;

    void setPlaying(bool shouldPlay) noexcept
    {
        const auto rate = sampleRate.load(std::memory_order_relaxed);
        if (shouldPlay)
        {
            // Starting ARRANGE takes over from the independent Step Sequencer audition.
            if (stepPreviewTrack.load(std::memory_order_acquire) >= 0)
                setStepPreview(-1, false);
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
    double getStepPreviewPositionSeconds() const noexcept
    {
        const auto rate = sampleRate.load(std::memory_order_relaxed);
        return rate > 0.0 ? (double)stepPreviewSamples.load(std::memory_order_relaxed) / rate : 0.0;
    }
    bool hasNativeDrumSamplerForTrack(int track) const noexcept
    {
        return track >= 0 && track == drumSamplerTrack.load(std::memory_order_acquire)
            && std::atomic_load(&drumSampler) != nullptr;
    }

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
                                 double clipStartSeconds, double clipLengthSeconds, double tempoBpm,
                                 bool repeatStepPattern = false) noexcept;

    struct InstrumentArrangementClip
    {
        double startSeconds = 0.0;
        double lengthSeconds = 0.0;
        std::vector<MidiEngine::NoteEvent> notes;
        int arrangementClipId = 0; // zero for legacy/unassigned sources
    };
    void setInstrumentArrangementClips(int instrumentTrack,
                                       const std::vector<InstrumentArrangementClip>& clips,
                                       double tempoBpm) noexcept;

    // Temporary arrangement audition, never serialized or applied to mixer controls.
    struct GroupAuditionMask {
        std::set<juce::String> audioIds;
        std::set<int> instrumentIds;
    };
    void setGroupAudition(std::shared_ptr<const GroupAuditionMask>) noexcept;
    std::shared_ptr<const GroupAuditionMask> getGroupAudition() const noexcept {
        return std::atomic_load(&groupAudition);
    }

    struct GroupMacroPreviewValue {
        bool gain=false,pan=false;
        float gainValue=1.f,panValue=0.f;
    };
    using GroupMacroPreview=std::map<juce::String,GroupMacroPreviewValue>;
    void setGroupMacroPreview(std::shared_ptr<const GroupMacroPreview> preview) noexcept {
        std::atomic_store(&groupMacroPreview,std::move(preview));
    }
    int getAudioTrackCount() const noexcept;
    int addAudioTrack();
    bool removeAudioTrack(int trackIndex);
    bool ensureInstrumentPlaybackTracks(int trackCount) noexcept;
    bool resetInstrumentPlayback(int trackCount) noexcept;

    void setTrackGain(int trackIndex, float gain) noexcept;
    void setChannelInputGainDb(int trackIndex, float db) noexcept;
    void setChannelHighPassHz(int trackIndex, float hz) noexcept;
    void setChannelCompressor(int trackIndex, bool enabled, float thresholdDb, float ratio) noexcept;
    void setChannelGate(int trackIndex, bool enabled, float thresholdDb) noexcept;
    void setChannelEq(int trackIndex, bool enabled, float lowDb, float midDb, float highDb) noexcept;
    float getChannelEqBandDb(int trackIndex, int band) const noexcept;
    struct ChannelProcessingSettings
    {
        float inputGainDb = 0.0f, highPassHz = 0.0f;
        bool compressorEnabled = false, gateEnabled = false, eqEnabled = false;
        float compressorThresholdDb = -18.0f, compressorRatio = 4.0f, gateThresholdDb = -55.0f;
        float eqLowDb = 0.0f, eqMidDb = 0.0f, eqHighDb = 0.0f;
    };
    ChannelProcessingSettings getChannelProcessingSettings(int trackIndex) const noexcept;
    void setChannelProcessingSettings(int trackIndex, const ChannelProcessingSettings& settings) noexcept;
    // GUI and project A/B memory, not part of the real-time DSP state.
    struct ChannelABState {
        std::array<ChannelProcessingSettings,2> variants{};
        int active=0;
        bool saved=false;
    };
    ChannelABState getChannelABState(int trackIndex) const noexcept;
    void setChannelABState(int trackIndex,const ChannelABState& state) noexcept;
    void clearChannelABState(int trackIndex) noexcept;
    float getTrackGain(int trackIndex) const noexcept;
    // Sample peaks captured after the channel's insert chain.
    // Consuming exchanges the accumulator with zero; no audio-thread locks.
    float consumeTrackMeterPeak(int trackIndex) noexcept;
    float consumeInstrumentMeterPeak(int instrumentTrack) noexcept;
    float consumeMasterMeterPeak() noexcept;
    LibertyImmersive::Tap& immersiveTap() noexcept { return masterAnalysis; }
    // Post-FX sample peaks separated by channel, always {left, right}.
    // The older mono methods remain as compatibility wrappers.
    std::array<float,2> consumeTrackStereoMeterPeaks(int trackIndex) noexcept;
    std::array<float,2> consumeInstrumentStereoMeterPeaks(int instrumentTrack) noexcept;
    std::array<float,2> consumeMasterStereoMeterPeaks() noexcept;
    // Sample-weighted RMS for the latest console refresh interval (L/R).
    std::array<float,2> consumeTrackStereoMeterRms(int trackIndex) noexcept;
    std::array<float,2> consumeInstrumentStereoMeterRms(int instrumentTrack) noexcept;
    std::array<float,2> consumeMasterStereoMeterRms() noexcept;
    // Audio/instruments can feed one BUS or Master, never another BUS.
    int getMixerBusCount() const noexcept { return mixerBusCount.load(std::memory_order_acquire); }
    int addMixerBus() noexcept;
    void resetMixerBuses() noexcept;
    void setTrackMixerBus(int track,int bus) noexcept;
    int getTrackMixerBus(int track) const noexcept;
    void setInstrumentMixerBus(int track,int bus) noexcept;
    int getInstrumentMixerBus(int track) const noexcept;
    void setMixerBusGain(int bus,float value) noexcept;
    float getMixerBusGain(int bus) const noexcept;
    void setMixerBusPan(int bus,float value) noexcept;
    float getMixerBusPan(int bus) const noexcept;
    void setMixerBusMuted(int bus,bool muted) noexcept;
    bool isMixerBusMuted(int bus) const noexcept;
    void setMixerBusSolo(int bus,bool solo) noexcept;
    bool isMixerBusSolo(int bus) const noexcept;
    std::array<float,2> consumeMixerBusPeaks(int bus) noexcept;
    std::array<float,2> consumeMixerBusRms(int bus) noexcept;
    LibertyMixConsoleMeter::StereoCorrelationReading consumeMixerBusCorrelation(int bus) noexcept;
    LibertyMixConsoleMeter::StereoCorrelationReading consumeTrackStereoCorrelation(int trackIndex) noexcept;
    LibertyMixConsoleMeter::StereoCorrelationReading consumeInstrumentStereoCorrelation(int instrumentTrack) noexcept;
    LibertyMixConsoleMeter::StereoCorrelationReading consumeMasterStereoCorrelation() noexcept;
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
    // Time is normalized to the visible clip duration. No curve = legacy playback.
    struct ClipAutomationPoint {
        double time=0.;
        float value=0.f;
        int shape=0; // outgoing: linear, quadratic (legacy), S, exponential, logarithmic, cubic Bezier
        double domainStart=0.,domainEnd=1.; // retained subsegment of the original shape
        double handle1=1./3.,handle2=2./3.;
    };
    struct ClipAutomationCurve {
        bool relative=true;
        std::vector<ClipAutomationPoint> points;
    };
    using ClipAutomationPtr=std::shared_ptr<const ClipAutomationCurve>;
    static constexpr int maxClipAutomationPoints=128;
    static bool validClipAutomation(const ClipAutomationCurve&,bool pan) noexcept;
    static float evaluateClipAutomation(const ClipAutomationCurve&,double time) noexcept;
    static bool cropClipAutomation(ClipAutomationPtr& curve,double from,double to);
    static void normalizeBezierSegment(ClipAutomationPoint&) noexcept;
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
        float clipPan = 0.0f; // independent stereo balance, -1 left / +1 right
        ClipAutomationPtr gainAutomation,panAutomation;
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
        // Manual fade gain laws: 0=linear, 1=exponential, 2=smooth S.
        int fadeInCurve = 0;
        int fadeOutCurve = 0;
        // Persistent preference for the next automatic overlap to the left.
        // 0=linear, 1=equal-power, 2=smooth S.
        int crossfadeCurve = 0;
        // Zero uses the complete overlap; positive values narrow the transition.
        double crossfadeDurationSeconds = 0.0;
        double crossfadeCenter = 0.5;
        int crossfadeOutCurve = -1; // -1 links to incoming curve (legacy)
        bool crossfadeEnergy = false;
        // Derived from neighbouring clips; never stored in project XML.
        // Use absolute timeline seconds so split clips retain their own offsets.
        double autoCrossfadeInStart = -1.0, autoCrossfadeInEnd = -1.0;
        double autoCrossfadeOutStart = -1.0, autoCrossfadeOutEnd = -1.0;
        double autoCrossfadeOverlapStart = -1.0, autoCrossfadeOverlapEnd = -1.0;
        int autoCrossfadeCurve = 0;
        int autoCrossfadeInCurve = -1, autoCrossfadeOutCurve = -1;
        int autoCrossfadeInPeer = -1, autoCrossfadeOutPeer = -1;
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
    // Prepare a replacement without publishing it. Original buffers remain owned by Undo.
    bool prepareStretchedArrangementClip(const AudioClipMetadata& clip,double seconds,AudioClipMetadata& result,double pitchShift=0.0,bool preserveFormants=false);
    // Build a non-destructive map from raw-source transients. Reject ambiguous source geometry.
    bool prepareQuantizedArrangementClip(const AudioClipMetadata& clip,double grid,double strength,AudioClipMetadata& result);
    static bool canRenderRawStretch(const AudioClipMetadata& clip) noexcept;
    static bool prepareRawStretchedArrangementClip(const AudioClipMetadata& clip,double seconds,AudioClipMetadata& result);
    static bool prepareTonalArrangementClip(const AudioClipMetadata& clip,double seconds,AudioClipMetadata& result,
        const std::atomic<bool>* cancel=nullptr,double* fundamental=nullptr);
    static bool prepareMelodicArrangementClip(const AudioClipMetadata& clip,double seconds,AudioClipMetadata& result,
        const std::atomic<bool>* cancel=nullptr,double* vibratoHz=nullptr,double* vibratoDepthCents=nullptr);
    struct ClipRhythmAnalysis { double bpm=0.0,confidence=0.0;std::vector<double> transients; };
    ClipRhythmAnalysis analyseArrangementClipRhythm(const AudioClipMetadata& clip);
    static ClipRhythmAnalysis analyseRawClipRhythm(const AudioClipMetadata& clip);
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
        std::atomic<int> clipFadeInCurve { 0 }, clipFadeOutCurve { 0 };
        std::atomic<double> autoCrossfadeInStart { -1.0 }, autoCrossfadeInEnd { -1.0 };
        std::atomic<double> autoCrossfadeOutStart { -1.0 }, autoCrossfadeOutEnd { -1.0 };
        std::atomic<int> autoCrossfadeCurve { 0 }, crossfadeCurve { 0 };
        std::atomic<double> crossfadeDurationSeconds { 0.0 }, crossfadeCenter { 0.5 };
        std::atomic<int> crossfadeOutCurve { -1 }, autoCrossfadeInCurve { -1 }, autoCrossfadeOutCurve { -1 };
        std::atomic<int> autoCrossfadeInPeer { -1 }, autoCrossfadeOutPeer { -1 };
        std::atomic<bool> crossfadeEnergy { false };
        std::atomic<double> autoCrossfadeOverlapStart { -1.0 }, autoCrossfadeOverlapEnd { -1.0 };
        std::atomic<double> clipTrimStart { 0.0 }, clipTrimEnd { 1.0 };
        std::shared_ptr<const std::vector<std::shared_ptr<AudioTrackState>>> regionVoices;
        std::atomic<float> inputGainDb { 0.0f }, highPassHz { 0.0f };
        std::atomic<bool> compressorEnabled { false }, gateEnabled { false }, eqEnabled { false };
        std::atomic<float> compressorThresholdDb { -18.0f }, compressorRatio { 4.0f }, gateThresholdDb { -55.0f };
        std::atomic<float> eqLowDb { 0.0f }, eqMidDb { 0.0f }, eqHighDb { 0.0f };
        ChannelABState channelAB; // under stateLock; audio callback never reads this
        float channelHighPassState[2] {}, channelLowState[2] {}, channelHighState[2] {};
        float channelDynamicsEnvelope = 0.0f;
        std::atomic<float> gain { 1.0f };
        std::atomic<int> mixerBusRoute{-1};
        std::atomic<float> meterPeak { 0.0f }; // audio callback -> L
        std::atomic<float> meterPeakRight { 0.0f }; // audio callback -> R
        std::atomic<std::uint64_t> meterRmsLeft { 0 }, meterRmsRight { 0 };
        LibertyMixConsoleMeter::StereoCorrelationAccumulator meterCorrelation;
        std::atomic<float> clipGain { 1.0f };
        std::atomic<float> clipPan { 0.0f };
        // Callback-owned audition smoothing, never persisted.
        float macroSmoothedGain=1.f,macroSmoothedPan=0.f;
        bool macroSmoothing=false;
        std::int64_t macroNextSample=-1;
        float groupAuditionLevel=1.f;
        std::int64_t groupAuditionNextSample=-1;
        ClipAutomationPtr gainAutomation,panAutomation;
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
    std::shared_ptr<const GroupAuditionMask> groupAudition;
    std::shared_ptr<const GroupMacroPreview> groupMacroPreview;
    void mixAudioVoice(AudioTrackState&,float,float,double,std::int64_t,float* const*,int,int,bool applyAudition=true);

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
        int arrangementClipId = 0;
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
        std::atomic<bool> repeatStepPattern { false };
        std::atomic<double> tempoBpm { 120.0 };
        std::atomic<float> gain { 1.0f };
        std::atomic<int> mixerBusRoute{-1};
        std::atomic<float> meterPeak { 0.0f }; // rendered instrument L
        std::atomic<float> meterPeakRight { 0.0f }; // rendered instrument R
        std::atomic<std::uint64_t> meterRmsLeft { 0 }, meterRmsRight { 0 };
        LibertyMixConsoleMeter::StereoCorrelationAccumulator meterCorrelation;
        std::atomic<float> pan { 0.0f };
        std::atomic<bool> muted { false };
        std::atomic<bool> solo { false };
    };

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) override;
    std::atomic<std::uint64_t> performMidiLearnEvent { 0 };
    std::atomic<std::uint32_t> performMidiLearnSerial { 0 };
    std::array<std::atomic<std::uint64_t>,2048> groupMacroCCWords {},groupMacroSignCCWords {};
    std::atomic<std::uint64_t> groupMacroLearnEvent {0};
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
    // Protect the spectral worker's iteration against vector reallocation and
    // track destruction. The real-time audio callback never acquires this lock.
    juce::CriticalSection spectralTrackListLock;
    // Updated under spectralTrackListLock to reject stale FFT publications.
    std::uint64_t spectralTrackListRevision = 0;
    std::atomic<std::uint64_t> spectralFftStarts { 0 };
    std::atomic<std::int64_t> spectralAddLockAcquisitionNs { 0 };
    std::atomic<std::int64_t> spectralRemoveLockAcquisitionNs { 0 };
    std::atomic<bool> spectralWorkerStop { false };
    // Only true while a dedicated offline test requests deterministic fallback.
    std::atomic<bool> skipSpectralCacheForOfflineTest { false };
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
    struct MixerBusState
    {
        std::atomic<float> gain{1.f},pan{0.f};
        std::atomic<bool> muted{false},solo{false};
        std::atomic<float> peakLeft{0.f},peakRight{0.f};
        std::atomic<std::uint64_t> rmsLeft{0},rmsRight{0};
        LibertyMixConsoleMeter::StereoCorrelationAccumulator correlation;
    };
    std::atomic<int> mixerBusCount{0};
    std::array<MixerBusState,maxMixerBuses> mixerBuses{};
    std::atomic<bool> cpuProfiling{true},cpuTrackProfiling{false};
    LibertyCpu::Meter cpuTotal;
    struct CallbackScratch
    {
        std::array<juce::AudioBuffer<float>,maxMixerBuses> buses;
        juce::AudioBuffer<float> drum, instrument, panic, baseline, track;
        juce::MidiBuffer live, piano, notes, panicNotes;
        void prepare(int frames);
    } callbackScratch;
    void prepareCallbackScratch(int frames);
    LibertyPdc::Bank offlinePdc;
    LibertyPdc::DynamicBank hardwarePdc;
    LibertyRealtime::Plan hardwarePlan{};
    std::atomic<bool> hardwarePdcRequested{false},hardwarePdcPublished{false};
    std::atomic<int> hardwarePdcReportedLatency{0};
    bool hardwarePdcPrepared=false,hardwarePdcBlock=false;
    int hardwarePdcAudioCount=0,hardwarePdcInstrumentCount=0,hardwarePdcBusCount=0;
    std::int64_t hardwarePdcExpectedPosition=-1;
    std::atomic<std::uint64_t> hardwarePdcRevisions{0},hardwarePdcFallbacks{0};
    bool hardwarePdcWasEnabled=false,hardwarePdcWasRunning=false;
    int hardwarePdcCapacity=0;
    void prepareHardwarePdc(double rate,int channels);
    void beginHardwarePdcBlock(int channels) noexcept;
    LibertyPdc::DynamicBank dynamicPdc;
    LibertyPdc::PlanMailbox pdcMailbox;
    std::uint64_t pdcGeneration=0,pdcAppliedGeneration=0;
    bool pdcPlanRejected=false;
    void consumeOfflinePdcPlan() noexcept;
    bool dynamicPdcRequested=false;
    int dynamicPdcCapacity=0,dynamicPdcTransition=128;
    std::uint64_t dynamicPdcUpdates=0;
    bool collectPdcPlan(LibertyRealtime::Plan&) const;
    bool offlinePdcRequested=false,offlinePdcBypass=false,offlinePdcActive=false;
    int offlinePdcLatency=0;
    std::uint64_t offlinePdcBlocks=0;
    std::vector<int> offlinePdcSimulated;
    bool prepareOfflinePdc(double rate,int channels);
    void compensateOfflineLane(int index,juce::AudioBuffer<float>& b,int frames) noexcept;
    static constexpr int maxParallelTracks=128;
    std::unique_ptr<LibertyMulticore::OfflinePool> offlinePool;
    std::vector<juce::AudioBuffer<float>> offlineTrackBuffers;
    std::array<bool,maxParallelTracks> offlineAudible{};
    bool offlineParallelActive=false,offlineAdaptive=false;
    unsigned requestedOfflineWorkers=0,selectedOfflineWorkers=0;
    std::uint64_t adaptiveSerialBlocks=0;
    LibertyMulticore::Cooldown offlineCooldown;
    std::uint64_t parallelBlocks=0,parallelWorkerJobs=0,parallelFallbacks=0;
    struct OfflineTrackBatch {
        AudioEngine* engine;
        double rate;
        std::int64_t position;
        int frames,channels;
    };
    static void renderOfflineTrack(void*,int) noexcept;
    std::atomic<int> preparedScratchFrames{0},inputLatencySamples{0},outputLatencySamples{0};
    std::atomic<std::uint64_t> oversizedCallbacks{0},deviceStarts{0},deviceStops{0};
    std::atomic<bool> deviceRunning{false};
    std::array<LibertyCpu::Meter,cpuProfileTrackCapacity> cpuAudio{},cpuInstrument{};
    LibertyImmersive::Tap masterAnalysis;
    std::atomic<float> masterMeterPeak { 0.0f }; // post-master left
    std::atomic<float> masterMeterPeakRight { 0.0f }; // post-master right
    std::atomic<std::uint64_t> masterMeterRmsLeft { 0 }, masterMeterRmsRight { 0 };
    LibertyMixConsoleMeter::StereoCorrelationAccumulator masterMeterCorrelation;
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