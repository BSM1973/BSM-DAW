#pragma once
#include <algorithm>
#include <array>

// Pure mixer geometry: every section has its own rectangle. No JUCE dependency,
// so boundary cases can be checked in the fast CI before launching Liberty.
namespace LibertyMixerLayout {
enum class Kind { Audio, Instrument, Master, Midi };
struct Panel { int headerY=0, bodyY=0, bodyHeight=0; };
struct Geometry {
    std::array<Panel,4> modules{};
    int insertsHeaderY=0, fxY=0, fxStep=0, visibleFx=0;
    int splitterY=0, panY=0, muteY=0, faderY=0, faderHeight=0;
};
inline Geometry calculate(int height,Kind kind,const std::array<bool,4>& expanded,
                          bool insertsExpanded,int preferredFaderHeight) {
    Geometry g;
    const int h=std::max(1,height);
    if(kind==Kind::Master) {
        g.faderY=std::min(260,std::max(110,h-185));
        g.faderHeight=std::max(0,h-12-g.faderY);
        return g;
    }
    if(kind==Kind::Midi)return g;
    const bool audio=kind==Kind::Audio;
    const int top=audio?90:116;
    const int expandedCount=audio?static_cast<int>(std::count(expanded.begin(),expanded.end(),true)):0;
    int detailHeight=h>=700?43:h>=620?36:32;
    if(expandedCount>0) {
        const int detailBudget=h-12-90-78-top-4*21-26;
        detailHeight=std::min(detailHeight,std::max(0,detailBudget/expandedCount));
    }
    int y=top;
    if(audio) {
        // Visual order INPUT, EQ, GATE, COMP; indices preserve existing DSP routing.
        for(int index:{3,0,1,2}) {
            auto& p=g.modules[static_cast<size_t>(index)];
            p.headerY=y; y+=21; p.bodyY=y;
            p.bodyHeight=expanded[static_cast<size_t>(index)]?detailHeight:0;
            y+=p.bodyHeight;
        }
    }
    g.insertsHeaderY=y+2;
    g.fxY=y+26;
    const int preferred=std::clamp(preferredFaderHeight,100,360);
    const int lastFaderStart=h-12-90;
    const int required=g.fxY+(insertsExpanded?8*16:0)+78;
    g.faderY=std::min(lastFaderStart,std::max(h-12-preferred,required));
    const int available=std::max(0,g.faderY-78-g.fxY);
    if(insertsExpanded) {
        g.visibleFx=std::min(8,available/16);
        g.fxStep=g.visibleFx==8?std::clamp(available/8,16,24):16;
    }
    g.splitterY=g.faderY-77;
    g.panY=g.faderY-70;
    g.muteY=g.faderY-31;
    g.faderHeight=std::max(0,h-12-g.faderY);
    return g;
}
}
