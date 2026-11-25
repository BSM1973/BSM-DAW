#pragma once
#include <array>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <limits>

namespace LibertyRealtime {
constexpr int maxLanes=256,maxBuses=8,maxNodes=2*maxLanes+maxBuses+1;
struct Lane { int bus=-1; int declaredLatency=0; };
enum class Kind { Source, Inserts, Bus, Master };
struct Node { Kind kind=Kind::Source; int index=0, successor=-1, dependencies=0; };
struct Plan {
    std::array<Node,maxNodes> nodes{};
    std::array<int,maxLanes> compensation{};
    std::array<int,maxBuses> busCompensation{};
    int size=0,master=-1,latency=0;
    // Control-thread plan only. Reject oversized or invalid latency instead
    // of silently truncating. Current buses have no hosted inserts.
    bool build(const Lane* lanes,int count,int buses) noexcept {
        size=0;master=-1;latency=0;
        if(count<0||count>maxLanes||buses<0||buses>maxBuses||(!lanes&&count))return false;
        std::array<int,maxBuses> busLatency{};
        for(int i=0;i<count;++i){
            const auto lane=lanes[i];
            if(lane.bus< -1||lane.bus>=buses||lane.declaredLatency<0)return false;
            latency=(std::max)(latency,lane.declaredLatency);
            if(lane.bus>=0)busLatency[lane.bus]=(std::max)(busLatency[lane.bus],lane.declaredLatency);
        }
        size=2*count+buses+1;master=size-1;
        for(int i=0;i<size;++i)nodes[i]={};
        for(int i=0;i<count;++i){
            const auto lane=lanes[i];
            const int destination=lane.bus<0?master:2*count+lane.bus;
            nodes[2*i]={Kind::Source,i,2*i+1,0};
            nodes[2*i+1]={Kind::Inserts,i,destination,1};
            ++nodes[destination].dependencies;
            compensation[i]=(lane.bus<0?latency:busLatency[lane.bus])-lane.declaredLatency;
        }
        for(int b=0;b<buses;++b){
            auto& node=nodes[2*count+b];
            node.kind=Kind::Bus;node.index=b;node.successor=master;
            ++nodes[master].dependencies;
            busCompensation[b]=latency-busLatency[b];
        }
        nodes[master].kind=Kind::Master;nodes[master].successor=-1;
        return true;
    }
};
// Experimental bounded, non-waiting dependency ledger. Immutable plan must
// outlive all workers; reset only after all users are quiescent. No DSP or
// actual hardware callback is dispatched by this class.
class Ledger {
    const Plan* plan=nullptr;
    std::array<std::atomic<int>,maxNodes> pending{},state{};
public:
    static_assert(std::atomic<int>::is_always_lock_free,"lock-free scheduler atomics required");
    void reset(const Plan& p) noexcept {
        plan=&p;
        for(int i=0;i<p.size;++i){pending[i].store(p.nodes[i].dependencies,std::memory_order_relaxed);state[i].store(0,std::memory_order_relaxed);}
    }
    bool tryClaim(int id) noexcept {
        if(!plan||id<0||id>=plan->size||pending[id].load(std::memory_order_acquire)!=0)return false;
        int expected=0;
        return state[id].compare_exchange_strong(expected,1,std::memory_order_acq_rel);
    }
    // Call only after writing the node output; acq_rel propagates all
    // predecessors' writes before the successor can acquire readiness.
    bool complete(int id) noexcept {
        if(!plan||id<0||id>=plan->size)return false;
        int expected=1;
        if(!state[id].compare_exchange_strong(expected,2,std::memory_order_acq_rel))return false;
        const int next=plan->nodes[id].successor;
        if(next>=0)pending[next].fetch_sub(1,std::memory_order_acq_rel);
        return true;
    }
    bool finished() const noexcept {
        return plan&&plan->master>=0&&state[plan->master].load(std::memory_order_acquire)==2;
    }
};
}
