#include "LibertyDelayCompensation.h"
#include <array>
#include <cassert>
using namespace LibertyPdc;
int main(){
    for(int delay:{0,1,31,32,63,128,513}){
        Delay line;line.prepare(delay);
        std::array<float,1024> l{},r{};
        l[3]=1.f;r[3]=-0.5f;
        for(int pos=0;pos<1024;pos+=37)line.process(l.data()+pos,r.data()+pos,(std::min)(37,1024-pos));
        for(int i=0;i<1024;++i){assert(l[i]==(i==3+delay?1.f:0.f));assert(r[i]==(i==3+delay?-0.5f:0.f));}
        line.prepare(delay);l.fill(0.f);r.fill(0.f);line.process(l.data(),r.data(),1024);
        for(float v:l)assert(v==0.f);
        for(float v:r)assert(v==0.f);
    }
    LibertyRealtime::Lane lanes[]={{0,32},{0,0},{-1,64}};
    LibertyRealtime::Plan plan;assert(plan.build(lanes,3,1));
    Bank bank;assert(bank.prepare(plan,3,1,128));
    assert(!bank.prepare(plan,3,1,32));
    std::array<float,128> l{},r{},busL{},busR{};
    l[0]=1.f;r[0]=-1.f;bank.lane(1,l.data(),r.data(),128);
    busL[32]=1.f;busR[32]=-1.f;
    for(int i=0;i<128;++i){busL[i]+=l[i];busR[i]+=r[i];}
    bank.bus(0,busL.data(),busR.data(),128);
    for(int i=0;i<128;++i){assert(busL[i]==(i==64?2.f:0.f));assert(busR[i]==(i==64?-2.f:0.f));}
}
