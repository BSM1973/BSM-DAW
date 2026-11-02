#pragma once
#include "AudioEngine.h"
#include <array>
#include <optional>
#include <utility>

// A CANAL dialog edits a pending copy. Only apply() changes the audio engine.
// The clipboard is internal to Liberty and is shared by all CANAL dialogs.
namespace LibertyChannelInspectorState
{
class Session final
{
public:
    using Settings = AudioEngine::ChannelProcessingSettings;
    enum class Section { Input, Gate, Compressor, Equalizer };
    Session(AudioEngine& source, int track)
      : engine(source), trackIndex(track)
    {
        const auto memory=engine.getChannelABState(trackIndex);
        variants=memory.variants;
        openingVariants=memory.variants;
        activeVariant=memory.active;
    }

    const Settings& pending() const noexcept { return variants[(size_t)activeVariant]; }
    void setPending(const Settings& settings) noexcept { variants[(size_t)activeVariant] = settings; }
    void reset() noexcept { variants[(size_t)activeVariant] = Settings{}; }
    void cancel() noexcept
    {
        const auto memory=engine.getChannelABState(trackIndex);
        variants=memory.variants;
        activeVariant=memory.active;
    }
    void apply() noexcept
    {
        engine.setChannelProcessingSettings(trackIndex,pending());
        AudioEngine::ChannelABState memory;
        memory.variants=variants;
        memory.active=activeVariant;
        memory.saved=true;
        engine.setChannelABState(trackIndex,memory);
    }

    int variantIndex() const noexcept { return activeVariant; }
    void switchVariant() noexcept { activeVariant = 1 - activeVariant; }

    // Duplicate a prepared variant without switching banks or touching DSP.
    // The caller must first capture any unsaved knob edits with setPending().
    bool copyVariant(int from,int to) noexcept
    {
        if(from<0 || from>=2 || to<0 || to>=2 || from==to)
            return false;
        variants[(size_t)to]=variants[(size_t)from];
        return true;
    }

    // Copy only the specified signal-processing section. Gate, compressor
    // and EQ each copy their activation switch as well as their parameters.
    // Neither the selection nor the actual audio engine is changed.
    bool copySection(int from,int to,Section section) noexcept
    {
        if(from<0 || from>=2 || to<0 || to>=2 || from==to)
            return false;
        return transferSection(variants[(size_t)from],variants[(size_t)to],section);
    }

    // Exchange just one group of controls between A and B. The selection,
    // unrelated groups, the preset references and live DSP remain untouched.
    bool swapSection(Section section) noexcept
    {
        const auto originalA=variants[0];
        const auto originalB=variants[1];
        if(!transferSection(originalB,variants[0],section))
            return false;
        return transferSection(originalA,variants[1],section);
    }

    // Return only the selected section to neutral values, leaving the other
    // treatments, the other A/B bank and the live audio engine untouched.
    bool resetSection(Section section) noexcept
    {
        return transferSection(Settings{},variants[(size_t)activeVariant],section);
    }

    // Recover only this processing section from the bank's opening snapshot.
    // The current A/B selection, the other sections and the DSP are unchanged.
    bool restoreOpeningSection(Section section) noexcept
    {
        return transferSection(openingVariants[(size_t)activeVariant],
                               variants[(size_t)activeVariant],section);
    }

    // Swap both drafts but retain the active bank selection. Nothing becomes
    // audible until apply(), and cancel() can restore the previous state.
    void swapVariants() noexcept
    {
        std::swap(variants[0],variants[1]);
    }

    // Restore one bank to its values when this dialog opened. No DSP change.
    bool restoreOpeningVariant(int bank) noexcept
    {
        if(bank<0 || bank>=2)return false;
        variants[(size_t)bank]=openingVariants[(size_t)bank];
        return true;
    }

    void copy() { clipboard = pending(); }
    bool paste() noexcept
    {
        if (!clipboard) return false;
        setPending(*clipboard);
        return true;
    }
    static bool canPaste() noexcept { return clipboard.has_value(); }
    static void clearClipboardForTests() noexcept { clipboard.reset(); }
private:
    static bool transferSection(const Settings& source,Settings& destination,
                                Section section) noexcept
    {
        switch(section)
        {
            case Section::Input:
                destination.inputGainDb=source.inputGainDb;
                destination.highPassHz=source.highPassHz;
                return true;
            case Section::Gate:
                destination.gateEnabled=source.gateEnabled;
                destination.gateThresholdDb=source.gateThresholdDb;
                return true;
            case Section::Compressor:
                destination.compressorEnabled=source.compressorEnabled;
                destination.compressorThresholdDb=source.compressorThresholdDb;
                destination.compressorRatio=source.compressorRatio;
                return true;
            case Section::Equalizer:
                destination.eqEnabled=source.eqEnabled;
                destination.eqLowDb=source.eqLowDb;
                destination.eqMidDb=source.eqMidDb;
                destination.eqHighDb=source.eqHighDb;
                return true;
            default: return false;
        }
    }
    AudioEngine& engine;
    int trackIndex;
    std::array<Settings,2> variants{};
    std::array<Settings,2> openingVariants{};
    int activeVariant = 0;
    inline static std::optional<Settings> clipboard{};
};
}
