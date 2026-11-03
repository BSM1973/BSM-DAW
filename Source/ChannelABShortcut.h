#pragma once

// CANAL dialog shortcuts are local, never shared with the DAW transport.
// Numeric text editors retain their native keyboard handling.
namespace LibertyChannelABShortcut
{
enum class Action { none, undo, redo, consumeText, selectA, selectB };

// Only a genuine bank change should create an Undo step.
constexpr bool selectBankWouldChange(int current,int target) noexcept
{
    return current>=0 && current<2 && target>=0 && target<2
        && current!=target;
}

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
