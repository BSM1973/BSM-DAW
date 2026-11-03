#pragma once

// Pure keyboard decision for CANAL's local A/B history.
// The focused JUCE TextEditor always receives the key first; if it lets
// Cmd+Z bubble to CANAL, consume it without touching the A/B draft.
namespace LibertyChannelABShortcut
{
enum class Action { none, undo, redo, consumeText };

constexpr Action resolve(int keyCode,bool command,bool shift,bool alt,
                         bool textEditing) noexcept
{
    if(!command || alt || (keyCode!='z' && keyCode!='Z'))
        return Action::none;
    if(textEditing)
        return Action::consumeText;
    return shift?Action::redo:Action::undo;
}
}
