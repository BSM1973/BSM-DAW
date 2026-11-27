#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>
namespace LibertySmartSilence {
struct Range {int start=0,end=0;};
struct Settings {double threshold=-55.,minimumMs=250.,marginMs=40.,hysteresisDb=6.;};
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
    const int margin=(int)std::ceil(rate*s.marginMs*.001);
    const int minKeep=(int)std::ceil(rate*.01);
    const double enter=std::pow(10.,s.threshold/20.),leave=std::pow(10.,(s.threshold+s.hysteresisDb)/20.);
    int silence=-1;bool active=false;
    auto close=[&](int end){
        if(silence<0||end-silence<minimum)return;
        int a=silence==0?0:silence+margin,b=end==count?count:end-margin;
        if(a>0&&a<minKeep)a=minKeep;
        if(b<count&&count-b<minKeep)b=count-minKeep;
        if(b-a<minimum)return;
        // Avoid leaving unusably short pieces between consecutive cuts.
        if(!cuts.empty()&&a-cuts.back().end<minKeep)return;
        cuts.push_back({a,b});
    };
    for(int pos=0;pos<count;){
        if(cancel&&cancel->load())return false;
        const int end=pos+std::min(block,count-pos);double peak=0.;
        for(int ch=0;ch<channels;++ch)for(int i=pos;i<end;++i){
            const double x=sample(ch,i);if(!std::isfinite(x))return false;
            peak=std::max(peak,std::abs(x));
        }
        if(silence>=0){
            if(peak>=leave){close(pos);silence=-1;active=true;}
        }else if(peak<=enter)silence=pos;
        else active=true;
        pos=end;
        if(cuts.size()>4096)return false;
    }
    close(count);
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
}
