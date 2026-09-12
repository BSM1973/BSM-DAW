#define private public
#include "MainComponent.h"
#undef private

#include <signalsmith-stretch/signalsmith-stretch.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <map>
#include <memory>
#include <utility>
#include <cmath>

namespace
{
struct SourceState
{
    juce::String fileName;
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
    auto& track = engine.tracks[(size_t)trackIndex];

    const bool needsRefresh = state.original == nullptr || state.fileName != track.fileName;
    if (needsRefresh)
    {
        state.fileName = track.fileName;
        state.original = std::make_unique<juce::AudioBuffer<float>>();
        if (track.buffer != nullptr)
            state.original->makeCopyOf(*track.buffer);
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
    if (state.original == nullptr || sourceEnd <= sourceStart)
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

    const int channels = juce::jmax(1, juce::jmin(2, state.original->getNumChannels()));
    const int inputSamples = sourceEnd - sourceStart;
    const int outputSamples = juce::jmax(1, (int)std::llround(targetLengthSeconds * rate));

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

    auto& track = engine.tracks[(size_t)trackIndex];
    track.loaded.store(false, std::memory_order_release);
    track.buffer = std::move(rendered);
    track.numSamples = outputSamples;
    track.lengthSeconds.store((double)outputSamples / rate, std::memory_order_relaxed);
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
    if (trackIndex < 0 || trackIndex >= AudioEngine::maxAudioTracks || !owner.audioEngine.hasAudioFile(trackIndex))
    {
        error = "Invalid audio clip.";
        return false;
    }

    auto& engine = owner.audioEngine;
    auto& track = engine.tracks[(size_t)trackIndex];
    auto& state = ensureSourceState(engine, trackIndex);
    if (state.original == nullptr || state.original->getNumSamples() <= 0)
    {
        error = "The source audio is unavailable.";
        return false;
    }

    const double rate = engine.sampleRate.load(std::memory_order_relaxed);
    const double oldStart = track.startSeconds.load(std::memory_order_relaxed);
    const double oldLength = juce::jmax(0.000001, track.lengthSeconds.load(std::memory_order_relaxed));
    const double oldRight = oldStart + oldLength;
    const double minLength = 1.0 / juce::jmax(1.0, rate);

    double newStart = juce::jmax(0.0, requestedStartSeconds);
    double newLength = juce::jmax(minLength, requestedLengthSeconds);

    int sourceStart = state.sourceStartSample;
    int sourceEnd = state.sourceEndSample;
    const int sourceSpan = juce::jmax(1, sourceEnd - sourceStart);

    if (!preservePitchStretch)
    {
        if (resizeLeft)
        {
            newStart = juce::jlimit(0.0, oldRight - minLength, newStart);
            newLength = oldRight - newStart;
            const double ratio = juce::jmax(0.0, newLength / oldLength);
            const int desiredSpan = juce::jmax(1, (int)std::llround((double)sourceSpan * ratio));
            sourceStart = juce::jmax(0, sourceEnd - desiredSpan);
            newLength = oldLength * ((double)(sourceEnd - sourceStart) / (double)sourceSpan);
            newStart = oldRight - newLength;
        }
        else
        {
            const double ratio = juce::jmax(0.0, newLength / oldLength);
            const int desiredSpan = juce::jmax(1, (int)std::llround((double)sourceSpan * ratio));
            sourceEnd = juce::jmin(state.original->getNumSamples(), sourceStart + desiredSpan);
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

void clearLibertyAudioClipResizeSource(AudioEngine& engine, int trackIndex)
{
    sourceStates.erase({ &engine, trackIndex });
}
