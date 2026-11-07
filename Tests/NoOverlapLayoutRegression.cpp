#include "../Source/NoOverlapLayout.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace LibertyNoOverlap;
    static_assert(creationControlsHaveSafeGaps(),"creation controls overlap");
    static_assert(mixerHeaderGeometryValid(),"name editor and routing area overlap");
    static_assert(creationZone.bottom()<=arrangementRowsTop,
                  "creation toolbar overlaps Arrangement track headers");
    static_assert(!intersects(audioButton,midiButton),"audio and midi overlap");
    static_assert(!intersects(instrumentButton,busButton),"BUS overlaps instrument");
    static_assert(!intersects(busButton,heightIcon),"BUS overlaps height selector");
    static_assert(!intersects(logo,busButton),"logo overlaps BUS");

    for(int width:{600,1024,1100,1440,1672,1920,2560})
        for(int height:{620,720,890,1080})
        {
            assert(mixerHeaderIsSeparate(width));
            assert(!intersects(mixerHint(width),mixerViewButton(width)));
            const auto search=mixerSearchField(width);
            assert(search.w==0||(!intersects(mixerHint(width),search)&&
                   !intersects(search,mixerViewButton(width))));
            if(width>=mixerSearchThreshold)
            {
                assert(search.w==218);
                assert(search.y>=0&&search.bottom()<=mixerToolbarHeight);
                assert(search.right()+12<=mixerViewButton(width).x);
            }
            const auto hint=mixerHint(width);
            const auto globalHint=Rect{hint.x,hint.y+transportHeight,hint.w,hint.h};
            for(auto control:creationControls)
                assert(!intersects(globalHint,control));
            const auto panel=routingPanel(width,height,true);
            const auto viewport=mixerStripViewport(width,height,true);
            assert(!intersects(panel,viewport));
            assert(routingHasNoOverlap(width,height));
            assert(routingPanel(width,height,false).w==0);
            assert(mixerStripViewport(width,height,false).w==width);
            for(bool routed:{false,true})
            {
                assert(pinnedMasterDoesNotOverlap(width,height,routed));
                const auto bank=scrollableChannels(width,height,routed);
                const auto master=pinnedMaster(width,height,routed);
                assert(!intersects(bank,master));
                assert(bank.right()+pinnedMasterGap<=master.x);
                assert(master.right()==mixerStripViewport(width,height,routed).right());
                assert(!intersects(master,routingPanel(width,height,routed)));
                const int insideY=bank.y+15;
                assert(mixerChannelHit(width,height,routed,bank.x+1,insideY));
                assert(mixerMasterHit(width,height,routed,master.x+1,insideY));
                assert(!mixerChannelHit(width,height,routed,master.x+1,insideY));
                assert(!mixerMasterHit(width,height,routed,bank.x+1,insideY));
                assert(!mixerMasterHit(width,height,routed,bank.right(),insideY));
                assert(!mixerChannelHit(width,height,routed,master.right(),insideY));
                assert(!mixerMasterHit(width,height,routed,master.x+1,master.bottom()));
                assert(!mixerChannelHit(width,height,routed,bank.x+1,bank.y-1));
                if(routed)
                {
                    const auto route=routingPanel(width,height,true);
                    assert(!mixerChannelHit(width,height,true,route.x+1,insideY));
                    assert(!mixerMasterHit(width,height,true,route.x+1,insideY));
                }
                for(int n:{0,1,2,4,8,32})
                {
                    const int max=channelMaxScroll(n,bank.w);
                    assert(max>=0);
                    assert(channelScroll(0,-100,max)==0);
                    assert(channelScroll(0,max+100,max)==max);
                    assert(channelScroll(max,-100000,max)==0);
                    assert(centeredChannelOffset(-1,n,bank.w)==0);
                    assert(centeredChannelOffset(n,n,bank.w)==0);
                    for(int index=0;index<n;++index)
                    {
                        const int centered=centeredChannelOffset(index,n,bank.w);
                        assert(centered>=0&&centered<=max);
                        const int left=12+index*mixerStripPitch-centered;
                        assert(left<bank.w&&left+mixerStripPitch-4>0);
                    }
                }
            }
        }
    std::cout<<"Liberty: header, creation buttons and routing inspector have no overlap\n";
    return 0;
}
