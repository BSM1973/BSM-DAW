#pragma once
#include <algorithm>
#include <array>

namespace LibertyNoOverlap
{
// Pure geometry shared by the arrangement track-creation overlay and
// the full-screen mixer. Coordinates are local to MainComponent except
// mixer header/pane, which are local to the mixer at y = transportHeight.
struct Rect
{
    int x=0,y=0,w=0,h=0;
    constexpr int right() const noexcept { return x+w; }
    constexpr int bottom() const noexcept { return y+h; }
};
constexpr bool intersects(Rect a,Rect b) noexcept
{
    return a.x<b.right()&&b.x<a.right()&&a.y<b.bottom()&&b.y<a.bottom();
}
constexpr bool inside(Rect r,Rect bounds) noexcept
{
    return r.x>=bounds.x&&r.y>=bounds.y&&r.right()<=bounds.right()&&r.bottom()<=bounds.bottom();
}
constexpr int transportHeight=76;
constexpr int arrangementRowsTop=108;
constexpr int creationZoneWidth=330;
constexpr int mixerToolbarHeight=54;
constexpr int mixerTopY=66;
constexpr int mixerBottomInset=38;
constexpr int routingPaneWidth=224;
constexpr int pinnedMasterWidth=146;
constexpr int pinnedMasterGap=12;
constexpr int mixerStripPitch=150;
constexpr Rect logo{8,8,178,60};
constexpr Rect audioButton{8,78,58,24};
constexpr Rect midiButton{72,78,50,24};
constexpr Rect instrumentButton{128,78,94,24};
constexpr Rect busButton{228,78,58,24};
constexpr Rect heightIcon{292,79,22,21};
constexpr std::array<Rect,5> creationControls{
    audioButton,midiButton,instrumentButton,busButton,heightIcon
};
// Keep creation row entirely above Arrangement track rows at y=108.
constexpr Rect creationZone{0,transportHeight,creationZoneWidth,30};
// A 150px-width channel strip (146px active width) keeps the three
// separate interactions strictly out of one another's hit rectangles.
constexpr Rect mixerNameEditor{7,10,132,24};
constexpr Rect mixerKindCaption{6,36,134,15};
constexpr Rect mixerRoutingCapsule{8,56,130,24};
constexpr bool mixerHeaderGeometryValid() noexcept
{
    return !intersects(mixerNameEditor,mixerKindCaption)&&
           !intersects(mixerKindCaption,mixerRoutingCapsule)&&
           !intersects(mixerNameEditor,mixerRoutingCapsule);
}
inline Rect mixerViewButton(int windowWidth) noexcept
{
    return {windowWidth-148,12,130,28};
}
// The search field only occupies space when the header is wide enough.
// Narrow windows retain the original controls without any collision.
constexpr int mixerSearchThreshold=1100;
constexpr int mixerMeterButtonThreshold=1280;
inline Rect mixerSearchField(int windowWidth) noexcept
{
    return windowWidth>=mixerSearchThreshold?
        Rect{windowWidth-378,12,218,28}:Rect{};
}
inline Rect mixerSearchInput(int windowWidth) noexcept
{
    const auto field=mixerSearchField(windowWidth);
    return field.w>0?Rect{field.x,field.y,field.w-28,field.h}:Rect{};
}
inline Rect mixerSearchClear(int windowWidth) noexcept
{
    const auto field=mixerSearchField(windowWidth);
    return field.w>0?Rect{field.right()-25,field.y+2,23,24}:Rect{};
}
inline Rect mixerMeterButton(int windowWidth) noexcept
{
    return windowWidth>=mixerMeterButtonThreshold?
        Rect{windowWidth-468,12,76,28}:Rect{};
}
inline Rect mixerHint(int windowWidth) noexcept
{
    return {creationZoneWidth+14,15,
            std::max(0,windowWidth-creationZoneWidth-190-
                (windowWidth>=mixerSearchThreshold?230:0)-
                (windowWidth>=mixerMeterButtonThreshold?90:0)),20};
}
inline Rect routingPanel(int windowWidth,int windowHeight,bool open) noexcept
{
    return open?Rect{std::max(0,windowWidth-routingPaneWidth),
           mixerTopY,routingPaneWidth,std::max(0,windowHeight-mixerTopY-mixerBottomInset)}
        :Rect{windowWidth,mixerTopY,0,0};
}
inline Rect mixerStripViewport(int windowWidth,int windowHeight,bool routingOpen) noexcept
{
    return {0,mixerTopY,
        std::max(0,windowWidth-(routingOpen?routingPaneWidth:0)),
        std::max(0,windowHeight-mixerTopY-mixerBottomInset)};
}
// Separate disjoint rectangles: routing inspector | pinned Master | scrollable bank.
inline Rect pinnedMaster(int width,int height,bool routing) noexcept
{
    const auto all=mixerStripViewport(width,height,routing);
    return {all.right()-pinnedMasterWidth,all.y,pinnedMasterWidth,all.h};
}
inline Rect scrollableChannels(int width,int height,bool routing) noexcept
{
    const auto all=mixerStripViewport(width,height,routing);
    const auto master=pinnedMaster(width,height,routing);
    return {all.x,all.y,std::max(0,master.x-all.x-pinnedMasterGap),all.h};
}
inline int channelMaxScroll(int channels,int areaWidth) noexcept
{
    return std::max(0,std::max(0,channels)*mixerStripPitch-
                      std::max(0,areaWidth-12));
}
inline int channelScroll(int current,int increment,int limit) noexcept
{
    return std::clamp(current+increment,0,std::max(0,limit));
}
// Centers a channel when there is space, or reveals it at the bank edge.
// Never changes the identity or order of a track, FX insert or BUS.
inline int centeredChannelOffset(int index,int channels,int viewportWidth) noexcept
{
    if(index<0||index>=channels)return 0;
    const int target=12+index*mixerStripPitch+(mixerStripPitch-4)/2-
                     std::max(0,viewportWidth)/2;
    return std::clamp(target,0,channelMaxScroll(channels,viewportWidth));
}
inline bool pinnedMasterDoesNotOverlap(int width,int height,bool routing) noexcept
{
    const auto master=pinnedMaster(width,height,routing);
    const auto tracks=scrollableChannels(width,height,routing);
    const auto inspector=routingPanel(width,height,routing);
    return !intersects(master,tracks)&&!intersects(master,inspector)&&
           !intersects(tracks,inspector)&&tracks.right()+pinnedMasterGap<=master.x&&
           master.right()==mixerStripViewport(width,height,routing).right()&&
           tracks.w>=pinnedMasterWidth;
}
constexpr bool pointIn(Rect area,int x,int y) noexcept
{
    return area.w>0&&area.h>0&&x>=area.x&&x<area.right()&&
           y>=area.y&&y<area.bottom();
}
inline bool mixerChannelHit(int width,int height,bool routed,int x,int y) noexcept
{
    return pointIn(scrollableChannels(width,height,routed),x,y);
}
inline bool mixerMasterHit(int width,int height,bool routed,int x,int y) noexcept
{
    return pointIn(pinnedMaster(width,height,routed),x,y);
}
inline Rect routingCloseButton(int windowWidth) noexcept
{
    return {windowWidth-routingPaneWidth+186,mixerTopY+15,24,24};
}
inline Rect routingOptionButton(int windowWidth,int option) noexcept
{
    return {windowWidth-routingPaneWidth+16,mixerTopY+65+option*37,
            routingPaneWidth-32,29};
}
constexpr bool creationControlsHaveSafeGaps() noexcept
{
    constexpr Rect bounds{0,0,creationZoneWidth,130};
    for(std::size_t i=0;i<creationControls.size();++i)
    {
        if(!inside(creationControls[i],bounds)||intersects(creationControls[i],logo))
            return false;
        for(std::size_t j=i+1;j<creationControls.size();++j)
            if(intersects(creationControls[i],creationControls[j]))return false;
    }
    return true;
}
inline bool mixerHeaderIsSeparate(int windowWidth) noexcept
{
    const auto hint=mixerHint(windowWidth);
    const auto button=mixerViewButton(windowWidth);
    const auto search=mixerSearchField(windowWidth);
    const auto input=mixerSearchInput(windowWidth);
    const auto clear=mixerSearchClear(windowWidth);
    const auto meter=mixerMeterButton(windowWidth);
    return windowWidth>=600&&hint.x>=creationZone.right()&&
           hint.right()+12<=button.x&&
           button.right()<=windowWidth&&hint.y>=0&&
           button.y>=0&&button.bottom()<=mixerToolbarHeight&&
           hint.bottom()<=mixerToolbarHeight&&
           (search.w==0||(!intersects(hint,search)&&
               inside(input,search)&&inside(clear,search)&&
               !intersects(input,clear)&&
               search.x>=creationZoneWidth&&
               search.right()+12<=button.x&&
               search.y>=0&&search.bottom()<=mixerToolbarHeight))&&
           (meter.w==0||(!intersects(meter,hint)&&
                !intersects(meter,search)&&!intersects(meter,button)&&
                meter.right()+12<=search.x&&
                meter.y>=0&&meter.bottom()<=mixerToolbarHeight));
}
inline bool routingHasNoOverlap(int width,int height) noexcept
{
    const auto panel=routingPanel(width,height,true);
    const auto viewport=mixerStripViewport(width,height,true);
    if(intersects(panel,viewport)||panel.right()!=width||
       !inside(routingCloseButton(width),panel))return false;
    for(int i=0;i<9;++i)
    {
        const auto option=routingOptionButton(width,i);
        if(!inside(option,panel)||intersects(option,routingCloseButton(width)))
            return false;
        for(int j=0;j<i;++j)
            if(intersects(option,routingOptionButton(width,j)))return false;
    }
    return true;
}
static_assert(creationZone.bottom()<=arrangementRowsTop,
              "Liberty: creation toolbar must never cover Arrangement track rows");
static_assert(creationControlsHaveSafeGaps(),
              "Liberty: controls must not overlap the wordmark or each other");
static_assert(mixerHeaderGeometryValid(),
              "Liberty: name editor, kind label and routing must never overlap");
}
