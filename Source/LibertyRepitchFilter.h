#pragma once
#include <array>
#include <cmath>
#include <algorithm>
namespace LibertyRepitch {
class Filter {
public:
    static constexpr int taps=32, phases=64, bands=32;
    Filter() noexcept {
        constexpr double pi=3.14159265358979323846;
        for(int b=0;b<bands;++b)for(int p=0;p<=phases;++p){
            const double cutoff=.45*(b+1)/bands;
            double sum=0;
            auto& row=weights[b][p];
            for(int k=0;k<taps;++k){
                const double x=k-(taps/2-1)-double(p)/phases;
                const double z=2*pi*cutoff*x;
                const double sinc=std::abs(z)<1.e-12?1:std::sin(z)/z;
                const double window=.5+.5*std::cos(pi*x/(taps/2));
                row[k]=float(2*cutoff*sinc*window);sum+=row[k];
            }
            for(auto& v:row)v=float(v/sum);
        }
    }
    float read(const float* data,int size,double position,double speed)const noexcept {
        if(!data||size<1||!std::isfinite(position)||!std::isfinite(speed)||speed<=0)return 0;
        position=std::clamp(position,0.0,double(size-1));
        const int base=int(position);
        const int phase=std::clamp(int(std::round((position-base)*phases)),0,phases);
        const int band=std::clamp(int(bands/std::max(1.0,speed))-1,0,bands-1);
        const auto& row=weights[band][phase];
        float result=0;
        for(int k=0;k<taps;++k)result+=data[std::clamp(base+k-(taps/2-1),0,size-1)]*row[k];
        return result;
    }
private:
    std::array<std::array<std::array<float,taps>,phases+1>,bands> weights{};
};
inline const Filter& filter(){static const Filter instance;return instance;}
}
