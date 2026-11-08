#include "../Source/MixerBusRouting.h"
#include <cassert>

int main()
{
    using namespace LibertyMixerBusRouting;
    static_assert(maxBuses==8, "A maximum of eight BUS channels is supported");
    assert(destination(-1,0)==-1);
    assert(destination(0,0)==-1);
    for(int count=1;count<=maxBuses;++count)
    {
        assert(destination(-1,count)==-1);
        for(int bus=0;bus<count;++bus)
        {
            assert(destination(bus,count)==bus);
            assert(matchesRestored(bus,bus,count));
            assert(!matchesRestored(bus,-1,count));
        }
        assert(destination(count,count)==-1);
        assert(matchesRestored(count,-1,count));
        assert(!matchesRestored(count,count,count));
    }
    assert(destination(7,8)==7);
    assert(destination(8,8)==-1);
    assert(destination(7,100)==7);
    assert(destination(8,100)==-1);
    assert(matchesRestored(-7,-1,8));
    assert(!matchesRestored(-1,0,8));
    // An old project may lack MixerRouting entirely, hence Master (-1).
    assert(matchesRestored(-1,-1,0));
    return 0;
}
