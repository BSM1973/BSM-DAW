#include "PerformLivePro.h"
#include <cassert>
#include <cmath>
int main()
{
    using namespace LibertyPerformLive;
    assert(barSamples(120,4,4,48000)==96000);
    assert(nextFollowSample(125,Follow{1,2},120,4,4,48000)==192125);
    std::uint32_t seed=1;
    assert(followScene(7,1,seed)==0);
    assert(followScene(0,2,seed)==7);
    assert(followScene(3,4,seed)==-1);
    int r=followScene(4,3,seed);assert(r>=0&&r<8&&r!=4);
    assert(std::abs(crossfadeIn(256,512)-0.5f)<0.001f);
    assert(std::abs(crossfadeOut(256,512)-0.5f)<0.001f);
    assert(crossfadeIn(123,0)==1.f);
    assert(transitionSamples(125,48000)==6000);
    assert(matches(Binding{1,1,74},1,1,74));
    assert(!matches(Binding{2,1,74},1,1,74));
    assert(!matches(Binding{0,2,64},1,1,64));
    const auto m=unpackMidi(packMidi(2,4,64,120,4321));
    assert(m.kind==2&&m.channel==4&&m.number==64&&m.value==120&&m.serial==4321);
    assert(macroFromMidi(127)==200&&macroFromMidi(0)==0);
    assert(macroGain(100)==1.f&&macroGain(200)==2.f);
    assert(wrapSource(103,2,16)==9);
    return 0;
}
