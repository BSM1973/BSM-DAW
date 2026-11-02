#pragma once
#include "AudioEngine.h"
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
      : engine(source), trackIndex(track),
        draft(source.getChannelProcessingSettings(track)) {}

    const Settings& pending() const noexcept { return draft; }
    void setPending(const Settings& settings) noexcept { draft = settings; }
    void reset() noexcept { draft = Settings{}; }
    void cancel() noexcept { draft = engine.getChannelProcessingSettings(trackIndex); }
    void apply() noexcept { engine.setChannelProcessingSettings(trackIndex, draft); }

    void copy() { clipboard = draft; }
    bool paste() noexcept
    {
        if (!clipboard) return false;
        draft = *clipboard;
        return true;
    }
    static bool canPaste() noexcept { return clipboard.has_value(); }
    static void clearClipboardForTests() noexcept { clipboard.reset(); }
private:
    AudioEngine& engine;
    int trackIndex;
    Settings draft;
    inline static std::optional<Settings> clipboard{};
};
}
