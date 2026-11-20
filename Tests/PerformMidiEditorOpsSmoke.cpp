#include "PerformMidiEditorOps.h"
#include <cassert>
#include <cstdint>
#include <vector>
struct Note
{
    std::int64_t startTick=0;
    std::int64_t lengthTicks=960;
    std::uint8_t pitch=60,velocity=100;
    bool muted=false;
};
int main()
{
    using namespace LibertyPerformMidiEdit;
    std::vector<Note> notes{{0,960,60,90,false},{1000,500,62,100,false}};
    History<Note> hist;
    hist.checkpoint(notes);
    assert(edit(notes,0,2300,960,140,0,2400));
    assert(notes[0].pitch==127 && notes[0].velocity==1);
    assert(notes[0].lengthTicks==100);
    assert(hist.undoStep(notes) && notes[0].pitch==60);
    assert(hist.redoStep(notes) && notes[0].pitch==127);
    hist.checkpoint(notes);
    humanize(notes,2400,0,123);
    assert(notes[0].startTick==2300);
    humanize(notes,2400,100,456);
    for(auto& n:notes) {
        assert(n.startTick>=0);
        assert(n.startTick+n.lengthTicks<=2400);
        assert(n.velocity>=1 && n.velocity<=127);
    }
    assert(snap(239)==240 && snap(480)==480);
    assert(!edit(notes,42,0,1,60,100,2400));
    hist.clear();
    assert(!hist.undoStep(notes) && !hist.redoStep(notes));
    return 0;
}
