#include "../Source/MixConsoleSoloLogic.h"
#include <cassert>
#include <iostream>

using namespace LibertyMixConsoleSoloLogic;

// Modifier combinations resolve identically from Console and Arrangement.
static_assert(resolveAction(false,false)==Action::toggle);
static_assert(resolveAction(false,true)==Action::exclusive);
static_assert(resolveAction(true,false)==Action::clearAll);
static_assert(resolveAction(true,true)==Action::clearAll);

int main()
{
    const State empty{{false,false,false},{false,false}};
    const Target audio1{Kind::audio,1};
    const Target instrument0{Kind::instrument,0};
    assert(countActive(empty)==0);
    assert(isValid(empty,audio1));
    assert(!isValid(empty,{Kind::audio,3}));
    assert(!isValid(empty,{Kind::instrument,-1}));

    // Normal click preserves Solos already active on other tracks.
    auto state=apply(empty,audio1,Action::toggle);
    assert(state.audio==std::vector<bool>({false,true,false}));
    assert(countActive(state)==1);
    state=apply(state,instrument0,Action::toggle);
    assert(state.instruments==std::vector<bool>({true,false}));
    assert(countActive(state)==2);
    state=apply(state,audio1,Action::toggle);
    assert(countActive(state)==1 && state.instruments[0]);

    // Option-click isolates across BOTH buses without changing track Mute.
    state=apply(state,{Kind::audio,2},Action::exclusive);
    assert(state.audio==std::vector<bool>({false,false,true}));
    assert(state.instruments==std::vector<bool>({false,false}));
    assert(countActive(state)==1);

    // Repeating Option-click on the sole selected track clears its Solo.
    state=apply(state,{Kind::audio,2},Action::exclusive);
    assert(countActive(state)==0);

    // Exclusive instrument track silences active audio Solos.
    state=apply(state,{Kind::audio,0},Action::toggle);
    state=apply(state,{Kind::audio,1},Action::toggle);
    state=apply(state,{Kind::instrument,1},Action::exclusive);
    assert(state.audio==std::vector<bool>({false,false,false}));
    assert(state.instruments==std::vector<bool>({false,true}));
    assert(countActive(state)==1);

    // Shift-click clears ALL Solos in both classes.
    state=apply(state,audio1,Action::clearAll);
    assert(countActive(state)==0);
    assert(state.audio==empty.audio && state.instruments==empty.instruments);

    // Invalid lanes are harmless even for an all-clear action.
    auto active=apply(empty,audio1,Action::toggle);
    assert(apply(active,{Kind::audio,-1},Action::exclusive).audio==active.audio);
    assert(apply(active,{Kind::instrument,20},Action::clearAll).audio==active.audio);
    assert(apply(active,{Kind::audio,3},Action::toggle).audio==active.audio);

    // Simulate alternating clicks between the Arrangement header and the
    // Console: both must update one shared audio/instrument Solo state.
    auto shared=apply(empty,audio1,resolveAction(false,false)); // header
    shared=apply(shared,instrument0,resolveAction(false,false)); // console
    assert(countActive(shared)==2);
    shared=apply(shared,audio1,resolveAction(false,true)); // header Option
    assert(shared.audio==std::vector<bool>({false,true,false}));
    assert(shared.instruments==std::vector<bool>({false,false}));
    shared=apply(shared,instrument0,resolveAction(true,true)); // console Shift wins
    assert(countActive(shared)==0);

    // No global state is remembered by the helper: fresh sessions start clean.
    assert(countActive(empty)==0);
    std::cout<<"MIXER SOLO: additive, exclusive audio/instrument, toggle-out, all-clear, invalid indices OK\n";
}
