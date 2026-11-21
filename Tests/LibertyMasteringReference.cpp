#include "LibertyMasteringMeter.h"
#include <iostream>
#include <iomanip>
#include <memory>
#include <cstdlib>
int main(int argc,char** argv)
{
    if(argc!=2)return 2;
    const double rate=std::atof(argv[1]);
    if(rate<8000.||rate>384000.)return 2;
    auto m=std::make_unique<LibertyMastering::Meter>();m->configure(rate);
    // Native little-endian float32 L/R pairs from the fixture converter.
    static_assert(sizeof(LibertyImmersive::Frame)==2*sizeof(float),"Frame layout");
    std::array<LibertyImmersive::Frame,4096> frames{};
    while(std::cin)
    {
        std::cin.read(reinterpret_cast<char*>(frames.data()),sizeof(frames));
        const auto bytes=std::cin.gcount();
        if(bytes%(long)sizeof(frames[0]))return 3;
        m->push(frames.data(),static_cast<size_t>(bytes)/sizeof(frames[0]));
    }
    m->finish();const auto r=m->get();
    std::cout<<std::setprecision(12)<<r.integrated<<" "<<r.lra<<" "<<r.truePeak<<"\n";
}
