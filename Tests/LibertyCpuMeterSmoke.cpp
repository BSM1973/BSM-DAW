#include "LibertyCpuMeter.h"
#include <cassert>
int main(){
    using namespace LibertyCpu;
    assert(budgetFor(48000,480)==10000000);
    assert(budgetFor(0,128)==0&&budgetFor(48000,0)==0);
    Meter m;m.add(500,1000);m.add(1500,1000);m.add(1000,1000);m.add(5,0);
    const auto s=m.snapshot();
    assert(s.blocks==3&&s.elapsedNs==3000&&s.budgetNs==3000&&s.overruns==1);
    assert(s.averagePercent()==100.0&&s.peakPercent()==150.0);
    m.reset();assert(m.snapshot().blocks==0&&m.snapshot().peakPercent()==0);
    {Scope scope(&m,1000000000);}assert(m.snapshot().blocks==1);
    {Scope disabled(nullptr,1000);}assert(m.snapshot().blocks==1);
}
