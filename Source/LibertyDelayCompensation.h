#pragma once
#include "LibertyRealtimePlan.h"
#include <vector>
#include <cstddef>
#include <algorithm>

namespace LibertyPdc {
struct PlanMessage {
    LibertyRealtime::Plan plan{};
    int lanes=0,buses=0;
    bool discontinuity=false;
    std::uint64_t generation=0;
};
// Single producer, single consumer. Both indices wrap in a fixed array.
// Producer owns only free slots; consumer copies before releasing its slot.
class PlanMailbox {
    static constexpr unsigned slots=4;
    std::array<PlanMessage,slots> queue{};
    std::atomic<unsigned> write{0},read{0};
public:
    static_assert(std::atomic<unsigned>::is_always_lock_free,"PDC mailbox requires lock-free indices");
    bool publish(const PlanMessage& message) noexcept {
        const auto w=write.load(std::memory_order_relaxed);
        const auto next=(w+1)%slots;
        if(next==read.load(std::memory_order_acquire))return false;
        queue[w]=message;
        write.store(next,std::memory_order_release);
        return true;
    }
    bool consume(PlanMessage& message) noexcept {
        const auto r=read.load(std::memory_order_relaxed);
        if(r==write.load(std::memory_order_acquire))return false;
        message=queue[r];
        read.store((r+1)%slots,std::memory_order_release);
        return true;
    }
    // Only while producer and consumer are both stopped.
    void reset() noexcept {read.store(0,std::memory_order_relaxed);write.store(0,std::memory_order_relaxed);}
};

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

// Variable delay with preallocated history and bounded linear tap transitions.
// Reset invalidates history in O(1), preventing stale samples after seeking.
class DynamicDelay {
    std::vector<float> left,right;
    std::size_t cursor=0,valid=0;
    int from=0,to=0,requested=0,progress=0,fade=0;
    float read(const std::vector<float>& ring,int delay,float input) const noexcept {
        if(delay==0)return input;
        if(static_cast<std::size_t>(delay)>valid)return 0.f;
        return ring[(cursor+ring.size()-static_cast<std::size_t>(delay))%ring.size()];
    }
    void begin(int delay,int frames) noexcept {
        from=to;to=delay;progress=0;fade=frames;
    }
public:
    void prepare(int capacity,int delay) {
        left.assign(static_cast<std::size_t>(capacity)+1,0.f);
        right.assign(left.size(),0.f);
        from=to=requested=delay;cursor=valid=0;progress=fade=0;
    }
    void reset(int delay) noexcept {
        from=to=requested=delay;cursor=valid=0;progress=fade=0;
    }
    void retarget(int delay,int frames) noexcept {
        requested=delay;
        if(fade==0&&to!=requested)begin(requested,frames);
    }
    void process(float* l,float* r,int frames,int transitionFrames) noexcept {
        if(left.empty()||frames<=0)return;
        for(int i=0;i<frames;++i){
            const float x=l?l[i]:0.f,y=r?r[i]:0.f;
            const float mix=fade>0?static_cast<float>(progress+1)/static_cast<float>(fade):1.f;
            const float a=read(left,from,x),b=read(right,from,y);
            if(l)l[i]=fade>0?a+(read(left,to,x)-a)*mix:read(left,to,x);
            if(r)r[i]=fade>0?b+(read(right,to,y)-b)*mix:read(right,to,y);
            left[cursor]=x;right[cursor]=y;
            if(++cursor==left.size())cursor=0;
            if(valid<left.size()-1)++valid;
            if(fade>0&&++progress>=fade){
                from=to;fade=progress=0;
                if(requested!=to)begin(requested,transitionFrames);
            }
        }
    }
};
class DynamicBank {
    std::vector<DynamicDelay> lanes,buses;
    LibertyRealtime::Plan plan{};
    int laneCount=0,busCount=0,capacity=0,transition=128;
    bool ready=false;
    bool validPlan(const LibertyRealtime::Plan& p,int n,int b) const noexcept {
        if(n<0||n>LibertyRealtime::maxLanes||b<0||b>LibertyRealtime::maxBuses||
           p.size!=2*n+b+1||p.master!=p.size-1||p.latency<0||p.latency>capacity)return false;
        for(int i=0;i<n;++i)if(p.compensation[i]<0||p.compensation[i]>capacity)return false;
        for(int i=0;i<b;++i)if(p.busCompensation[i]<0||p.busCompensation[i]>capacity)return false;
        return true;
    }
public:
    bool prepare(const LibertyRealtime::Plan& p,int n,int b,int maxDelay,int fadeFrames) {
        if(maxDelay<0||fadeFrames<1||n<0||n>LibertyRealtime::maxLanes)return false;
        if((static_cast<std::uint64_t>(n)+LibertyRealtime::maxBuses)*(static_cast<std::uint64_t>(maxDelay)+1)*2*sizeof(float)>64ull*1024*1024)return false;
        ready=false;capacity=maxDelay;transition=fadeFrames;
        if(!validPlan(p,n,b))return false;
        try{
            std::vector<DynamicDelay> l(static_cast<std::size_t>(n)),bs(LibertyRealtime::maxBuses);
            for(int i=0;i<n;++i)l[i].prepare(capacity,p.compensation[i]);
            for(int i=0;i<LibertyRealtime::maxBuses;++i)bs[i].prepare(capacity,i<b?p.busCompensation[i]:0);
            lanes.swap(l);buses.swap(bs);
        }catch(...){return false;}
        plan=p;laneCount=n;busCount=b;ready=true;return true;
    }
    // Single coordinator between callbacks. No allocations; invalid plans
    // are rejected in their entirety. Routing changes invalidate old history.
    bool update(const LibertyRealtime::Plan& p,int n,int b,bool discontinuity,bool& changed) noexcept {
        changed=false;
        if(!ready||n!=laneCount||!validPlan(p,n,b))return false;
        bool routing=b!=busCount;
        bool delays=p.latency!=plan.latency;
        for(int i=0;i<n;++i){
            routing=routing||p.nodes[2*i+1].successor!=plan.nodes[2*i+1].successor;
            delays=delays||p.compensation[i]!=plan.compensation[i];
        }
        for(int i=0;i<b;++i)delays=delays||p.busCompensation[i]!=plan.busCompensation[i];
        changed=routing||delays||discontinuity;
        if(!changed)return true;
        for(int i=0;i<n;++i){
            if(routing||discontinuity)lanes[i].reset(p.compensation[i]);
            else lanes[i].retarget(p.compensation[i],transition);
        }
        for(int i=0;i<LibertyRealtime::maxBuses;++i){
            const int delay=i<b?p.busCompensation[i]:0;
            if(routing||discontinuity)buses[i].reset(delay);
            else buses[i].retarget(delay,transition);
        }
        plan=p;busCount=b;return true;
    }
    void lane(int i,float* l,float* r,int frames) noexcept {
        if(ready&&i>=0&&i<laneCount)lanes[i].process(l,r,frames,transition);
    }
    void bus(int i,float* l,float* r,int frames) noexcept {
        if(ready&&i>=0&&i<busCount)buses[i].process(l,r,frames,transition);
    }
};
}
