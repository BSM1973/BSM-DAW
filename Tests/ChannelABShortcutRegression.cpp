#include "../Source/ChannelABShortcut.h"
#include <cassert>
#include <iostream>

using LibertyChannelABShortcut::Action;
using LibertyChannelABShortcut::resolve;

static_assert(resolve('z',true,false,false,false)==Action::undo);
static_assert(resolve('Z',true,true,false,false)==Action::redo);
static_assert(resolve('z',true,true,false,false)==Action::redo);
static_assert(resolve('z',false,false,false,false)==Action::none);
static_assert(resolve('x',true,false,false,false)==Action::none);
static_assert(resolve('z',true,false,true,false)==Action::none);
static_assert(resolve('z',true,false,false,true)==Action::consumeText);
static_assert(resolve('z',true,true,false,true)==Action::consumeText);
static_assert(resolve('Z',true,true,false,true)==Action::consumeText);
static_assert(resolve('z',false,false,false,true)==Action::none);
// Direct bank selection is intentionally unavailable during text entry.
static_assert(resolve('1',true,false,false,false)==Action::selectA);
static_assert(resolve('2',true,false,false,false)==Action::selectB);
static_assert(resolve('1',true,false,false,true)==Action::none);
static_assert(resolve('2',true,false,false,true)==Action::none);
// Do not steal global keyboard shortcuts that differ by modifiers.
static_assert(resolve('1',false,false,false,false)==Action::none);
static_assert(resolve('2',true,true,false,false)==Action::none);
static_assert(resolve('2',true,false,true,false)==Action::none);
static_assert(resolve('3',true,false,false,false)==Action::none);

int main()
{
    assert(resolve('z',true,false,false,false)==Action::undo);
    assert(resolve('z',true,true,false,false)==Action::redo);
    assert(resolve('z',true,false,false,true)==Action::consumeText);
    assert(resolve('z',true,true,false,true)==Action::consumeText);
    assert(resolve('z',true,false,true,false)==Action::none);
    assert(resolve('1',true,false,false,false)==Action::selectA);
    assert(resolve('2',true,false,false,false)==Action::selectB);
    assert(resolve('1',true,false,false,true)==Action::none);
    assert(resolve('2',true,true,false,false)==Action::none);
    std::cout<<"CANAL A/B shortcuts: safe Undo/Redo and direct A/B selection OK\n";
}
