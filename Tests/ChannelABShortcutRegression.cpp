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

int main()
{
    assert(resolve('z',true,false,false,false)==Action::undo);
    assert(resolve('z',true,true,false,false)==Action::redo);
    assert(resolve('z',true,false,false,true)==Action::consumeText);
    assert(resolve('z',true,true,false,true)==Action::consumeText);
    assert(resolve('z',true,false,true,false)==Action::none);
    std::cout<<"CANAL A/B shortcuts: focused text editor remains protected\n";
}
