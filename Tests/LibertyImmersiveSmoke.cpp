#include "LibertyImmersiveAnalysis.h"
#include <cassert>
#include <iostream>
int main()
{
    using namespace LibertyImmersive;
    Tap tap;std::array<float,fftSize> l{},r{};std::array<Frame,fftSize> samples{};
    for(std::size_t i=0;i<fftSize;++i){l[i]=0.5f*std::sin(2.f*pi*128.f*(float)i/(float)fftSize);r[i]=-l[i];}
    float* channels[]={l.data(),r.data()};
    tap.capture(channels,2,(int)fftSize);assert(tap.read(samples.data(),fftSize)==0);
    tap.setEnabled(true);tap.capture(channels,2,(int)fftSize);assert(tap.read(samples.data(),fftSize)==fftSize);
    const auto m=measure(samples.data(),fftSize);assert(std::abs(m.peak-.5f)<.001f);assert(std::abs(m.rms-.353553f)<.001f);assert(m.correlated&&m.correlation<-.999f);assert(std::abs(m.crest-3.0103f)<.01f);
    ComplexBuffer work{};Spectrum spectrumBins{};spectrum(samples,work,spectrumBins);
    assert(std::distance(spectrumBins.begin(),std::max_element(spectrumBins.begin(),spectrumBins.end()))==128);
    assert(std::abs(db(spectrumBins[128])+6.0206f)<.02f);
    tap.capture(channels,1,(int)fftSize);assert(tap.read(samples.data(),fftSize)==fftSize);assert(measure(samples.data(),fftSize).correlation>.999f);
    for(int i=0;i<10;++i)tap.capture(channels,2,(int)fftSize);
    for(int i=0;i<8;++i)assert(tap.read(samples.data(),fftSize)==fftSize);
    assert(tap.read(samples.data(),fftSize)==0);
    for(int i=0;i<16;++i){tap.capture(channels,2,(int)fftSize);assert(tap.read(samples.data(),fftSize)==fftSize);assert(samples[20].l==l[20]);}
    tap.capture(channels,2,(int)fftSize);tap.discard();assert(tap.read(samples.data(),fftSize)==0);
    samples.fill({});assert(!measure(samples.data(),fftSize).correlated);
    std::cout<<"PASS: real stereo tap, mono, overflow, wrap, calibrated FFT, RMS and correlation\n";
}
