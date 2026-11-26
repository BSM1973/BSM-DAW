#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace LibertyTonal {
struct Period { double samples=0,confidence=0; };
inline Period estimate(const float* data,int n,double rate,const std::atomic<bool>* cancel=nullptr)
{
    if(!data||!std::isfinite(rate)||rate<8000||rate>192000||n<rate*.16)return {};
    const int lo=std::max(2,int(rate/1000)),hi=int(rate/50),window=int(rate*.045);
    if(n<window+hi)return {};
    const int stride=std::max(1,int(rate/16000));
    double reference=0,confidence=1;
    std::vector<double> corr((size_t)hi+2);
    for(int frame=0;frame<5;++frame){
        if(cancel&&cancel->load(std::memory_order_relaxed))return {};
        const int start=(int)((long long)(n-window-hi)*frame/4);
        // Remove DC independently for each correlation pair.
        for(int lag=lo-1;lag<=hi+1;++lag){
            double aa=0,bb=0,ab=0,ma=0,mb=0;int count=0;
            for(int i=0;i<window-1;i+=stride){
                const double a=data[start+i],b=data[start+i+lag];
                aa+=a*a;bb+=b*b;ab+=a*b;ma+=a;mb+=b;++count;
            }
            aa-=ma*ma/count;bb-=mb*mb/count;ab-=ma*mb/count;
            corr[(size_t)lag]=aa>1.e-8&&bb>1.e-8?ab/std::sqrt(aa*bb):0;
        }
        int best=0;
        for(int lag=lo;lag<=hi;++lag){
            const double q=corr[(size_t)lag];
            if(q>=.92&&q>=corr[(size_t)lag-1]&&q>corr[(size_t)lag+1]){best=lag;break;}
        }
        if(!best)return {};
        const double a=corr[(size_t)best-1],b=corr[(size_t)best],d=corr[(size_t)best+1];
        const double curvature=a-2*b+d;
        const double period=best+(std::abs(curvature)>1.e-12?std::clamp(.5*(a-d)/curvature,-.5,.5):0.);
        if(frame==0)reference=period;
        else if(std::abs(period-reference)>.015*reference)return {};
        confidence=std::min(confidence,b);
    }
    return {reference,confidence};
}
inline float interpolate(const float* source,int size,double position) noexcept {
    position=std::clamp(position,0.,double(size-1));
    const int a=int(position),b=std::min(size-1,a+1);
    return float(source[a]+(source[b]-source[a])*(position-a));
}
// Period-synchronous overlap-add, common timing for both channels.
// Intended for stable periodic material, not arbitrary monophonic melodies.
inline bool render(const float* const* input,int size,float* const* output,int outSize,int channels,
                   double period,const std::atomic<bool>* cancel=nullptr)
{
    if(!input||!output||channels<1||channels>2||size<4||outSize<1||
       !std::isfinite(period)||period<2||period*4>=size)return false;
    for(int ch=0;ch<channels;++ch)if(!input[ch]||!output[ch])return false;
    const double ratio=double(outSize)/size;
    if(ratio<.25||ratio>4)return false;
    const int maxCycle=int((size-1)/period)-1;
    for(int i=0;i<outSize;++i){
        if((i&1023)==0&&cancel&&cancel->load(std::memory_order_relaxed))return false;
        const double cycle=i/period;
        const int k=int(cycle);const double phase=cycle-k;
        const double weight=.5-.5*std::cos(3.14159265358979323846*phase);
        const int a=std::clamp(int(std::round(k/ratio)),1,maxCycle);
        const int b=std::clamp(int(std::round((k+1)/ratio)),1,maxCycle);
        const double pa=(a+phase)*period,pb=(b+phase-1)*period;
        for(int ch=0;ch<channels;++ch)
            output[ch][i]=float((1-weight)*interpolate(input[ch],size,pa)+weight*interpolate(input[ch],size,pb));
    }
    return !(cancel&&cancel->load(std::memory_order_relaxed));
}
}
