#include "LibertyMotionMath.h"
#include <cassert>
int main()
{
    using namespace LibertyMotion;
    float x=0.f;
    for(int i=0;i<40;++i){const float n=advance(x,1.f,1.f/60.f);assert(n>=x&&n<=1.f);x=n;}
    assert(x==1.f);
    for(int i=0;i<40;++i){const float n=advance(x,0.f,1.f/60.f);assert(n<=x&&n>=0.f);x=n;}
    assert(x==0.f);
    assert(advance(0.2f,0.8f,0.f)==0.2f);
    assert(pulse(0.25,1.0)>0.99f);
    assert(pulse(0.75,1.0)<0.01f);
    assert(meterSegmentFill(0.625f,2,4)==0.5f);
    assert(meterSegmentFill(0.625f,1,4)==1.f);
    assert(meterSegmentFill(0.625f,3,4)==0.f);
    assert(meterSegmentFill(1.f,0,0)==0.f);
    setAnimationsEnabled(false);
    assert(!animationsEnabled());
    assert(pulse(0.75,1.0)==1.f);
    assert(meterSegmentFill(.625f,2,4)==.5f);
    setAnimationsEnabled(true);
    assert(pulse(.75,1.0)<.01f);
}
