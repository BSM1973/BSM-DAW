#include "LibertyDrumPatternLibrary.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <set>
#include <string>
int main()
{
    using namespace LibertyDrumPatterns;
    static_assert(styleCount*variationsPerStyle==100,"100 distinct presets");
    std::set<Grid> unique;
    std::set<std::string> labels;
    for(int style=0;style<styleCount;++style)
    {
        int totalKick=0,totalSnare=0;
        for(int v=0;v<variationsPerStyle;++v)
        {
            const auto grid=make(style,v);
            int count=0;
            for(int i=0;i<instrumentCount*steps;++i)
            {
                const auto vel=grid[(std::size_t)i];
                assert(vel<=127);
                count+=(vel>0);
            }
            assert(count>=4&&count<=256);
            totalKick+=(grid[0]>0);
            totalSnare+=(grid[(std::size_t)(1*steps+4)]>0);
            assert(unique.insert(grid).second&&"Preset duplicate, not a unique rhythm");
            const std::string label=std::string(styleName(style))+" / "+name(style,v);
            assert(labels.insert(label).second);
        }
        assert(totalKick>0);
        std::cout<<styleName(style)<<": 10 grooves, "<<totalSnare
            <<" hits at snare beat 2"<<'\n';
    }
    assert(unique.size()==100&&labels.size()==100);
    const auto reggae=make(5,0);
    assert(reggae[(std::size_t)(0*steps+8)]!=0);
    assert(reggae[(std::size_t)(0*steps+0)]==0); // One drop
    const auto club=make(4,0);
    for(auto s:{0,4,8,12})assert(club[(std::size_t)(0*steps+s)]!=0);
    const auto metal=make(3,1);
    for(int s=0;s<steps;++s)assert(metal[(std::size_t)(0*steps+s)]!=0);
    const auto trap=make(9,0);
    assert(trap[(std::size_t)(1*steps+8)]!=0);
    const auto invalid=make(-1,100);
    for(auto value:invalid)assert(value==0);
    std::cout<<"PASS 100 genre-specific unique MIDI patterns"<<std::endl;
}
