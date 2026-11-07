#include "../Source/MixConsoleMeter.h"
#include "../Source/MixerBusRouting.h"
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

    // Identical peak amplitudes can have completely different RMS energy.
    // Use actual PCM samples, not the instantaneous peak as an RMS proxy.
    std::atomic<std::uint64_t> pulseEnergy{0},steadyEnergy{0};
    const float pulse[]={1.f,0.f,0.f,0.f};
    const float steady[]={1.f,1.f,1.f,1.f};
    publishRmsSamples(pulseEnergy,pulse,4);
    publishRmsSamples(steadyEnergy,steady,4);
    assert(std::abs(consumeRms(pulseEnergy)-0.5f)<0.0001f);
    assert(std::abs(consumeRms(steadyEnergy)-1.f)<0.0001f);
    publishRmsSamples(pulseEnergy,nullptr,4);
    assert(consumeRms(pulseEnergy)==0.f);

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

    // Numeric readout uses peak hold or the most recent measured RMS;
    // the RMS visual bar can continue releasing without changing the number.
    auto mode=ReadoutMode::peak;
    assert(toggleReadoutMode(mode)==ReadoutMode::rms);
    mode=toggleReadoutMode(mode);
    assert(mode==ReadoutMode::rms);
    assert(toggleReadoutMode(mode)==ReadoutMode::peak);
    StereoDisplay readout;
    advance(readout,StereoPeaks{{0.8f,0.3f}},
            StereoPeaks{{0.25f,0.1f}});
    assert(std::abs(readoutAmplitude(readout,ReadoutMode::peak)-0.8f)<0.0001f);
    assert(std::abs(readoutAmplitude(readout,ReadoutMode::rms)-0.25f)<0.0001f);
    assert(std::abs(toDb(readoutAmplitude(readout,ReadoutMode::rms))+12.0412f)<0.002f);
    advance(readout,StereoPeaks{{0.f,0.f}},
            StereoPeaks{{0.f,0.f}});
    assert(readoutAmplitude(readout,ReadoutMode::peak)==0.8f);
    assert(readoutAmplitude(readout,ReadoutMode::rms)==0.f);
    assert(readout.channel[0].rms>0.f); // Visual decay independent of exact readout.
    advance(readout,StereoPeaks{{0.4f,1.1f}},
            StereoPeaks{{0.1f,0.32f}});
    assert(readout.clipped());
    assert(std::abs(readoutAmplitude(readout,ReadoutMode::rms)-0.32f)<0.0001f);
    readout.resetPeak();
    assert(!readout.clipped());
    assert(readoutAmplitude(readout,ReadoutMode::peak)==0.f);
    assert(std::abs(readoutAmplitude(readout,ReadoutMode::rms)-0.32f)<0.0001f);
    assert(readoutAmplitude(StereoDisplay{},ReadoutMode::peak)==0.f);
    assert(readoutAmplitude(StereoDisplay{},ReadoutMode::rms)==0.f);
    // Separate digital readings for left and right in both modes.
    StereoDisplay split;
    advance(split,StereoPeaks{{0.75f,0.25f}},
            StereoPeaks{{0.125f,0.0625f}});
    assert(std::abs(channelReadoutAmplitude(split,ReadoutMode::peak,0)-0.75f)<0.0001f);
    assert(std::abs(channelReadoutAmplitude(split,ReadoutMode::peak,1)-0.25f)<0.0001f);
    assert(std::abs(channelReadoutAmplitude(split,ReadoutMode::rms,0)-0.125f)<0.0001f);
    assert(std::abs(channelReadoutAmplitude(split,ReadoutMode::rms,1)-0.0625f)<0.0001f);
    assert(std::abs(toDb(channelReadoutAmplitude(split,ReadoutMode::peak,0))+2.4988f)<0.002f);
    assert(std::abs(toDb(channelReadoutAmplitude(split,ReadoutMode::peak,1))+12.0412f)<0.002f);
    assert(readoutAmplitude(split,ReadoutMode::peak)==
           channelReadoutAmplitude(split,ReadoutMode::peak,0));
    assert(channelReadoutAmplitude(split,ReadoutMode::peak,-1)==0.f);
    assert(channelReadoutAmplitude(split,ReadoutMode::peak,2)==0.f);

    // A transient in R never rewrites the held PEAK in L. RMS numbers
    // follow the measured block, not the decaying cyan bars.
    advance(split,StereoPeaks{{0.f,1.1f}},
            StereoPeaks{{0.f,0.32f}});
    assert(std::abs(channelReadoutAmplitude(split,ReadoutMode::peak,0)-0.75f)<0.0001f);
    assert(std::abs(channelReadoutAmplitude(split,ReadoutMode::peak,1)-1.1f)<0.0001f);
    assert(channelReadoutAmplitude(split,ReadoutMode::rms,0)==0.f);
    assert(std::abs(channelReadoutAmplitude(split,ReadoutMode::rms,1)-0.32f)<0.0001f);
    assert(split.channel[0].rms>0.f);
    assert(split.channel[1].clipped && !split.channel[0].clipped);
    split.resetPeak();
    assert(channelReadoutAmplitude(split,ReadoutMode::peak,0)==0.f);
    assert(channelReadoutAmplitude(split,ReadoutMode::peak,1)==0.f);
    assert(channelReadoutAmplitude(split,ReadoutMode::rms,1)>0.f);
    assert(!split.clipped());

    // Numeric / bar hit regions are disjoint and never steal the fader.
    const int top=100,bottom=340;
    assert(resolveMeterClick(4,bottom+4,top,bottom)==MeterClick::toggleDigital);
    assert(resolveMeterClick(43,bottom+35,top,bottom)==MeterClick::toggleDigital);
    assert(resolveMeterClick(18,bottom+30,top,bottom)==MeterClick::toggleDigital);
    assert(resolveMeterClick(18,top+10,top,bottom)==MeterClick::resetPeaks);
    assert(resolveMeterClick(18,bottom+1,top,bottom)==MeterClick::resetPeaks);
    assert(resolveMeterClick(18,bottom+2,top,bottom)==MeterClick::none);
    assert(resolveMeterClick(18,bottom+3,top,bottom)==MeterClick::none);
    assert(resolveMeterClick(18,bottom+36,top,bottom)==MeterClick::none);
    assert(resolveMeterClick(3,top+5,top,bottom)==MeterClick::none);
    assert(resolveMeterClick(44,top+5,top,bottom)==MeterClick::none);
    assert(resolveMeterClick(44,bottom+5,top,bottom)==MeterClick::none);
    assert(resolveMeterClick(10,top+10,top,top+39)==MeterClick::none);
    assert(resolveMeterClick(10,top+45,top,top+39)==MeterClick::none);

    // STEREO BALANCE: sample-weighted RMS difference, not a pan change.
    // The number identifies the louder side; the pointer saturates at 24 dB.
    auto equal=evaluateStereoBalance(0.25f,0.25f);
    assert(equal.side==BalanceSide::center&&equal.differenceDb==0.f);
    assert(equal.position==0.f);
    const auto leftLouder=evaluateStereoBalance(0.5f,0.25f);
    assert(leftLouder.side==BalanceSide::left);
    assert(std::abs(leftLouder.differenceDb-6.0206f)<0.002f);
    assert(leftLouder.position<0.f&&leftLouder.position>-1.f);
    const auto rightLouder=evaluateStereoBalance(0.25f,0.5f);
    assert(rightLouder.side==BalanceSide::right);
    assert(std::abs(rightLouder.differenceDb-6.0206f)<0.002f);
    assert(rightLouder.position>0.f&&rightLouder.position<1.f);
    assert(std::abs(leftLouder.position+rightLouder.position)<0.0001f);
    assert(evaluateStereoBalance(0.f,0.f).side==BalanceSide::silent);
    assert(evaluateStereoBalance(0.0005f,0.0004f).side==BalanceSide::silent);
    assert(evaluateStereoBalance(0.5f,0.f).side==BalanceSide::left);
    assert(evaluateStereoBalance(0.f,0.5f).side==BalanceSide::right);
    assert(evaluateStereoBalance(1.f,0.f).differenceDb==60.f);
    assert(evaluateStereoBalance(1.f,0.f).position==-1.f);
    assert(evaluateStereoBalance(0.f,1.f).position==1.f);
    assert(evaluateStereoBalance(0.3f,0.299f).side==BalanceSide::center);
    assert(evaluateStereoBalance(std::numeric_limits<float>::quiet_NaN(),0.4f)
           .side==BalanceSide::right);
    assert(evaluateStereoBalance(0.4f,std::numeric_limits<float>::infinity())
           .side==BalanceSide::left);
    assert(std::isfinite(evaluateStereoBalance(0.f,1.f).differenceDb));

    StereoDisplay balanceDisplay;
    advance(balanceDisplay,StereoPeaks{{0.7f,0.4f}},
            StereoPeaks{{0.4f,0.2f}});
    assert(stereoRmsBalance(balanceDisplay).side==BalanceSide::left);
    assert(std::abs(stereoRmsBalance(balanceDisplay).differenceDb-6.0206f)<0.002f);
    // Switch PEAK/RMS or clear CLIP: neither changes the RMS balance.
    const auto previousBalance=stereoRmsBalance(balanceDisplay);
    mode=toggleReadoutMode(mode);
    balanceDisplay.resetPeak();
    assert(stereoRmsBalance(balanceDisplay).side==previousBalance.side);
    assert(stereoRmsBalance(balanceDisplay).differenceDb==previousBalance.differenceDb);
    advance(balanceDisplay,StereoPeaks{{0.f,0.f}},StereoPeaks{{0.f,0.f}});
    assert(stereoRmsBalance(balanceDisplay).side==BalanceSide::left);
    for(int frame=0;frame<100;++frame)
        advance(balanceDisplay,StereoPeaks{{0.f,0.f}},StereoPeaks{{0.f,0.f}});
    assert(stereoRmsBalance(balanceDisplay).side==BalanceSide::silent);


    // Stereo correlation is sample-pair phase, not a balance estimate.
    StereoCorrelationAccumulator correlation;
    const float same[4] {0.5f,-0.5f,0.5f,-0.5f};
    const float opposite[4] {-0.5f,0.5f,-0.5f,0.5f};
    publishStereoCorrelationSamples(correlation,same,same,4);
    auto correlationReading=consumeStereoCorrelation(correlation);
    assert(correlationReading.valid&&std::abs(correlationReading.value-1.f)<0.0001f);
    assert(!consumeStereoCorrelation(correlation).valid);
    publishStereoCorrelationSamples(correlation,same,opposite,4);
    correlationReading=consumeStereoCorrelation(correlation);
    assert(correlationReading.valid&&std::abs(correlationReading.value+1.f)<0.0001f);
    const float leftQ[4] {1.f,0.f,-1.f,0.f};
    const float rightQ[4] {0.f,1.f,0.f,-1.f};
    publishStereoCorrelationSamples(correlation,leftQ,rightQ,4);
    correlationReading=consumeStereoCorrelation(correlation);
    assert(correlationReading.valid&&std::abs(correlationReading.value)<0.0001f);
    const float strong[4] {1.f,1.f,1.f,1.f};
    const float quiet[4] {0.25f,0.25f,0.25f,0.25f};
    const float inverted[4] {-0.25f,-0.25f,-0.25f,-0.25f};
    publishStereoCorrelationSamples(correlation,strong,strong,4);
    publishStereoCorrelationSamples(correlation,quiet,inverted,4);
    correlationReading=consumeStereoCorrelation(correlation);
    assert(correlationReading.valid);
    assert(std::abs(correlationReading.value-(1.f-0.0625f)/(1.f+0.0625f))<0.001f);
    const float silence[4] {0.f,0.f,0.f,0.f};
    publishStereoCorrelationSamples(correlation,silence,silence,4);
    assert(!consumeStereoCorrelation(correlation).valid);
    publishStereoCorrelationSamples(correlation,strong,silence,4);
    assert(!consumeStereoCorrelation(correlation).valid);
    publishStereoCorrelationSamples(correlation,strong,nullptr,4);
    assert(!consumeStereoCorrelation(correlation).valid);
    const float noise[4] {0.0001f,-0.0001f,0.0001f,-0.0001f};
    publishStereoCorrelationSamples(correlation,noise,noise,4);
    assert(!consumeStereoCorrelation(correlation).valid);
    const float nonFinite[4] {std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity(),0.f,0.f};
    publishStereoCorrelationSamples(correlation,nonFinite,strong,4);
    assert(!consumeStereoCorrelation(correlation).valid);
    StereoCorrelationDisplay corrDisplay;
    corrDisplay.advance({1.f,true});
    assert(corrDisplay.valid&&corrDisplay.value==1.f);
    corrDisplay.advance({-1.f,true});
    assert(corrDisplay.valid&&corrDisplay.value<0.f);
    corrDisplay.advance({1.f,true});
    assert(corrDisplay.value<1.f);
    corrDisplay.advance({});
    assert(!corrDisplay.valid&&corrDisplay.value==0.f);
    assert(same[0]==0.5f&&opposite[0]==-0.5f);

    // A right-click meter menu may clear one channel's held clips or all
    // channels together. RMS/instantaneous levels stay unchanged.
    std::array<StereoDisplay,3> meterBank{};
    for(auto& meter:meterBank)
        advance(meter,StereoPeaks{{1.2f,0.5f}},StereoPeaks{{0.4f,0.2f}});
    const float measuredBefore=meterBank[0].channel[0].measuredRms;
    meterBank[1].resetPeak();
    assert(meterBank[0].clipped()&&!meterBank[1].clipped()&&meterBank[2].clipped());
    for(auto& meter:meterBank)meter.resetPeak();
    for(const auto& meter:meterBank)
    {
        assert(!meter.clipped());
        assert(meter.channel[0].heldPeak==0.f);
        assert(meter.channel[1].heldPeak==0.f);
    }
    assert(meterBank[0].channel[0].measuredRms==measuredBefore);

    using namespace LibertyMixerBusRouting;
    assert(destination(-1,3)==-1&&destination(2,3)==2);
    assert(destination(3,3)==-1&&destination(0,0)==-1);
    assert(destination(8,8)==-1);
    assert(std::abs(leftGain(1.f,0.f)-1.f)<0.0001f);
    assert(std::abs(rightGain(1.f,0.f)-1.f)<0.0001f);
    assert(std::abs(leftGain(0.5f,1.f))<0.0001f);
    assert(std::abs(rightGain(0.5f,1.f)-0.5f)<0.0001f);
    assert(busAudible(false,false,false));
    assert(busAudible(false,true,true));
    assert(!busAudible(true,true,true));
    assert(!busAudible(false,false,true));
    std::cout<<"MIXER: phase correlation, routing BUS and stereo gains OK\n";
}
