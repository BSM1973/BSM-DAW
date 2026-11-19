// Phase 5 professional Step Sequencer acceptance suite.
// No JUCE dependency: tests deterministic geometry, note gestures, MIDI
// scheduling across buffers and tempo changes, project-transfer invariants.
#include "PolyphonicStepGridLayout.h"
#include "PolyphonicStepGridEditing.h"
#include "PolyphonicStepGridMidiBridge.h"
#include "PolyphonicStepGridTransport.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <utility>
#include <limits>
#include <vector>

using LibertyPolyGridUI::Rect;
using LibertyPolyGridUI::Layout;
using Grid = LibertyPolyphonicGrid;
using Edit = LibertyPolyGridEditing;

static bool inside(Rect r, int width, int height) noexcept
{
    return r.w > 0 && r.h > 0 && r.x >= 0 && r.y >= 0 &&
        r.x + r.w <= width && r.y + r.h <= height;
}
static void checkGeometry(int width, int height)
{
    auto l = Layout::make(width,height);
    assert(l.valid);
    std::vector<Rect> interactive;
    const auto add = [&](Rect r)
    {
        assert(inside(r,width,height));
        for (const auto& existing : interactive) assert(!r.intersects(existing));
        interactive.push_back(r);
    };
    // Omit parent surfaces (clipPanel/editorArea). Every actionable rectangle
    // must be inside the window and disjoint from all other actionable ones.
    add(l.createClip);
    add(l.classicMode);
    add(l.pianoExport);
    add(l.pianoImport);
    add(l.undoButton);
    add(l.redoButton);
    add(l.editMinus);
    add(l.editPlus);
    add(l.editValue);
    for (int bank=0;bank<8;++bank) add(l.banks[(size_t)bank]);
    for (int tab=0;tab<6;++tab) add(l.editorTabs[(size_t)tab]);
    for (int row=0;row<8;++row)
    {
        add(l.pitchLabels[(size_t)row]);
        add(l.muteButtons[(size_t)row]);
        add(l.soloButtons[(size_t)row]);
        for (int c=0;c<4;++c) add(l.rowControls[(size_t)row][(size_t)c]);
        for (int step=0;step<16;++step)
            add(l.cells[(size_t)row][(size_t)step]);
    }
    // Readable dynamic fallback must not draw negative bounds.
    assert(inside(l.clipPanel,width,height));
    assert(inside(l.editorArea,width,height));
    assert(inside(l.patternHeader,width,height));
    assert(inside(l.gridHeader,width,height));
    assert(inside(l.parameterHeader,width,height));
    assert(!l.clipPanel.intersects(l.parameterHeader));
    assert(!l.clipPanel.intersects(l.editorArea));
    // All four clip-panel controls must be strictly within the clip panel.
    for (auto r : {l.createClip,l.classicMode,l.pianoExport,l.pianoImport})
    {
        assert(l.clipPanel.contains(r.x,r.y));
        assert(l.clipPanel.contains(r.x+r.w-1,r.y+r.h-1));
    }
    // Page navigation must preserve all sixteen column hit targets.
    for (int p=0;p<4;++p)
    {
        const auto page=Layout::make(width,height,p);
        assert(page.firstStep==p*16);
        for (int row=0;row<8;++row)
            for (int step=0;step<16;++step)
                assert(page.cells[(size_t)row][(size_t)step].w>=1);
    }
}
static void verify13Inch()
{
    // Screen sizes are logical pixels, not claims about specific Mac models.
    // The first pair is a typical 13-inch usable resolution; narrower sizes
    // exercise the editor when docked beside the track/navigation controls.
    for (const auto& size: {std::pair<int,int>{1280,800},{1440,900},
                           {1150,500},{1130,380},{970,330},{950,300},
                           {920,370},{900,300},{800,280},{760,300},
                           {650,240},{510,230},{510,216}})
        checkGeometry(size.first,size.second);
    // Boundary sweep: catch 1-pixel collisions when compact/full switches.
    for (int w=510;w<=1440;w+=17)
        for (int h=216;h<=550;h+=19)
            if (Layout::make(w,h).valid) checkGeometry(w,h);
}
static void verifyGesturesAndUndo()
{
    Grid grid; grid.active=true;
    auto a=grid.addNote(0,0,240,110);
    auto b=grid.addNote(1,0,480,92);
    auto c=grid.addNote(7,14,480,83);
    assert(a && b && c && grid.render().size()==3);
    Edit e; e.select({0,a});e.select({1,b},true);
    auto baseline=grid;
    e.begin(grid);
    assert(e.moveFrom(baseline,grid,2,3));
    assert(e.commit(grid));
    assert(grid.noteAt(2,3)->id==a && grid.noteAt(3,3)->id==b);
    assert(e.undo(grid) && Edit::same(baseline,grid));
    assert(e.redo(grid) && grid.noteAt(2,3)->id==a);
    e.clearSelection();e.select({2,a});e.select({3,b},true);
    baseline=grid;
    e.begin(grid);
    assert(e.resizeFrom(baseline,grid,2));
    assert(e.commit(grid));
    assert(grid.noteAt(2,3)->lengthTicks==720);
    assert(grid.noteAt(3,3)->lengthTicks==960);
    assert(e.undo(grid) && Edit::same(grid,baseline));
    assert(e.redo(grid));
    const auto current=grid;
    e.begin(grid);
    assert(e.applyDelta(grid,Edit::Property::Velocity,-17));
    assert(e.applyDelta(grid,Edit::Property::Chance,-30));
    assert(e.applyDelta(grid,Edit::Property::Offset,30));
    assert(e.applyDelta(grid,Edit::Property::Repeat,2));
    assert(e.applyDelta(grid,Edit::Property::Pitch,4));
    assert(e.applyDelta(grid,Edit::Property::Length,-240));
    assert(e.commit(grid));
    assert(e.undo(grid) && Edit::same(grid,current)); // Six properties = one Undo.
    assert(e.redo(grid));
    const auto saved=grid;
    e.begin(grid);
    assert(e.removeSelected(grid));
    assert(e.commit(grid));
    assert(grid.render().size()==1);
    assert(e.undo(grid) && Edit::same(grid,saved));
    e.clearHistory();
    assert(!e.canUndo() && !e.canRedo());
    // Near the end of a cycle, moving a long note must be rejected rather
    // than silently shortening its independent musical duration.
    e.select({2,a});
    const auto edgeBaseline=grid;
    assert(!e.moveFrom(edgeBaseline,grid,0,12));
    assert(Edit::same(edgeBaseline,grid));
    // A rejected drag may not corrupt the existing notes or their lengths.
    e.clearSelection();
    e.select({7,c});
    const auto unchanged=grid;
    assert(!e.moveFrom(unchanged,grid,1,0));
    assert(Edit::same(grid,unchanged));
}
struct Midi {std::int64_t startTick=0,lengthTicks=240;std::uint8_t pitch=60,velocity=100,channel=1;
             bool muted=false;std::uint8_t probability=100;};
static void verifyMidiExchange()
{
    std::vector<Midi> in{{0,240,72,127,1,false,100},
                         {0,480,64,80,2,true,50},
                         {1200,240,69,75,3,false,0}};
    Grid grid;
    assert(LibertyPolyGridMidi::importPianoRoll(in,grid,16,240));
    assert(LibertyPolyGridMidi::matchesPianoRoll(grid,in));
    const auto before=grid;
    in.push_back(in.front()); // Conflict must be rejected without data loss.
    assert(!LibertyPolyGridMidi::importPianoRoll(in,grid,16,240));
    assert(Edit::same(before,grid));
    in.pop_back();
    in[0].startTick=1; // Out-of-grid microtiming never quantized silently.
    assert(!LibertyPolyGridMidi::importPianoRoll(in,grid,16,240));
    assert(Edit::same(before,grid));
}
static void verifyTempoAndCycles()
{
    // MIDI quarter = 960 ticks; 16 steps * 240 ticks = 4 quarters.
    for (const double tempo: {60.0,78.0,90.0,120.0,144.0,180.0,240.0})
    {
        const double cycle=4.0*60.0/tempo;
        for (const int rate : {44100,48000,96000})
        for (const int block : {32,64,128,256,512,1024})
        {
            const double duration=(double)block/(double)rate;
            // Inspect 5 full cycles with a frame-accurate sweep of block edges.
            int starts=0,offs=0;
            const double noteStart=0.0,noteEnd=cycle/8.0;
            for (double pos=0.0;pos<5*cycle;pos+=duration)
            {
                const auto finish=std::min(5*cycle,pos+duration);
                LibertyStepTransport::eachLoopOccurrence(pos,finish,0.0,cycle,noteStart,noteEnd,
                    [&](double on,double off)
                    {
                        if (on>=pos && on<finish) ++starts;
                        if (off>=pos && off<finish) ++offs;
                    });
            }
            assert(starts==5);
            assert(offs==5);
        }
        // Restart from the middle of a held note: resume still sees it.
        int held=0;
        LibertyStepTransport::eachLoopOccurrence(2*cycle+cycle/16.0,
            2*cycle+cycle/16.0+0.001,0.0,cycle,0.0,cycle/8.0,
            [&](double on,double off){++held;assert(on<2*cycle+cycle/16.0&&off>2*cycle+cycle/16.0);});
        assert(held==1);
    }
    // Start with an offset/late clip: no playback before its start.
    int before=0;
    LibertyStepTransport::eachLoopOccurrence(0.0,0.25,1.0,2.0,0.0,0.25,
        [&](double,double){++before;});
    assert(before==0);
    // Degenerate external transport inputs are silently rejected.
    int invalid=0;
    LibertyStepTransport::eachLoopOccurrence(0.0,1.0,0.0,0.0,0.0,0.5,
        [&](double,double){++invalid;});
    LibertyStepTransport::eachLoopOccurrence(std::numeric_limits<double>::quiet_NaN(),1.0,
        0.0,1.0,0.0,0.5,[&](double,double){++invalid;});
    assert(invalid==0);
}
int main()
{
    verify13Inch();
    verifyGesturesAndUndo();
    verifyMidiExchange();
    verifyTempoAndCycles();
    std::cout<<"Liberty Phase 5: 13-inch layout, no overlaps, grouped Undo, MIDI loops OK\n";
}
