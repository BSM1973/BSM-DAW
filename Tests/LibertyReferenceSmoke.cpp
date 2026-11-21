#include "LibertyReferenceAnalysis.h"
#include "LibertyReferencePdf.h"
#include <cassert>
#include <iostream>
int main()
{
    using namespace LibertyReference;
    using namespace LibertyImmersive;
    Analyzer mono(48000.),anti(48000.);
    std::array<Frame,fftSize> block{};
    for(size_t i=0;i<block.size();++i){const float x=.5f*std::sin(2.f*pi*128.f*i/fftSize);block[i]={x,x};}
    mono.push(block.data(),block.size());
    for(auto& x:block)x.r=-x.l;
    anti.push(block.data(),block.size());
    const auto a=mono.get(),b=anti.get();
    assert(a.windows==1&&b.windows==1);
    assert(a.width(3)<.01&&b.width(3)>99.99);
    assert(a.dynamics(3)<.01);
    assert(std::abs(a.band[3].power-b.band[3].power)<.0001);
    assert(matchGain(-10.,-14.,-1.,-2.)<=-5.);
    const double ga=matchGain(-10.,-14.,-1.,-2.),gb=matchGain(-14.,-10.,-2.,-1.);
    assert(std::abs((-10.+ga)-(-14.+gb))<.00001);
    const auto pdf=makePdf({"Test (A)","\xc3\xa9",std::string(20,'x')});
    assert(pdf.rfind("%PDF-1.4",0)==0&&pdf.find("xref")!=std::string::npos&&pdf.find("Test \\(A\\)")!=std::string::npos);
    assert(pdf.find("\\351")!=std::string::npos);
    std::cout<<"PASS reference spectral bands, stereo side fraction, dynamics, attenuation match\n";
}
