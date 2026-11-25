#include "LibertyRealtimePlan.h"
#include <cassert>
#include <thread>
#include <vector>
using namespace LibertyRealtime;
int main(){
    const Lane lanes[]={{0,12},{0,32},{-1,5},{1,80},{1,20}};
    Plan p;
    assert(p.build(lanes,5,2));
    assert(p.size==13&&p.latency==80&&p.master==12);
    assert(p.compensation[0]==20&&p.compensation[1]==0&&p.compensation[2]==75);
    assert(p.compensation[3]==0&&p.compensation[4]==60&&p.busCompensation[0]==48&&p.busCompensation[1]==0);
    Ledger l;l.reset(p);
    assert(!l.tryClaim(p.master)&&!l.tryClaim(1));
    assert(l.tryClaim(0)&&!l.tryClaim(0)&&l.complete(0)&&!l.complete(0));
    assert(l.tryClaim(1)&&l.complete(1)&&!l.tryClaim(10));
    for(int i=2;i<10;++i){assert(l.tryClaim(i));assert(l.complete(i));}
    assert(l.tryClaim(10)&&l.complete(10));assert(!l.tryClaim(p.master));
    assert(l.tryClaim(11)&&l.complete(11));assert(l.tryClaim(p.master)&&l.complete(p.master)&&l.finished());
    Lane invalid{2,0};assert(!p.build(&invalid,1,2));
    assert(!p.build(nullptr,maxLanes+1,0));
    std::array<Lane,maxLanes> many{};
    for(int i=0;i<maxLanes;++i)many[i]={i%8,i};
    assert(p.build(many.data(),maxLanes,8));
    for(int repeat=0;repeat<20;++repeat){
        l.reset(p);std::array<int,maxNodes> result{};
        std::vector<std::thread> workers;
        for(int w=0;w<4;++w)workers.emplace_back([&]{
            while(!l.finished()){
                for(int i=0;i<p.size;++i)if(l.tryClaim(i)){
                    result[i]=1;
                    assert(l.complete(i));
                }
                std::this_thread::yield();
            }
        });
        for(auto& worker:workers)worker.join();
        for(int i=0;i<p.size;++i)assert(result[i]==1);
    }
}
