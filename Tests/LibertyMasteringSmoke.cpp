#include "LibertyMasteringMeter.h"
#include <cassert>
#include <iostream>
int main()
{
    using namespace LibertyMastering;
    const auto s=shelf(48000.),h=highpass(48000.);
    assert(std::abs(s.b0-1.53512485958697)<1e-8);
    assert(std::abs(s.a1+1.69065929318241)<1e-8);
    assert(std::abs(h.a1+1.99004745483398)<1e-8);
    Distribution d;d.add(std::pow(10.,(-23.+.691)/10.));d.add(std::pow(10.,(-60.+.691)/10.));
    assert(std::abs(d.integrated()+23.)<.01);
    Distribution range;for(int i=0;i<100;++i){range.add(std::pow(10.,(-23.+.691)/10.));range.add(std::pow(10.,(-13.+.691)/10.));}
    assert(std::abs(range.range()-10.)<.02);
    Meter m;m.configure(48000.);
    std::array<LibertyImmersive::Frame,480> b{};
    assert(!std::isfinite(m.get().integrated));
    for(int n=0;n<400;++n){for(size_t i=0;i<b.size();++i){const float x=.1f*std::sin(2.*3.141592653589793*1000.*(n*480+i)/48000.);b[i]={x,x};}m.push(b.data(),b.size());}
    const auto r=m.get();assert(std::abs(r.integrated+20.)<.15);assert(std::abs(r.shortTerm+20.)<.15);assert(std::abs(r.momentary+20.)<.15);assert(std::abs(r.truePeak+20.)<.15);assert(r.lra<.1);assert(m.historyCount()==40);
    m.resetPeak();assert(!std::isfinite(m.get().truePeak));assert(std::abs(m.get().integrated-r.integrated)<.001);
    m.resetHistory();assert(m.historyCount()==0);assert(std::isfinite(m.get().integrated));
    m.resetLoudness();assert(!std::isfinite(m.get().integrated));
    TruePeak tp;for(int i=0;i<1024;++i){const float x=.9f*std::sin(3.141592653589793*.5*i+3.141592653589793*.25);tp.push({x,x});}
    assert(tp.dbtp()>-1.5&&tp.dbtp()<.1);
    m.configure(44100.);assert(m.duration()==0.);
    std::cout<<"PASS: K filters, loudness gate, LRA, stereo sine, independent resets and intersample peak\n";
}
