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

struct Contour {
    std::vector<double> periods;
    int hop=0;
    double vibratoHz=0.0,vibratoDepthCents=0.0;
    double at(double sample) const noexcept {
        const double index=std::clamp(sample/hop,0.0,double(periods.size()-1));
        const size_t a=(size_t)index,b=std::min(a+1,periods.size()-1);
        return periods[a]+(periods[b]-periods[a])*(index-a);
    }
};
inline Contour track(const float* data,int n,double rate,const std::atomic<bool>* cancel=nullptr)
{
    Contour out;
    if(!data||!std::isfinite(rate)||rate<8000||rate>192000||n<rate*.2||n>rate*20)return out;
    const int lo=std::max(2,int(rate/1000)),hi=int(rate/50),window=int(rate*.025);
    const int stride=std::max(1,int(rate/12000));
    out.hop=std::max(1,int(rate*.02));
    if(n<window+hi+2)return {};
    std::vector<double> corr((size_t)hi+2);
    for(int center=0;center<n;center+=out.hop){
        if(cancel&&cancel->load(std::memory_order_relaxed))return {};
        const int start=std::clamp(center-window/2,0,n-window-hi-2);
        for(int lag=lo-1;lag<=hi+1;++lag){
            double aa=0,bb=0,ab=0,ma=0,mb=0;int count=0;
            for(int i=0;i<window;i+=stride){
                const double a=data[start+i],b=data[start+i+lag];
                aa+=a*a;bb+=b*b;ab+=a*b;ma+=a;mb+=b;++count;
            }
            aa-=ma*ma/count;bb-=mb*mb/count;ab-=ma*mb/count;
            corr[(size_t)lag]=aa>1.e-8&&bb>1.e-8?ab/std::sqrt(aa*bb):0;
        }
        double period=0;
        for(int lag=lo;lag<=hi;++lag){
            const double a=corr[(size_t)lag-1],b=corr[(size_t)lag],z=corr[(size_t)lag+1];
            if(b>=.86&&b>=a&&b>z){
                const double curvature=a-2*b+z;
                period=lag+(std::abs(curvature)>1.e-12?std::clamp(.5*(a-z)/curvature,-.5,.5):0.);
                break;
            }
        }
        out.periods.push_back(period);
    }
    // Bridge only short unvoiced gaps at note boundaries, never extended silence.
    for(size_t i=0;i<out.periods.size();){
        if(out.periods[i]>0){++i;continue;}
        size_t end=i;while(end<out.periods.size()&&out.periods[end]<=0)++end;
        if(i==0||end==out.periods.size()||end-i>3)return {};
        for(size_t k=i;k<end;++k)out.periods[k]=out.periods[i-1]+(out.periods[end]-out.periods[i-1])*double(k-i+1)/double(end-i+1);
        i=end;
    }
    if(out.periods.size()<10)return {};
    // Search periodic modulation of the cents contour in 3–9 Hz range.
    // Only report if a single oscillation explains most contour variance.
    std::vector<double> cents;double mean=0;
    for(double p:out.periods){const double x=1200*std::log2(rate/p);cents.push_back(x);mean+=x;}
    mean/=cents.size();double energy=0;
    for(auto& x:cents){x-=mean;energy+=x*x;}
    if(energy/cents.size()>25&&out.periods.size()*out.hop/rate>=.8){
        double best=0;
        for(double hz=3;hz<=9.001;hz+=.1){
            double re=0,im=0,ss=0,cc=0,sc=0;
            for(size_t i=0;i<cents.size();++i){
                const double phase=6.283185307179586*hz*i*out.hop/rate;
                const double a=std::cos(phase),b=std::sin(phase);
                re+=cents[i]*a;im+=cents[i]*b;cc+=a*a;ss+=b*b;sc+=a*b;
            }
            const double determinant=cc*ss-sc*sc;
            if(determinant<=0)continue;
            const double a=(re*ss-im*sc)/determinant,b=(im*cc-re*sc)/determinant;
            const double score=(a*re+b*im)/energy;
            if(score>best){best=score;out.vibratoHz=hz;out.vibratoDepthCents=std::hypot(a,b);}
        }
        if(best<.65||out.vibratoDepthCents<10||out.vibratoDepthCents>200)out.vibratoHz=out.vibratoDepthCents=0;
    }
    return out;
}
// Variable-period overlap-add. Locate same-phase positive zero crossings
// near predicted pitch marks; use their shared positions for synthesis.
inline bool renderMelody(const float* source,int n,float* target,int count,const Contour& contour,
                         const std::atomic<bool>* cancel=nullptr)
{
    if(!source||!target||n<4||count<1||contour.periods.empty()||contour.hop<=0)return false;
    const double ratio=double(count)/n;
    if(ratio<.25||ratio>4)return false;
    std::vector<double> marks;
    auto snap=[&](double predicted,double radius){
        const int left=std::max(1,int(predicted-radius)),right=std::min(n-1,int(predicted+radius));
        double best=predicted,distance=radius+1;
        for(int j=left;j<=right;++j)if(source[j-1]<=0&&source[j]>0){
            const double crossing=j-1-source[j-1]/double(source[j]-source[j-1]);
            if(std::abs(crossing-predicted)<distance){best=crossing;distance=std::abs(crossing-predicted);}
        }
        return std::clamp(best,0.0,double(n-1));
    };
    double mark=snap(0,contour.at(0)*.3);
    marks.push_back(mark);
    while(mark<n-1){
        if(cancel&&cancel->load(std::memory_order_relaxed))return false;
        const double period=contour.at(mark);
        const double predicted=mark+period;
        if(predicted>=n-1)break;
        const double next=snap(predicted,period*.28);
        if(next-mark<period*.5)return false;
        marks.push_back(next);mark=next;
    }
    if(marks.size()<3)return false;
    std::vector<float> norm((size_t)count,0.f);
    std::fill_n(target,count,0.f);
    size_t nearest=0;
    for(double pos=0;pos<count;){
        if(cancel&&cancel->load(std::memory_order_relaxed))return false;
        const double mapped=pos/ratio;
        const double period=contour.at(mapped);
        while(nearest+1<marks.size()&&std::abs(marks[nearest+1]-mapped)<std::abs(marks[nearest]-mapped))++nearest;
        const double center=marks[nearest];
        // Two-period Hann grain, no resampling within each grain.
        const int first=std::max(0,int(std::ceil(pos-period))),last=std::min(count-1,int(std::floor(pos+period)));
        for(int j=first;j<=last;++j){
            const double delta=j-pos,sourcePos=center+delta;
            if(sourcePos<0||sourcePos>n-1)continue;
            const float w=float(.5+.5*std::cos(3.14159265358979323846*delta/period));
            target[j]+=w*interpolate(source,n,sourcePos);norm[(size_t)j]+=w;
        }
        pos+=period;
    }
    for(int i=0;i<count;++i){
        if(norm[(size_t)i]>.01f)target[i]/=norm[(size_t)i];
        else target[i]=0;
        if(!std::isfinite(target[i]))return false;
    }
    return !(cancel&&cancel->load(std::memory_order_relaxed));
}

}
