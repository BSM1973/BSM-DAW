#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <memory>

class LibertyPluginHost final
{
public:
    static constexpr int maxAudioTracks = 4;

    static LibertyPluginHost& instance();

    void initialise(double sampleRate, int blockSize);
    void shutdown();

    void scanInstalledPlugins();
    const juce::KnownPluginList& getKnownPluginList() const noexcept { return knownPlugins; }
    juce::Array<juce::PluginDescription> getPluginDescriptions() const;

    bool loadEffectForTrack(int trackIndex, const juce::PluginDescription& description, juce::String& error);
    bool loadInstrument(const juce::PluginDescription& description, juce::String& error);
    void unloadEffectForTrack(int trackIndex);
    void unloadInstrument();

    bool hasEffectForTrack(int trackIndex) const;
    bool hasInstrument() const;
    juce::String getEffectName(int trackIndex) const;
    juce::String getInstrumentName() const;

    void beginAudioTrackBlock(int trackIndex, float* const* outputChannelData, int numOutputChannels, int numSamples);
    void endAudioTrackBlock(int trackIndex, float* const* outputChannelData, int numOutputChannels, int numSamples);
    bool processInstrument(juce::AudioBuffer<float>& output, juce::MidiBuffer& midi);

    void showEditorForTrack(int trackIndex);
    void showInstrumentEditor();

private:
    LibertyPluginHost();
    ~LibertyPluginHost();

    struct Slot
    {
        std::unique_ptr<juce::AudioPluginInstance> processor;
        juce::PluginDescription description;
        juce::AudioBuffer<float> baseline;
        juce::AudioBuffer<float> work;
        std::unique_ptr<juce::DocumentWindow> editorWindow;
    };

    bool loadIntoSlot(Slot& slot, const juce::PluginDescription& description, juce::String& error, bool requireInstrument);
    void prepareSlot(Slot& slot);
    void closeEditor(Slot& slot);
    void showEditor(Slot& slot, const juce::String& title);
    juce::File pluginListFile() const;
    juce::File deadMansPedalFile() const;
    void loadCachedPluginList();
    void saveCachedPluginList();
    bool validTrack(int trackIndex) const noexcept { return trackIndex >= 0 && trackIndex < maxAudioTracks; }

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    std::array<Slot, maxAudioTracks> trackEffects;
    Slot instrument;
    double currentSampleRate = 48000.0;
    int currentBlockSize = 512;
    mutable juce::CriticalSection lock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibertyPluginHost)
};
