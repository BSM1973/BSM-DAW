#include "../Source/MixConsoleMeter.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

using namespace LibertyMixConsoleMeter;

int main()
{
    std::atomic<float> audioPeak{0.f};
    publish(audioPeak,0.2f);
    publish(audioPeak,0.7f);
    publish(audioPeak,0.1f);
    assert(std::abs(consume(audioPeak)-0.7f)<0.0001f);
    assert(consume(audioPeak)==0.f);
    publish(audioPeak,-1.f);
    publish(audioPeak,std::numeric_limits<float>::quiet_NaN());
    publish(audioPeak,std::numeric_limits<float>::infinity());
    assert(consume(audioPeak)==0.f);

    assert(meterPosition(0.f)==0.f);
    assert(std::abs(meterPosition(1.f)-60.f/66.f)<0.0001f);
    assert(meterPosition(2.5f)==1.f);
    assert(meterPosition(std::numeric_limits<float>::quiet_NaN())==0.f);
    assert(std::abs(toDb(0.5f)+6.0206f)<0.002f);
    assert(toDb(0.f)==-60.f);

    Display channel;
    advance(channel,0.5f);
    assert(channel.level==0.5f);
    assert(channel.heldPeak==0.5f && !channel.clipped);
    assert(channel.holdFrames==36);
    for(int frame=0;frame<35;++frame)
        advance(channel,0.f);
    assert(channel.heldPeak==0.5f && channel.holdFrames==1);
    advance(channel,0.f);
    assert(channel.heldPeak==0.5f && channel.holdFrames==0);
    advance(channel,0.f);
    assert(channel.heldPeak<0.5f && channel.heldPeak>0.f);
    advance(channel,1.1f);
    assert(channel.clipped && channel.heldPeak>=1.1f);
    assert(channel.level>=1.1f);
    channel.resetPeak();
    assert(!channel.clipped && channel.heldPeak==0.f);
    assert(channel.level>0.f); // Reset indication, never alter the audio.
    advance(channel,0.f);
    assert(!channel.clipped && channel.heldPeak==0.f);
    advance(channel,0.2f);
    assert(channel.heldPeak==0.2f && !channel.clipped);
    advance(channel,std::numeric_limits<float>::quiet_NaN());
    assert(std::isfinite(channel.level) && std::isfinite(channel.heldPeak));

    // Independent tracks maintain their own peak holds.
    Display instrument;
    advance(instrument,1.25f);
    assert(instrument.clipped && !channel.clipped);
    instrument.resetPeak();
    assert(!instrument.clipped && channel.heldPeak==0.2f);
    std::cout<<"MIXER meter: atomic max, sample dBFS, peak hold, sticky clip, reset, NaN OK\n";
}
