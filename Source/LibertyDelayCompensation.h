#pragma once
#include "LibertyRealtimePlan.h"
#include <vector>
#include <cstddef>
#include <algorithm>

namespace LibertyPdc {
// Fixed stereo ring, prepared/reset outside the audio callback.
// Zero delay is a true bypass. No allocation or lock in process().
class Delay {
    std::vector<float> left,right;
    std::size_t cursor=0;
public:
    void prepare(int samples) {
        left.assign(static_cast<std::size_t>(samples),0.f);
        right.assign(static_cast<std::size_t>(samples),0.f);
        cursor=0;
    }
    void process(float* l,float* r,int frames) noexcept {
        if(left.empty()||frames<=0)return;
        for(int i=0;i<frames;++i){
            const float x=l?l[i]:0.f,y=r?r[i]:0.f;
            if(l)l[i]=left[cursor];
            if(r)r[i]=right[cursor];
            left[cursor]=x;right[cursor]=y;
            if(++cursor==left.size())cursor=0;
        }
    }
};
class Bank {
    std::vector<Delay> lanes,buses;
public:
    // A failed request never activates a partially prepared plan.
    bool prepare(const LibertyRealtime::Plan& p,int laneCount,int busCount,int maxDelay) {
        if(laneCount<0||laneCount>LibertyRealtime::maxLanes||busCount<0||
           busCount>LibertyRealtime::maxBuses||p.size!=2*laneCount+busCount+1||
           p.master!=p.size-1||maxDelay<0||p.latency<0||p.latency>maxDelay)return false;
        std::uint64_t total=0;
        for(int i=0;i<laneCount;++i){
            if(p.compensation[i]<0||p.compensation[i]>maxDelay)return false;
            total+=static_cast<std::uint64_t>(p.compensation[i]);
        }
        for(int i=0;i<busCount;++i){
            if(p.busCompensation[i]<0||p.busCompensation[i]>maxDelay)return false;
            total+=static_cast<std::uint64_t>(p.busCompensation[i]);
        }
        if(total>64ull*1024*1024/(2*sizeof(float)))return false;
        try {
            std::vector<Delay> l(static_cast<std::size_t>(laneCount)),b(static_cast<std::size_t>(busCount));
            for(int i=0;i<laneCount;++i)l[i].prepare(p.compensation[i]);
            for(int i=0;i<busCount;++i)b[i].prepare(p.busCompensation[i]);
            lanes.swap(l);buses.swap(b);return true;
        }catch(...){return false;}
    }
    void lane(int i,float* l,float* r,int frames) noexcept {
        if(i>=0&&static_cast<std::size_t>(i)<lanes.size())lanes[i].process(l,r,frames);
    }
    void bus(int i,float* l,float* r,int frames) noexcept {
        if(i>=0&&static_cast<std::size_t>(i)<buses.size())buses[i].process(l,r,frames);
    }
};
}
