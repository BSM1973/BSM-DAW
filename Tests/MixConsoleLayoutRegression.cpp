#include "../Source/MixConsoleLayout.h"
#include <array>
#include <cassert>
#include <iostream>

int main(){
 using namespace LibertyMixerLayout;
 for(int h=400;h<=1200;h+=20){
   for(int mask=0;mask<16;++mask){
     std::array<bool,4> expanded{};
     for(int i=0;i<4;++i)expanded[(size_t)i]=(mask&(1<<i))!=0;
     for(int pref:{100,160,220,280,360}){
       for(auto kind:{Kind::Audio,Kind::Instrument}){
         for(bool inserts:{false,true}){
           const auto g=calculate(h,kind,expanded,inserts,pref);
           const int moduleCount=kind==Kind::Audio?4:0;
           int last=kind==Kind::Audio?90:116;
           if(moduleCount){
             for(int i:{3,0,1,2}){
               const auto& p=g.modules[(size_t)i];
               assert(p.headerY>=last);
               assert(p.bodyY>=p.headerY+21);
               assert(p.bodyHeight>=0);
               last=p.bodyY+p.bodyHeight;
             }
           }
           assert(g.insertsHeaderY>=last);
           assert(g.fxY>=g.insertsHeaderY+21);
           assert(g.visibleFx>=0&&g.visibleFx<=8);
           if(!inserts)assert(g.visibleFx==0);
           if(inserts&&g.visibleFx>0){
             assert(g.fxStep>=16&&g.fxStep<=24);
             assert(g.fxY+g.visibleFx*g.fxStep<=g.splitterY);
           }
           assert(g.insertsHeaderY+21<=g.splitterY);
           assert(g.splitterY+4<=g.panY);
           assert(g.panY+34<=g.muteY);
           assert(g.muteY+26<=g.faderY);
           assert(g.faderY+g.faderHeight==h-12);
           assert(g.faderHeight>=90);
         }
       }
     }
   }
 }
 // All four modules and eight effect slots must fit in a normal 900px window.
 const auto all=calculate(720,Kind::Audio,{true,true,true,true},true,250);
 assert(all.visibleFx==8&&all.faderHeight>=90);
 // Closing all sections restores a longer fader.
 const auto closed=calculate(720,Kind::Audio,{false,false,false,false},true,250);
 assert(closed.faderHeight>=all.faderHeight);
 // An independent collapsed insert rack never hides the fader.
 const auto noInserts=calculate(720,Kind::Audio,{true,true,true,true},false,250);
 assert(noInserts.visibleFx==0&&noInserts.faderHeight>=90);
 std::cout<<"Liberty mixer geometry: all widths and section combinations OK\n";
}
