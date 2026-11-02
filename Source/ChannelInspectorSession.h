#pragma once
#include "AudioEngine.h"
#include <array>
#include <optional>

// A CANAL dialog edits a pending copy. Only apply() changes the audio engine.
// The clipboard is internal to Liberty and is shared by all CANAL dialogs.
namespace LibertyChannelInspectorState
{
class Session final
{
public:
    using Settings = AudioEngine::ChannelProcessingSettings;
    Session(AudioEngine& source, int track)
      : engine(source), trackIndex(track)
    {
        const auto memory=engine.getChannelABState(trackIndex);
        variants=memory.variants;
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
    AudioEngine& engine;
    int trackIndex;
    std::array<Settings,2> variants{};
    int activeVariant = 0;
    inline static std::optional<Settings> clipboard{};
};
}
