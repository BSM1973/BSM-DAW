#pragma once
#include "PluginHost.h"
#include "LibertySampleLab.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
namespace LibertyPluginAudition {
inline juce::AudioBuffer<float> reference(int type,double tempo,double rate=48000.){
 // Original deterministic synthetic test signals, not recorded instruments.
 const double beat=60./juce::jlimit(40.,240.,tempo);
 const int length=(int)std::min(rate*8.,rate*beat*8.);
 juce::AudioBuffer<float> buffer(2,length);buffer.clear();
 constexpr double pi=3.14159265358979323846;
 uint32_t rng=123456789u;
 for(int i=0;i<length;++i){
  const double time=i/rate,t=std::fmod(time,beat),half=std::fmod(time,beat*.5);rng=rng*1664525u+1013904223u;const double noise=(double(rng>>8)/8388608.)-1.;
  const int step=int(time/beat)%8;const int notes[]={48,48,55,51,53,53,58,55};const double hz=440.*std::pow(2.,(notes[step]-69)/12.);
  double v=0.;
  if(type==0){const double pulse=std::exp(-half*13.);v=.32*pulse*std::sin(2*pi*(48*half+5*(1-std::exp(-half*35.))));if(step%2)v+=.09*noise*std::exp(-t*18.);}
  else if(type==1)v=.28*std::exp(-t*7.)*(std::sin(2*pi*hz*t)+.23*std::sin(4*pi*hz*t));
  else if(type==2)v=.20*std::exp(-t*4.)*(std::sin(2*pi*hz*t)+.35*std::sin(2*pi*hz*1.5*t)+.2*std::sin(4*pi*hz*t));
  else v=.18*std::exp(-half*5.)*(std::sin(2*pi*hz*half)+.3*std::sin(4*pi*hz*half)+.15*std::sin(6*pi*hz*half));
  buffer.setSample(0,i,(float)v);buffer.setSample(1,i,(float)v);
 }return buffer;
}
inline bool readSource(const juce::File& file,juce::AudioBuffer<float>& out,double& rate){
 juce::AudioBuffer<float> input;if(!LibertySampleLab::read(file,input,rate,8.)||rate>96000)return false;
 out.setSize(2,input.getNumSamples());for(int ch=0;ch<2;++ch)out.copyFrom(ch,0,input,juce::jmin(ch,input.getNumChannels()-1),0,input.getNumSamples());return true;
}
}
