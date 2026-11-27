#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>
#include <iterator>
namespace LibertySmartSilence {
struct Range {int start=0,end=0;};
struct Settings {double threshold=-55.,minimumMs=250.,marginMs=40.,hysteresisDb=6.;double beforeMs=-1.,afterMs=-1.;bool protectQuiet=false;bool detailProtection=false;bool adaptiveNoise=false;};
inline Settings factoryProfile(Settings settings,int mode) noexcept {
            if(mode==2){settings.protectQuiet=true;settings.threshold=std::min(settings.threshold,-65.);settings.minimumMs=std::max(settings.minimumMs,350.);settings.beforeMs=std::max(settings.beforeMs,80.);settings.afterMs=std::max(settings.afterMs,100.);}
            if(mode==3){settings.protectQuiet=true;settings.beforeMs=std::max(settings.beforeMs,20.);}
            if(mode==4){settings.protectQuiet=true;settings.threshold=std::min(settings.threshold,-60.);settings.afterMs=std::max(settings.afterMs,100.);}
            if(mode==5){settings.protectQuiet=true;settings.threshold=std::min(settings.threshold,-65.);settings.minimumMs=std::max(settings.minimumMs,300.);settings.afterMs=std::max(settings.afterMs,150.);}
            if(mode==6){settings.protectQuiet=true;settings.threshold=std::min(settings.threshold,-60.);settings.minimumMs=std::max(settings.minimumMs,400.);settings.beforeMs=std::max(settings.beforeMs,100.);settings.afterMs=std::max(settings.afterMs,120.);}
            if(mode==7){settings.protectQuiet=true;settings.beforeMs=std::max(settings.beforeMs,30.);settings.afterMs=std::max(settings.afterMs,60.);}
            if(mode==8||mode==9){settings.protectQuiet=true;settings.threshold=std::min(settings.threshold,-75.);settings.minimumMs=std::max(settings.minimumMs,500.);settings.afterMs=std::max(settings.afterMs,mode==9?300.:200.);}
            settings.detailProtection=mode>=2&&mode<=9;

    return settings;
}
// Full-wave window peak avoids stereo cancellation and preserves short attacks.
// A silence must survive both the duration test and the safety margins.
template<class Sample>
bool detect(int count,double rate,int channels,const Settings& s,Sample sample,
            std::vector<Range>& cuts,const std::atomic<bool>* cancel=nullptr)
{
    cuts.clear();
    if(count<=0||!std::isfinite(rate)||rate<8000.||rate>384000.||channels<1||channels>2||
       !std::isfinite(s.threshold)||s.threshold<-90.||s.threshold>-10.||
       !std::isfinite(s.minimumMs)||s.minimumMs<20.||s.minimumMs>10000.||
       !std::isfinite(s.marginMs)||s.marginMs<0.||s.marginMs>500.||
       !std::isfinite(s.hysteresisDb)||s.hysteresisDb<0.||s.hysteresisDb>18.)return false;
    const int block=std::max(1,(int)std::llround(rate*.005));
    const int minimum=(int)std::ceil(rate*s.minimumMs*.001);
    if(!std::isfinite(s.beforeMs)||!std::isfinite(s.afterMs)||s.beforeMs< -1.||s.afterMs< -1.||s.beforeMs>500.||s.afterMs>500.)return false;
    const int before=(int)std::ceil(rate*(s.beforeMs<0.?s.marginMs:s.beforeMs)*.001);
    const int after=(int)std::ceil(rate*(s.afterMs<0.?s.marginMs:s.afterMs)*.001);
    const int minKeep=(int)std::ceil(rate*.01);
    const double globalEnter=std::pow(10.,s.threshold/20.),hysteresis=std::pow(10.,s.hysteresisDb/20.);
    double enter=globalEnter;
    // Three continuous low-pass stages define four approximate frequency bands.
    // State is independent per channel; no mono cancellation and no FFT allocation.
    const double a0=1.-std::exp(-6.283185307179586*250./rate);
    const double a1=1.-std::exp(-6.283185307179586*1500./rate);
    const double a2=1.-std::exp(-6.283185307179586*std::min(4500.,rate*.40)/rate);
    double lp[2][3]={};
    int noiseBins[91]={},noiseWindows=0;
    const int noisePeriod=std::max(1,(int)std::llround(rate/block));
    int silence=-1;bool active=false;double previousPeak=0.;
    auto close=[&](int end){
        if(silence<0||end-silence<minimum)return;
        int a=silence==0?0:silence+after,b=end==count?count:end-before;
        if(a>0&&a<minKeep)a=minKeep;
        if(b<count&&count-b<minKeep)b=count-minKeep;
        if(b-a<minimum)return;
        // Avoid leaving unusably short pieces between consecutive cuts.
        if(!cuts.empty()&&a-cuts.back().end<minKeep)return;
        cuts.push_back({a,b});
    };
    for(int pos=0;pos<count;){
        if(cancel&&cancel->load())return false;
        const int end=pos+std::min(block,count-pos);double peak=0.,energy=0.,difference=0.,bands[4]={};
        for(int ch=0;ch<channels;++ch){
            double last=0.;
            for(int i=pos;i<end;++i){
                const double x=sample(ch,i);if(!std::isfinite(x))return false;
                peak=std::max(peak,std::abs(x));energy+=x*x;
                if(i>pos)difference+=(x-last)*(x-last);
                last=x;
                if(s.detailProtection){
                    auto& f=lp[ch];
                    f[0]+=a0*(x-f[0]);f[1]+=a1*(x-f[1]);f[2]+=a2*(x-f[2]);
                    const double values[]={f[0],f[1]-f[0],f[2]-f[1],x-f[2]};
                    for(int b=0;b<4;++b)bands[b]+=values[b]*values[b];
                }
            }
        }
        // Heuristic, not a speech recognizer: relative high-frequency energy
        // and a slowly decaying envelope can indicate weak detail or a tail.
        const double bandSum=bands[0]+bands[1]+bands[2]+bands[3];
        const bool spectralDetail=bandSum>1.e-20&&(bands[2]+bands[3])>.18*bandSum;
        const bool detail=s.detailProtection&&peak>=enter*.125&&
            ((energy>1.e-20&&difference/energy>.30)||spectralDetail||
             (previousPeak>0.&&peak<previousPeak&&peak>previousPeak*.70));
        const double leave=enter*hysteresis;
        if(silence>=0){
            if(detail||peak>=(s.protectQuiet?enter:leave)){close(pos);silence=-1;active=true;}
        }else if(!detail&&peak<enter)silence=pos;
        else active=true;
        // Adapt to the preceding one-second region only, with a lower
        // percentile noise floor. Never raise the user's/global threshold.
        if(s.adaptiveNoise){
            const int db=(int)std::floor(std::clamp(20.*std::log10(std::max(1.e-9,peak)),-90.,0.));
            ++noiseBins[db+90];++noiseWindows;
            if(noiseWindows>=noisePeriod){
                int sum=0;double local=-90.;
                for(int b=0;b<=90;++b){sum+=noiseBins[b];if(sum>=std::max(1,(noiseWindows+4)/5)){local=b-90.+6.;break;}}
                enter=std::min(globalEnter,std::pow(10.,std::clamp(local,-90.,-10.)/20.));
                std::fill(std::begin(noiseBins),std::end(noiseBins),0);noiseWindows=0;
            }
        }
        previousPeak=peak;pos=end;
        if(cuts.size()>4096)return false;
    }
    close(count);
    if(cuts.size()>4096)return false;
    // Never delete a completely silent clip automatically.
    if(!active)cuts.clear();
    return !(cancel&&cancel->load());
}
inline std::vector<Range> retained(int count,const std::vector<Range>& cuts){
    std::vector<Range> result;int pos=0;
    for(const auto& cut:cuts){if(cut.start>pos)result.push_back({pos,cut.start});pos=cut.end;}
    if(pos<count)result.push_back({pos,count});
    return result;
}
// Conservative lower percentile of 5 ms peaks. Limit the threshold to
// the user ceiling, so an automatic estimate cannot remove louder events.
template<class Sample>
double noiseThreshold(int count,double rate,int channels,double ceiling,Sample sample,const std::atomic<bool>* cancel=nullptr){
    if(count<=0||channels<1||channels>2||!std::isfinite(rate)||rate<8000.||rate>384000.||!std::isfinite(ceiling))return ceiling;
    const int block=std::max(1,(int)std::llround(rate*.005));
    int histogram[91]={};int windows=0;
    for(int pos=0;pos<count;){
        if(cancel&&cancel->load())return ceiling;
        const int end=pos+std::min(block,count-pos);double peak=0.;
        for(int ch=0;ch<channels;++ch)for(int i=pos;i<end;++i){
            const double x=sample(ch,i);if(!std::isfinite(x))return ceiling;
            peak=std::max(peak,std::abs(x));
        }
        const int db=(int)std::floor(std::clamp(20.*std::log10(std::max(1.e-9,peak)),-90.,0.));
        ++histogram[db+90];++windows;pos=end;
    }
    int cumulative=0;
    for(int i=0;i<=90;++i){cumulative+=histogram[i];if(cumulative>=std::max(1,(windows+4)/5))
        return std::clamp(std::min(ceiling,double(i-90)+6.),-90.,-10.);}
    return ceiling;
}
// Search only inward: never extend a proposed deletion beyond its
// detected bounds. Both channels must cross and be quiet at the boundary.
template<class Sample>
Range alignInward(Range cut,int count,int channels,int radius,int minimum,Sample sample){
    if(cut.start<0||cut.end>count||cut.end<=cut.start||channels<1||channels>2||radius<0||minimum<1)return cut;
    const auto quietCrossing=[&](int i){
        if(i<=0||i>=count)return false;
        for(int ch=0;ch<channels;++ch){
            const double a=sample(ch,i-1),b=sample(ch,i);
            if(!std::isfinite(a)||!std::isfinite(b)||std::max(std::abs(a),std::abs(b))>.01||
               !((a<=0.&&b>=0.)||(a>=0.&&b<=0.)))return false;
        }
        return true;
    };
    Range result=cut;
    if(cut.start>0)for(int i=cut.start;i<=cut.start+std::min(radius,cut.end-cut.start-minimum);++i)
        if(quietCrossing(i)){result.start=i;break;}
    if(cut.end<count)for(int i=cut.end;i>=cut.end-std::min(radius,cut.end-result.start-minimum);--i)
        if(quietCrossing(i)){result.end=i;break;}
    return result.end-result.start>=minimum?result:cut;
}
}
