#include "PerformMidiGenerator.h"
#include <cassert>
#include <cstdint>
#include <vector>
struct Note {
    std::int64_t startTick=0,lengthTicks=960;
    std::uint8_t pitch=60,velocity=100,channel=1;
    bool muted=false;
};
int main()
{
    using namespace LibertyPerformGenerator;
    std::vector<Note> chord{{0,960,60,100,1,false},{0,960,64,110,1,false},
                            {0,960,67,90,1,false}};
    const auto untouched=chord;
    auto p=Parameters{};
    p.kind=0;p.density=100;p.octaves=2;p.steps=16;
    const auto up=render(chord,3840,p);
    assert(!up.empty() && up.size()==16);
    assert(up.front().pitch==60 && up[1].pitch==64 && up[2].pitch==67);
    assert(up[3].pitch==72);
    p.direction=1;
    const auto down=render(chord,3840,p);
    assert(!down.empty() && down.front().pitch==79);
    p.direction=2;
    assert(render(chord,3840,p).size()==16);
    p.direction=3;p.seed=123;
    const auto randomA=render(chord,3840,p),randomB=render(chord,3840,p);
    assert(randomA.size()==randomB.size());
    for(size_t i=0;i<randomA.size();++i)
        assert(randomA[i].pitch==randomB[i].pitch);
    p.kind=1;p.variation=75;
    const auto varied=render(chord,3840,p);
    const auto variedAgain=render(chord,3840,p);
    assert(varied.size()==variedAgain.size());
    for(size_t i=0;i<varied.size();++i){
        assert(varied[i].startTick==variedAgain[i].startTick);
        assert(varied[i].pitch==variedAgain[i].pitch);
        assert(varied[i].startTick+varied[i].lengthTicks<=3840);
    }
    p.kind=2;p.rhythmStyle=0;p.density=100;
    const auto drums=render(chord,3840,p);
    assert(!drums.empty());
    bool kick=false,snare=false,hat=false;
    for(const auto& n:drums){
        assert(n.channel==10);
        kick|=n.pitch==36;snare|=n.pitch==38;hat|=n.pitch==42;
        assert(n.startTick>=0 && n.startTick+n.lengthTicks<=3840);
    }
    assert(kick && snare && hat);
    p.density=0;
    assert(render(chord,3840,p).empty());
    assert(chord[0].startTick==untouched[0].startTick);
    assert(chord[0].pitch==untouched[0].pitch);
    p.kind=0;p.density=100;
    assert(render(std::vector<Note>{},3840,p).empty());
    assert(render(chord,0,p).empty());
    return 0;
}
