#include "PerformSessionTiming.h"
#include <cassert>
int main()
{
    using namespace LibertyPerformTiming;
    assert(quantumSamples(0,120,4,4,48000)==1);
    assert(quantumSamples(1,120,4,4,48000)==24000);
    assert(quantumSamples(2,120,4,4,48000)==96000);
    assert(nextBoundary(0,96000)==0);
    assert(nextBoundary(1000,96000)==96000);
    assert(nextBoundary(96000,96000)==96000);
    assert(ticksToSamples(960,120,48000)==24000);
    assert(ticksToSamples(3840,120,48000)==96000);
    assert(instrumentForLogicalTrack(4,4,2,3)==0);
    assert(instrumentForLogicalTrack(5,4,2,3)==1);
    assert(instrumentForLogicalTrack(6,4,2,3)==0);
    assert(instrumentForLogicalTrack(4,4,1,0)==-1);
    return 0;
}
