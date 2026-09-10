#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "MidiEngine.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

class AudioEngine final : private juce::AudioIODeviceCallback
{
public:
    static constexpr int maxAudioTracks = 4;

    AudioEngine();
    ~AudioEngine() override;

    bool initialise();
    void shutdown();

    bool isInitialised() const noexcept { return initialised.load(std::memory_order_relaxed); }
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
    }
    bool isPlaying() const noexcept { return playing.load(std::memory_order_relaxed); }
    void resetTransport() noexcept
    {
        transportSamples.store(0, std::memory_order_relaxed);
        playbackClockBaseSeconds.store(0.0, std::memory_order_relaxed);
        playbackClockStartMilliseconds.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
    }
    void setCurrentTimeSeconds(double seconds) noexcept;
    double getCurrentTimeSeconds() const noexcept;

    void setProjectExtraLengthSeconds(double seconds) noexcept { projectExtraLengthSeconds.store(juce::jmax(0.0, seconds), std::memory_order_relaxed); }
    double getProjectExtraLengthSeconds() const noexcept { return projectExtraLengthSeconds.load(std::memory_order_relaxed); }

    void setMidiNotes(const std::vector<MidiEngine::NoteEvent>& notes,
                      double clipStartSeconds,
                      double clipLengthSeconds,
                      double tempoBpm) noexcept;

    void setTrackGain(int trackIndex, float gain) noexcept;
    float getTrackGain(int trackIndex) const noexcept;
    void setTrackPan(int trackIndex, float pan) noexcept;
    float getTrackPan(int trackIndex) const noexcept;
    void setTrackMuted(int trackIndex, bool muted) noexcept;
    bool isTrackMuted(int trackIndex) const noexcept;
    void setTrackSolo(int trackIndex, bool solo) noexcept;
    bool isTrackSolo(int trackIndex) const noexcept;
    bool isAnyTrackSolo() const noexcept;

    bool loadAudioFileIntoTrack(int trackIndex, const juce::File& file, juce::String& error);
    void clearAudioTrack(int trackIndex);
    bool splitAudioTrack(int trackIndex, double splitProjectSeconds, int& newTrackIndex, juce::String& error);
    bool hasAudioFile(int trackIndex) const noexcept;
    juce::String getAudioFileName(int trackIndex) const;
    double getAudioFileLengthSeconds(int trackIndex) const noexcept;
    double getTrackStartSeconds(int trackIndex) const noexcept;
    void setTrackStartSeconds(int trackIndex, double seconds) noexcept;
    const juce::AudioBuffer<float>* getAudioBuffer(int trackIndex) const noexcept;

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
    const juce::AudioBuffer<float>* getAudioBuffer() const noexcept { return getAudioBuffer(0); }

private:
    struct AudioTrackState
    {
        std::atomic<float> gain { 1.0f };
        std::atomic<float> pan { 0.0f };
        std::atomic<bool> muted { false };
        std::atomic<bool> solo { false };
        std::atomic<bool> loaded { false };
        std::atomic<double> lengthSeconds { 0.0 };
        std::atomic<double> startSeconds { 0.0 };
        std::unique_ptr<juce::AudioBuffer<float>> buffer;
        std::int64_t numSamples = 0;
        juce::String fileName;
    };

    static constexpr std::size_t maxMidiPlaybackNotes = 256;
    struct MidiPlaybackNote
    {
        std::atomic<double> startSeconds { 0.0 };
        std::atomic<double> endSeconds { 0.0 };
        std::atomic<double> frequency { 440.0 };
        std::atomic<float> amplitude { 0.0f };
    };

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    bool isValidTrackIndex(int trackIndex) const noexcept { return trackIndex >= 0 && trackIndex < maxAudioTracks; }
    std::int64_t getProjectLengthSamples() const noexcept;

    juce::AudioDeviceManager deviceManager;
    std::array<AudioTrackState, maxAudioTracks> tracks;
    std::array<MidiPlaybackNote, maxMidiPlaybackNotes> midiPlaybackNotes;
    std::atomic<std::size_t> midiPlaybackNoteCount { 0 };
    std::atomic<double> midiClipStartSeconds { 0.0 };
    std::atomic<double> midiClipLengthSeconds { 0.0 };
    std::atomic<double> midiTempoBpm { 120.0 };
    std::atomic<bool> initialised { false };
    std::atomic<bool> playing { false };
    std::atomic<double> sampleRate { 0.0 };
    std::atomic<int> bufferSize { 0 };
    std::atomic<int> outputChannels { 0 };
    std::atomic<std::int64_t> transportSamples { 0 };
    std::atomic<double> projectExtraLengthSeconds { 0.0 };
    std::atomic<double> playbackClockBaseSeconds { 0.0 };
    std::atomic<double> playbackClockStartMilliseconds { 0.0 };
    std::atomic<float> masterGain { 1.0f };
    mutable juce::CriticalSection stateLock;
    juce::String deviceName;
    juce::String lastError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};