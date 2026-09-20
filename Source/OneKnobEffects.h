#pragma once

#include <JuceHeader.h>
#include <array>
#include <memory>

class LibertyOneKnobRack final
{
public:
    enum class Type { none = 0, chorus, flanger, phaser, tremolo };

    LibertyOneKnobRack();
    ~LibertyOneKnobRack();

    void prepare(double sampleRate, int maximumBlockSize);
    void reset();
    void setType(Type newType);
    Type getType() const noexcept { return type; }
    void setAmount(float newAmount) noexcept;
    float getAmount() const noexcept { return amount; }
    juce::String getName() const;
    void process(juce::AudioBuffer<float>& buffer);

private:
    void updateParameters();

    Type type = Type::none;
    float amount = 0.50f;
    double sampleRate = 48000.0;
    juce::dsp::Chorus<float> chorus;
    juce::dsp::Chorus<float> flanger;
    juce::dsp::Phaser<float> phaser;
    double tremoloPhase = 0.0;
};

class LibertyOneKnobManager final
{
public:
    static constexpr int maxTracks = 4;
    static LibertyOneKnobManager& instance();

    void prepare(double sampleRate, int maximumBlockSize);
    void setEffect(int trackIndex, LibertyOneKnobRack::Type type);
    void clearEffect(int trackIndex);
    LibertyOneKnobRack::Type getEffect(int trackIndex) const;
    void setAmount(int trackIndex, float amount);
    float getAmount(int trackIndex) const;
    juce::String getName(int trackIndex) const;
    bool hasEffect(int trackIndex) const;
    void process(int trackIndex, juce::AudioBuffer<float>& buffer);
    void beginAudioTrackBlock(int trackIndex, float* const* outputChannelData, int numOutputChannels, int numSamples);
    void endAudioTrackBlock(int trackIndex, float* const* outputChannelData, int numOutputChannels, int numSamples);
    void showEditor(int trackIndex);

private:
    LibertyOneKnobManager();
    bool validTrack(int trackIndex) const noexcept { return trackIndex >= 0 && trackIndex < maxTracks; }
    std::array<LibertyOneKnobRack, maxTracks> racks;
    std::array<juce::AudioBuffer<float>, maxTracks> baselines;
    std::array<juce::AudioBuffer<float>, maxTracks> workBuffers;
    std::array<std::unique_ptr<juce::DocumentWindow>, maxTracks> editors;
    mutable juce::CriticalSection lock;
};
