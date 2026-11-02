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
        // Both variants initially reflect the same actual engine settings.
        variants.fill(engine.getChannelProcessingSettings(trackIndex));
    }

    const Settings& pending() const noexcept { return variants[(size_t)activeVariant]; }
    void setPending(const Settings& settings) noexcept { variants[(size_t)activeVariant] = settings; }
    void reset() noexcept { variants[(size_t)activeVariant] = Settings{}; }
    void cancel() noexcept
    {
        variants.fill(engine.getChannelProcessingSettings(trackIndex));
        activeVariant = 0;
    }
    void apply() noexcept { engine.setChannelProcessingSettings(trackIndex, pending()); }

    int variantIndex() const noexcept { return activeVariant; }
    void switchVariant() noexcept { activeVariant = 1 - activeVariant; }

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
