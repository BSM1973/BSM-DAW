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
    // Each stereo side has independent level, hold, decay and clip state.
    StereoDisplay stereo;
    advance(stereo,StereoPeaks{{0.7f,0.18f}});
    assert(stereo.channel[0].level==0.7f);
    assert(stereo.channel[1].level==0.18f);
    assert(stereo.heldPeak()==0.7f);
    assert(!stereo.clipped());

    advance(stereo,StereoPeaks{{0.1f,1.15f}});
    assert(stereo.channel[0].heldPeak==0.7f);
    assert(stereo.channel[1].heldPeak==1.15f);
    assert(stereo.clipped());
    assert(stereo.heldPeak()==1.15f);
    assert(stereo.channel[0].level!=stereo.channel[1].level);
    for(int frame=0;frame<40;++frame)
        advance(stereo,StereoPeaks{{0.f,0.f}});
    assert(stereo.clipped()); // Overload survives the end of the signal.
    stereo.resetPeak();
    assert(!stereo.clipped());
    assert(stereo.channel[0].heldPeak==0.f);
    assert(stereo.channel[1].heldPeak==0.f);
    advance(stereo,StereoPeaks{{1.01f,0.08f}});
    assert(stereo.channel[0].clipped && !stereo.channel[1].clipped);
    assert(stereo.clipped());
    stereo.resetPeak();
    advance(stereo,StereoPeaks{{0.25f,0.f}});
    assert(stereo.channel[0].heldPeak==0.25f);
    assert(stereo.channel[1].heldPeak==0.f && !stereo.clipped());
    assert(meterPosition(stereo.channel[0].level)>=meterPosition(stereo.channel[1].level));

    // Weighted RMS across unequal device blocks must not average peak values.
    std::atomic<std::uint64_t> rmsL{0},rmsR{0};
    publishRms(rmsL,0.5f,100);
    publishRms(rmsL,0.f,300);
    // sqrt((100 * 0.5^2 + 300 * 0^2)/400) == 0.25
    assert(std::abs(consumeRms(rmsL)-0.25f)<0.0001f);
    assert(consumeRms(rmsL)==0.f);
    publishRms(rmsL,1.f,50);
    publishRms(rmsR,0.125f,200);
    assert(std::abs(consumeRms(rmsL)-1.f)<0.0001f);
    assert(std::abs(consumeRms(rmsR)-0.125f)<0.0001f);
    publishRms(rmsL,-1.f,128);
    publishRms(rmsL,std::numeric_limits<float>::quiet_NaN(),128);
    publishRms(rmsL,std::numeric_limits<float>::infinity(),128);
    assert(consumeRms(rmsL)==0.f);
    publishRms(rmsL,0.6f,0);
    assert(consumeRms(rmsL)==0.f);
    publishRms(rmsL,0.001f,512);
    assert(std::abs(toDb(consumeRms(rmsL))+60.f)<0.001f);

    StereoDisplay rmsStereo;
    advance(rmsStereo,StereoPeaks{{0.9f,0.2f}},
            StereoPeaks{{0.25f,0.08f}});
    assert(rmsStereo.channel[0].level==0.9f);
    assert(rmsStereo.channel[0].rms==0.25f);
    assert(rmsStereo.channel[1].rms==0.08f);
    assert(!rmsStereo.clipped());
    advance(rmsStereo,StereoPeaks{{0.f,1.2f}},
            StereoPeaks{{0.f,0.35f}});
    assert(rmsStereo.channel[0].rms<0.25f);
    assert(rmsStereo.channel[0].rms>0.f);
    assert(rmsStereo.channel[1].rms==0.35f);
    assert(rmsStereo.channel[1].clipped);
    assert(rmsStereo.clipped()); // RMS does not change sample-peak CLIP.
    rmsStereo.resetPeak();
    assert(!rmsStereo.clipped());
    assert(rmsStereo.channel[1].rms==0.35f); // Reset affects only peak hold.
    advance(rmsStereo,StereoPeaks{{0.f,0.f}},
            StereoPeaks{{0.f,0.f}});
    assert(rmsStereo.channel[1].rms<0.35f);
    assert(std::isfinite(rmsStereo.channel[1].rms));

    std::cout<<"MIXER meter: stereo peaks, weighted RMS, clip hold and reset OK\n";
}
