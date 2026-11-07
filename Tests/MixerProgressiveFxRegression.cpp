#include "../Source/MixerProgressiveFx.h"
#include <array>
#include <cassert>
#include <iostream>
int main()
{
    using namespace LibertyMixerProgressiveFx;
    std::array<bool,slotLimit> state{};
    assert(revealedCount(state)==1);
    for(int index=0;index<slotLimit;++index)
    {
        state[(size_t)index]=true;
        assert(revealedCount(state)==std::min(slotLimit,index+2));
    }
    state.fill(false);
    state[3]=true; // Sparse old project: no effect index may be shifted.
    assert(revealedCount(state)==5);
    state[3]=false;
    assert(revealedCount(state)==1);
    state[7]=true;
    assert(revealedCount(state)==8);
    assert(windowStart(8,3,99)==5);
    assert(windowStart(2,2,5)==0);
    assert(windowStart(1,1,-10)==0);
    std::cout<<"Progressive FX: 1 initial slot, next slot on insert, max 8, stable IDs OK\n";
}
