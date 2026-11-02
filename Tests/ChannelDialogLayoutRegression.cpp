#include "../Source/ChannelDialogLayout.h"
#include <cassert>
#include <iostream>

int main(){
    for(int width:{760,800,900}){
      for(int height:{520,560,650}){
        const auto g=LibertyChannelDialogLayout::make(width,height);
        using LibertyChannelDialogLayout::Rect;
        const Rect full{0,0,width,height};
        const Rect title{28,17,width-260,26};
        const Rect subtitle{29,52,width-58,16};
        const Rect header{12,10,width-24,60};
        assert(full.contains(g.presets));
        assert(header.contains(g.presets));
        assert(!title.intersects(g.presets));
        assert(!subtitle.intersects(g.presets));
        for(const auto& card:g.sections)assert(!card.intersects(g.presets));
        for(const auto& card:g.sections)assert(full.contains(card));
        for(int i=0;i<4;++i){
            for(int j=i+1;j<4;++j)assert(!g.sections[i].intersects(g.sections[j]));
        }
        const int parent[]={0,0,1,2,2,3,3,3};
        for(int i=0;i<8;++i){
            const auto knob=g.controls[(size_t)i];
            assert(g.sections[(size_t)parent[i]].contains(knob));
            for(int j=i+1;j<8;++j)assert(!knob.intersects(g.controls[(size_t)j]));
        }
        const int toggleParents[]={1,2,3};
        for(int i=0;i<3;++i){
            const auto toggle=g.powers[(size_t)i];
            assert(g.sections[(size_t)toggleParents[i]].contains(toggle));
            for(const auto& knob:g.controls)assert(!toggle.intersects(knob));
        }
        for(int i=0;i<2;++i){
            const auto button=g.clipboard[(size_t)i];
            assert(full.contains(button));
            for(const auto& card:g.sections)assert(!card.intersects(button));
            for(const auto& action:g.actions)assert(!action.intersects(button));
            for(int j=i+1;j<2;++j)assert(!button.intersects(g.clipboard[(size_t)j]));
        }
        assert(full.contains(g.variant));
        for(const auto& card:g.sections)assert(!card.intersects(g.variant));
        for(const auto& action:g.actions)assert(!action.intersects(g.variant));
        for(const auto& button:g.clipboard)assert(!button.intersects(g.variant));
        for(int i=0;i<3;++i){
            assert(full.contains(g.actions[(size_t)i]));
            for(const auto& card:g.sections)assert(!card.intersects(g.actions[(size_t)i]));
            for(int j=i+1;j<3;++j)assert(!g.actions[(size_t)i].intersects(g.actions[(size_t)j]));
        }
      }
    }
    std::cout<<"Liberty channel inspector: no overlapping controls or actions\n";
}
