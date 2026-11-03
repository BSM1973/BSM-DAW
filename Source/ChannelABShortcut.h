#pragma once

// CANAL dialog shortcuts are local, never shared with the DAW transport.
// Numeric text editors retain their native keyboard handling.
namespace LibertyChannelABShortcut
{
enum class Action { none, undo, redo, consumeText, selectA, selectB };

constexpr Action resolve(int keyCode,bool command,bool shift,bool alt,
                         bool textEditing) noexcept
{
    if(!command || alt)return Action::none;

    if(keyCode=='z' || keyCode=='Z')
        return textEditing?Action::consumeText:(shift?Action::redo:Action::undo);

    // Cmd+1/2 select a pending bank directly; never intercept text entry.
    if(!textEditing && !shift && keyCode=='1')return Action::selectA;
    if(!textEditing && !shift && keyCode=='2')return Action::selectB;
    return Action::none;
}
}
