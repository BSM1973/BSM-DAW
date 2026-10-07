#define private public
#include "MainComponent.h"
#undef private

#include <signalsmith-stretch/signalsmith-stretch.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <map>
#include <memory>
#include <utility>
#include <cmath>
#include <limits>

namespace
{
struct SourceState
{
    std::shared_ptr<const juce::AudioBuffer<float>> sourceBuffer;
    std::unique_ptr<juce::AudioBuffer<float>> original;
    int sourceStartSample = 0;
    int sourceEndSample = 0;
};

using Key = std::pair<AudioEngine*, int>;
std::map<Key, SourceState> sourceStates;

SourceState& ensureSourceState(AudioEngine& engine, int trackIndex)
{
    const Key key { &engine, trackIndex };
    auto& state = sourceStates[key];
    auto& track = *engine.tracks[(size_t)trackIndex];
    const auto publishedBuffer = std::atomic_load(&track.buffer);

    const bool needsRefresh = state.original == nullptr || state.sourceBuffer != publishedBuffer;
    if (needsRefresh)
    {
        state.sourceBuffer = publishedBuffer;
        state.original = std::make_unique<juce::AudioBuffer<float>>();
        if (publishedBuffer != nullptr)
            state.original->makeCopyOf(*publishedBuffer);
        state.sourceStartSample = 0;
        state.sourceEndSample = state.original->getNumSamples();
    }
    return state;
}

bool renderRegion(AudioEngine& engine,
                  int trackIndex,
                  SourceState& state,
                  int sourceStart,
                  int sourceEnd,
                  double targetLengthSeconds,
                  juce::String& error)
{
    error.clear();
    if (state.original == nullptr
        || sourceStart < 0
        || sourceEnd <= sourceStart
        || sourceEnd > state.original->getNumSamples())
    {
        error = "Invalid source region.";
        return false;
    }

    const double rate = engine.sampleRate.load(std::memory_order_relaxed);
    if (rate <= 0.0)
    {
        error = "No audio device is available.";
        return false;
    }

    const double requestedSamples = targetLengthSeconds * rate;
    if (!std::isfinite(requestedSamples)
        || requestedSamples < 1.0
        || requestedSamples > (double)std::numeric_limits<int>::max())
    {
        error = "The requested audio resize is too large.";
        return false;
    }

    const int channels = juce::jmax(1, juce::jmin(2, state.original->getNumChannels()));
    constexpr std::uint64_t maxResizeBufferBytes = 512ull * 1024ull * 1024ull;
    const auto requestedBytes = (std::uint64_t)std::llround(requestedSamples)
                              * (std::uint64_t)channels
                              * sizeof(float);
    if (requestedBytes > maxResizeBufferBytes)
    {
        error = "The requested audio resize exceeds the safe render memory limit.";
        return false;
    }

    const int inputSamples = sourceEnd - sourceStart;
    const int outputSamples = juce::jmax(1, (int)std::llround(requestedSamples));
    if (inputSamples != outputSamples)
    {
        const auto inputBytes = (std::uint64_t)inputSamples
                              * (std::uint64_t)channels
                              * sizeof(float);
        if (inputBytes > maxResizeBufferBytes
            || inputBytes > maxResizeBufferBytes - requestedBytes)
        {
            error = "The requested audio stretch exceeds the safe temporary memory limit.";
            return false;
        }
    }

    auto rendered = std::make_unique<juce::AudioBuffer<float>>(channels, outputSamples);
    rendered->clear();

    if (inputSamples == outputSamples)
    {
        for (int ch = 0; ch < channels; ++ch)
            rendered->copyFrom(ch, 0, *state.original, ch, sourceStart, outputSamples);
    }
    else
    {
        juce::AudioBuffer<float> input(channels, inputSamples);
        for (int ch = 0; ch < channels; ++ch)
            input.copyFrom(ch, 0, *state.original, ch, sourceStart, inputSamples);

        signalsmith::stretch::SignalsmithStretch<float> stretch;
        stretch.presetDefault(channels, rate);
        stretch.setTransposeFactor(1.0);

        auto inputPointers = input.getArrayOfReadPointers();
        auto outputPointers = rendered->getArrayOfWritePointers();
        stretch.process(inputPointers, inputSamples, outputPointers, outputSamples);
    }

    const bool wasInitialised = engine.initialised.load(std::memory_order_relaxed);
    const bool wasPlaying = engine.playing.load(std::memory_order_relaxed);
    if (wasInitialised)
        engine.deviceManager.removeAudioCallback(&engine);
    engine.playing.store(false, std::memory_order_relaxed);

    auto& track = *engine.tracks[(size_t)trackIndex];
    track.loaded.store(false, std::memory_order_release);
    std::shared_ptr<juce::AudioBuffer<float>> publishedBuffer(std::move(rendered));
    state.sourceBuffer = publishedBuffer;
    std::atomic_store(&track.buffer, std::move(publishedBuffer));
    track.contentRevision.fetch_add(1, std::memory_order_relaxed);
    track.lengthSeconds.store((double)outputSamples / rate, std::memory_order_relaxed);
    track.bufferSampleRate.store(rate, std::memory_order_relaxed);
    track.warpEnabled.store(false, std::memory_order_relaxed);
    track.loaded.store(true, std::memory_order_release);
    engine.resetTrackWarpMarkers(trackIndex);

    if (wasInitialised)
        engine.deviceManager.addAudioCallback(&engine);
    if (wasPlaying)
        engine.setPlaying(true);

    return true;
}
}

bool commitLibertyAudioClipResize(MainComponent& owner,
                                  int trackIndex,
                                  double requestedStartSeconds,
                                  double requestedLengthSeconds,
                                  bool preservePitchStretch,
                                  bool resizeLeft,
                                  juce::String& error)
{
    error.clear();
    if (trackIndex < 0 || trackIndex >= owner.audioEngine.getAudioTrackCount() || !owner.audioEngine.hasAudioFile(trackIndex))
    {
        error = "Invalid audio clip.";
        return false;
    }

    auto& engine = owner.audioEngine;
    auto& track = *engine.tracks[(size_t)trackIndex];
    auto& state = ensureSourceState(engine, trackIndex);
    if (state.original == nullptr || state.original->getNumSamples() <= 0)
    {
        error = "The source audio is unavailable.";
        return false;
    }

    const double rate = engine.sampleRate.load(std::memory_order_relaxed);
    const double oldStart = track.startSeconds.load(std::memory_order_relaxed);
    const double storedLength = track.lengthSeconds.load(std::memory_order_relaxed);
    if (!std::isfinite(requestedStartSeconds) || !std::isfinite(requestedLengthSeconds)
        || !std::isfinite(rate) || rate <= 0.0
        || !std::isfinite(oldStart) || !std::isfinite(storedLength) || storedLength <= 0.0)
    {
        error = "The audio resize parameters are invalid.";
        return false;
    }

    const double oldLength = juce::jmax(0.000001, storedLength);
    const double oldRight = oldStart + oldLength;
    if (!std::isfinite(oldRight))
    {
        error = "The audio clip timing is invalid.";
        return false;
    }

    const double minLength = 1.0 / rate;
    double newStart = juce::jmax(0.0, requestedStartSeconds);
    double newLength = juce::jmax(minLength, requestedLengthSeconds);

    int sourceStart = state.sourceStartSample;
    int sourceEnd = state.sourceEndSample;
    const int totalSourceSamples = state.original->getNumSamples();
    if (sourceStart < 0 || sourceEnd <= sourceStart || sourceEnd > totalSourceSamples)
    {
        error = "The cached audio resize source region is invalid.";
        return false;
    }

    const int sourceSpan = sourceEnd - sourceStart;

    if (!preservePitchStretch)
    {
        if (resizeLeft)
        {
            newStart = juce::jlimit(0.0, oldRight - minLength, newStart);
            newLength = oldRight - newStart;
            const double ratio = juce::jmax(0.0, newLength / oldLength);
            const double requestedSpan = (double)sourceSpan * ratio;
            const int desiredSpan = !std::isfinite(requestedSpan)
                ? state.original->getNumSamples()
                : juce::jlimit(1, state.original->getNumSamples(),
                               (int)std::llround(juce::jmin(requestedSpan,
                                                          (double)state.original->getNumSamples())));
            sourceStart = juce::jmax(0, sourceEnd - desiredSpan);
            newLength = oldLength * ((double)(sourceEnd - sourceStart) / (double)sourceSpan);
            newStart = oldRight - newLength;
        }
        else
        {
            const double ratio = juce::jmax(0.0, newLength / oldLength);
            const double requestedSpan = (double)sourceSpan * ratio;
            const int desiredSpan = !std::isfinite(requestedSpan)
                ? state.original->getNumSamples()
                : juce::jlimit(1, state.original->getNumSamples(),
                               (int)std::llround(juce::jmin(requestedSpan,
                                                          (double)state.original->getNumSamples())));
            const int remainingSamples = juce::jmax(0, state.original->getNumSamples() - sourceStart);
            sourceEnd = sourceStart + juce::jmin(desiredSpan, remainingSamples);
            newLength = oldLength * ((double)(sourceEnd - sourceStart) / (double)sourceSpan);
        }
    }
    else if (resizeLeft)
    {
        newStart = juce::jlimit(0.0, oldRight - minLength, newStart);
        newLength = oldRight - newStart;
    }

    if (sourceEnd <= sourceStart)
    {
        error = "The clip cannot be resized any further.";
        return false;
    }

    if (!renderRegion(engine, trackIndex, state, sourceStart, sourceEnd, newLength, error))
        return false;

    state.sourceStartSample = sourceStart;
    state.sourceEndSample = sourceEnd;
    track.startSeconds.store(newStart, std::memory_order_relaxed);
    owner.rebuildWaveformCache(trackIndex);
    owner.selectedTrack = trackIndex;
    owner.repaint();
    return true;
}

bool commitLibertyAudioTempoChange(MainComponent& owner, double tempoRatio, juce::String& error)
{
    error.clear();
    auto& engine = owner.audioEngine;
    const double rate = engine.sampleRate.load(std::memory_order_relaxed);
    if (!std::isfinite(tempoRatio) || tempoRatio <= 0.0 || !std::isfinite(rate) || rate <= 0.0)
    {
        error = "The tempo stretch parameters are invalid.";
        return false;
    }

    struct Prepared
    {
        int trackIndex = -1;
        double startSeconds = 0.0;
        int sourceStart = 0;
        int sourceEnd = 0;
        std::unique_ptr<juce::AudioBuffer<float>> buffer;
    };
    std::vector<Prepared> prepared;
    constexpr std::uint64_t maxTempoBatchBytes = 512ull * 1024ull * 1024ull;
    std::uint64_t preparedBytes = 0;

    for (int trackIndex = 0; trackIndex < engine.getAudioTrackCount(); ++trackIndex)
    {
        if (!engine.hasAudioFile(trackIndex)) continue;
        auto& track = *engine.tracks[(size_t)trackIndex];
        auto& state = ensureSourceState(engine, trackIndex);
        if (state.original == nullptr || state.original->getNumSamples() <= 0)
        {
            error = "Audio " + juce::String(trackIndex + 1) + ": source audio is unavailable.";
            return false;
        }

        const double oldStart = track.startSeconds.load(std::memory_order_relaxed);
        const double oldLength = track.lengthSeconds.load(std::memory_order_relaxed);
        if (!std::isfinite(oldStart) || !std::isfinite(oldLength) || oldLength <= 0.0)
        {
            error = "Audio " + juce::String(trackIndex + 1) + ": invalid clip timing.";
            return false;
        }

        const int sourceStart = state.sourceStartSample;
        const int sourceEnd = state.sourceEndSample;
        if (sourceStart < 0 || sourceEnd <= sourceStart || sourceEnd > state.original->getNumSamples())
        {
            error = "Audio " + juce::String(trackIndex + 1) + ": invalid cached source region.";
            return false;
        }

        const double targetLength = oldLength * tempoRatio;
        const double requestedSamples = targetLength * rate;
        if (!std::isfinite(requestedSamples) || requestedSamples < 1.0
            || requestedSamples > (double)std::numeric_limits<int>::max())
        {
            error = "Audio " + juce::String(trackIndex + 1) + ": requested tempo stretch is too large.";
            return false;
        }

        const int channels = juce::jmax(1, juce::jmin(2, state.original->getNumChannels()));
        constexpr std::uint64_t maxResizeBufferBytes = 512ull * 1024ull * 1024ull;
        const int inputSamples = sourceEnd - sourceStart;
        const int outputSamples = juce::jmax(1, (int)std::llround(requestedSamples));
        const auto outputBytes = (std::uint64_t)outputSamples * (std::uint64_t)channels * sizeof(float);
        const auto inputBytes = (std::uint64_t)inputSamples * (std::uint64_t)channels * sizeof(float);
        if (outputBytes > maxResizeBufferBytes
            || outputBytes > maxTempoBatchBytes - preparedBytes
            || (inputSamples != outputSamples
                && (inputBytes > maxResizeBufferBytes || inputBytes > maxResizeBufferBytes - outputBytes)))
        {
            error = "Audio " + juce::String(trackIndex + 1) + ": tempo stretch exceeds the safe memory limit.";
            return false;
        }

        try
        {
            auto rendered = std::make_unique<juce::AudioBuffer<float>>(channels, outputSamples);
            rendered->clear();
            if (inputSamples == outputSamples)
            {
                for (int ch = 0; ch < channels; ++ch)
                    rendered->copyFrom(ch, 0, *state.original, ch, sourceStart, outputSamples);
            }
            else
            {
                juce::AudioBuffer<float> input(channels, inputSamples);
                for (int ch = 0; ch < channels; ++ch)
                    input.copyFrom(ch, 0, *state.original, ch, sourceStart, inputSamples);
                signalsmith::stretch::SignalsmithStretch<float> stretch;
                stretch.presetDefault(channels, rate);
                stretch.setTransposeFactor(1.0);
                stretch.process(input.getArrayOfReadPointers(), inputSamples,
                                rendered->getArrayOfWritePointers(), outputSamples);
            }

            Prepared item;
            item.trackIndex = trackIndex;
            item.startSeconds = oldStart * tempoRatio;
            item.sourceStart = sourceStart;
            item.sourceEnd = sourceEnd;
            item.buffer = std::move(rendered);
            prepared.push_back(std::move(item));
            preparedBytes += outputBytes;
        }
        catch (const std::exception& exception)
        {
            error = "Audio " + juce::String(trackIndex + 1) + ": tempo render failed: "
                  + juce::String(exception.what());
            return false;
        }
        catch (...)
        {
            error = "Audio " + juce::String(trackIndex + 1) + ": tempo render failed.";
            return false;
        }
    }

    const bool wasInitialised = engine.initialised.load(std::memory_order_relaxed);
    const bool wasPlaying = engine.playing.load(std::memory_order_relaxed);
    if (wasInitialised) engine.deviceManager.removeAudioCallback(&engine);
    engine.playing.store(false, std::memory_order_relaxed);

    for (auto& item : prepared)
    {
        auto& track = *engine.tracks[(size_t)item.trackIndex];
        auto& state = ensureSourceState(engine, item.trackIndex);
        track.loaded.store(false, std::memory_order_release);
        std::shared_ptr<juce::AudioBuffer<float>> publishedBuffer(std::move(item.buffer));
        state.sourceBuffer = publishedBuffer;
        std::atomic_store(&track.buffer, std::move(publishedBuffer));
        state.sourceStartSample = item.sourceStart;
        state.sourceEndSample = item.sourceEnd;
        track.contentRevision.fetch_add(1, std::memory_order_relaxed);
        const auto buffer = std::atomic_load(&track.buffer);
        track.lengthSeconds.store(buffer != nullptr ? (double)buffer->getNumSamples() / rate : 0.0,
                                  std::memory_order_relaxed);
        track.bufferSampleRate.store(rate, std::memory_order_relaxed);
        track.startSeconds.store(juce::jmax(0.0, item.startSeconds), std::memory_order_relaxed);
        track.warpEnabled.store(false, std::memory_order_relaxed);
        track.loaded.store(true, std::memory_order_release);
        engine.resetTrackWarpMarkers(item.trackIndex);
    }

    if (wasInitialised) engine.deviceManager.addAudioCallback(&engine);
    if (wasPlaying) engine.setPlaying(true);
    for (const auto& item : prepared)
        owner.rebuildWaveformCache(item.trackIndex);
    owner.repaint();
    return true;
}

void clearLibertyAudioClipResizeSource(AudioEngine& engine, int trackIndex)
{
    sourceStates.erase({ &engine, trackIndex });
}
