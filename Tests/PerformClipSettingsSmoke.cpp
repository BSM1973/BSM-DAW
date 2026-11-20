#include "PerformClipSettings.h"
#include <cassert>
#include <cmath>
int main()
{
    using namespace LibertyPerformClip;
    const Settings defaults;
    assert(defaults.loop && defaults.quantization == -1 && defaults.launchMode == 0);
    assert(std::abs(linearGain(0) - 1.0f) < 0.0001f);
    assert(std::abs(linearGain(6) - 1.995f) < 0.01f);
    assert(validated(Settings{100.f,100,-3.f,3.f,true,99,99,3}).transpose == 24);
    assert(notePitch(120,24)==127 && notePitch(5,-24)==0);
    assert(launchQuantization(2,-1)==2 && launchQuantization(2,0)==0);
    assert(launchedScene(3,3,1)==-1 && launchedScene(2,3,1)==3);
    assert(launchedScene(3,3,0)==3);
    assert(midiLengthTicks(0,2,4,4)==7680);
    assert(midiLengthTicks(1234,0,4,4)==1234);
    const auto trimmed=audioSlice(48000,Settings{0,0,0.25f,0.75f});
    assert(trimmed.first==12000 && trimmed.length==24000);
    assert(audioSlice(10,Settings{0,0,0.99f,1.f}).length>=1);
    assert(audioSlice(0,defaults).length==0);
    return 0;
}
