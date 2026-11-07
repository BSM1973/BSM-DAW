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

    for(int width:{600,1024,1440,1672,1920,2560})
        for(int height:{620,720,890,1080})
        {
            assert(mixerHeaderIsSeparate(width));
            assert(!intersects(mixerHint(width),mixerViewButton(width)));
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
        }
    std::cout<<"Liberty: header, creation buttons and routing inspector have no overlap\n";
    return 0;
}
