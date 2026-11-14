#include "../Source/MixerProgressiveFx.h"
#include <array>
#include <cassert>
#include <iostream>
int main()
{
    using namespace LibertyMixerProgressiveFx;
    // Loading a synth on a collapsed instrument strip automatically
    // opens its separate FX rack. Repeated sync must respect manual collapse.
    static_assert(revealOnInstrumentLoad(false,true),"synth load must expose FX 1");
    static_assert(!revealOnInstrumentLoad(true,true),"no repeated forced expansion");
    static_assert(!revealOnInstrumentLoad(false,false),"empty instrument is unchanged");
    static_assert(!revealOnInstrumentLoad(true,false),"unload doesn't force expansion");
    std::array<bool,slotLimit> state{};
    assert(activeCount(state)==0);
    assert(revealedCount(state)==1);
    for(int index=0;index<slotLimit;++index)
    {
        state[(size_t)index]=true;
        assert(activeCount(state)==index+1);
        assert(revealedCount(state)==std::min(slotLimit,index+2));
    }
    state.fill(false);
    state[3]=true; // Sparse old project: no effect index may be shifted.
    assert(revealedCount(state)==5);
    state[3]=false;
    assert(revealedCount(state)==1);
    state[7]=true;
    assert(revealedCount(state)==8);
    assert(activeCount(state)==1); // Sparse last slot stays at slot 8.
    assert(windowStart(8,3,99)==5);
    assert(windowStart(2,2,5)==0);
    assert(windowStart(1,1,-10)==0);
    std::cout<<"Progressive FX: 1 initial slot, next slot on insert, max 8, stable IDs OK\n";
}
