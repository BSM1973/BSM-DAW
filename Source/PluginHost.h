#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <atomic>
#include <functional>
#include <memory>
#include <array>

class LibertyPluginHost final
{
public:
    using ScanProgressCallback = std::function<void(const juce::String& formatName,
                                                    const juce::String& pluginName,
                                                    float progress)>;

    static LibertyPluginHost& instance();
    static bool runSingleVST3ScanHelper(const juce::String& pluginIdentifier,
                                        const juce::File& resultFile);

    void initialise(double sampleRate, int blockSize);
    void shutdown();
    void clearProjectPlugins();
    void scanInstalledPlugins(const ScanProgressCallback& progressCallback = {});
    const juce::KnownPluginList& getKnownPluginList() const noexcept { return knownPlugins; }
    juce::Array<juce::PluginDescription> getPluginDescriptions() const;
    juce::StringArray getBlacklistedPlugins() const;
    void clearBlacklist();
    juce::File getBlacklistFolder() const;

    static constexpr int effectSlotsPerTrack = 8;
    bool loadEffectForTrack(int trackIndex, const juce::PluginDescription& description, juce::String& error);
    bool loadEffectForTrackSlot(int trackIndex, int slotIndex, const juce::PluginDescription& description, juce::String& error);
    bool loadInstrument(const juce::PluginDescription& description, juce::String& error);
    bool loadInstrumentForTrack(int instrumentTrack, const juce::PluginDescription& description, juce::String& error);
    void unloadEffectForTrack(int trackIndex);
    void unloadEffectForTrackSlot(int trackIndex, int slotIndex);
    void unloadInstrument();
    void unloadInstrumentForTrack(int instrumentTrack);
    bool hasEffectForTrack(int trackIndex) const;
    bool hasEffectForTrackSlot(int trackIndex, int slotIndex) const;
    bool hasInstrument() const;
    bool hasInstrumentForTrack(int instrumentTrack) const;
    juce::String getEffectName(int trackIndex) const;
    juce::String getEffectNameForTrackSlot(int trackIndex, int slotIndex) const;
    juce::String getInstrumentName() const;
    juce::String getInstrumentNameForTrack(int instrumentTrack) const;
    bool getEffectDescriptionForTrack(int trackIndex, juce::PluginDescription& out) const;
    bool getEffectDescriptionForTrackSlot(int trackIndex, int slotIndex, juce::PluginDescription& out) const;
    bool getEffectDescriptionForInstrumentTrackSlot(int instrumentTrack, int slotIndex, juce::PluginDescription& out) const;
    bool getInstrumentDescriptionForTrack(int instrumentTrack, juce::PluginDescription& out) const;
    bool findKnownPluginByIdentifier(const juce::String& identifier, juce::PluginDescription& out) const;
    bool getEffectStateForTrackSlot(int trackIndex, int slotIndex, juce::MemoryBlock& out) const;
    bool setEffectStateForTrackSlot(int trackIndex, int slotIndex, const juce::MemoryBlock& state);
    bool getEffectStateForInstrumentTrackSlot(int instrumentTrack, int slotIndex, juce::MemoryBlock& out) const;
    bool setEffectStateForInstrumentTrackSlot(int instrumentTrack, int slotIndex, const juce::MemoryBlock& state);
    bool getInstrumentStateForTrack(int instrumentTrack, juce::MemoryBlock& out) const;
    bool setInstrumentStateForTrack(int instrumentTrack, const juce::MemoryBlock& state);


    void beginAudioTrackBlock(int trackIndex, float* const* outputChannelData, int numOutputChannels, int numSamples);
    void endAudioTrackBlock(int trackIndex, float* const* outputChannelData, int numOutputChannels, int numSamples);
    bool processInstrument(float* const* outputChannelData, int numOutputChannels, int numSamples, juce::MidiBuffer& midi);
    bool processInstrumentForTrack(int instrumentTrack, float* const* outputChannelData, int numOutputChannels, int numSamples, juce::MidiBuffer& midi, float gain = 1.0f, float pan = 0.0f);
    bool renderInstrumentForTrack(int instrumentTrack, juce::AudioBuffer<float>& destination, int numSamples, juce::MidiBuffer& midi, float gain = 1.0f, float pan = 0.0f);
    bool loadEffectForInstrumentTrack(int instrumentTrack, const juce::PluginDescription& description, juce::String& error);
    bool loadEffectForInstrumentTrackSlot(int instrumentTrack, int slotIndex, const juce::PluginDescription& description, juce::String& error);
    void unloadEffectForInstrumentTrack(int instrumentTrack);
    void unloadEffectForInstrumentTrackSlot(int instrumentTrack, int slotIndex);
    bool hasEffectForInstrumentTrack(int instrumentTrack) const;
    bool hasEffectForInstrumentTrackSlot(int instrumentTrack, int slotIndex) const;
    juce::String getEffectNameForInstrumentTrack(int instrumentTrack) const;
    juce::String getEffectNameForInstrumentTrackSlot(int instrumentTrack, int slotIndex) const;
    void showEffectEditorForInstrumentTrack(int instrumentTrack);
    void showEffectEditorForInstrumentTrackSlot(int instrumentTrack, int slotIndex);
    void processInstrumentEffect(int instrumentTrack, juce::AudioBuffer<float>& buffer, int numSamples);
    void processAudioEffectSlot(int trackIndex, int slotIndex, juce::AudioBuffer<float>& buffer);
    void processInstrumentEffectSlot(int instrumentTrack, int slotIndex, juce::AudioBuffer<float>& buffer);
    void resetInstrumentEffectState(int instrumentTrack);

    // Offline render is exclusive: the realtime audio callback is prevented from
    // driving the same instrument instance while AI Render owns it.
    bool beginOfflineInstrumentRender(int instrumentTrack);
    bool processOfflineInstrument(int instrumentTrack, float* const* outputChannelData, int numOutputChannels, int numSamples, juce::MidiBuffer& midi, float gain = 1.0f, float pan = 0.0f);
    void endOfflineInstrumentRender(int instrumentTrack);

    void showEditorForTrack(int trackIndex);
    void showEditorForTrackSlot(int trackIndex, int slotIndex);
    void showInstrumentEditor();
    void showInstrumentEditorForTrack(int instrumentTrack);

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
    juce::File blacklistFolder() const;
    juce::File blacklistTextFile() const;
    void loadCachedPluginList();
    void saveCachedPluginList();
    void recoverCrashedPluginsFromDeadMansPedal();
    void loadPersistentBlacklist();
    void savePersistentBlacklist();
    bool scanVST3OutOfProcess(const juce::String& identifier, const juce::String& displayName);
    void blacklistPluginIdentifier(const juce::String& identifier);
    bool validTrack(int trackIndex) const noexcept { return trackIndex >= 0; }
    void ensureAudioTrackSlot(int trackIndex);
    void ensureInstrumentSlot(int instrumentTrack);

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    std::vector<std::array<std::unique_ptr<Slot>, effectSlotsPerTrack>> trackEffects;
    std::vector<std::unique_ptr<Slot>> instruments;
    std::vector<std::array<std::unique_ptr<Slot>, effectSlotsPerTrack>> instrumentEffects;
    double currentSampleRate = 48000.0;
    int currentBlockSize = 512;
    mutable juce::CriticalSection lock;
    std::atomic<bool> shutdownCompleted { false };
    std::atomic<int> offlineInstrumentTrack { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibertyPluginHost)
};
