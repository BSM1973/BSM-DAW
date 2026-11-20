#include "PolyphonicStepGridDirectEdit.h"
#include <cassert>
#include <iostream>
#include <string>

int main()
{
    using namespace LibertyPolyGridDirect;
    using Grid = LibertyPolyphonicGrid;
    using Editor = LibertyPolyGridEditing;
    assert(chooseIntent(0,0) == DragIntent::Pending);
    assert(chooseIntent(4,4) == DragIntent::Pending);
    assert(chooseIntent(2,-18) == DragIntent::Parameter);
    assert(chooseIntent(20,-2) == DragIntent::Move);
    assert(chooseIntent(2,-20,true) == DragIntent::Move);
    assert(chooseIntent(10,-10) == DragIntent::Move);

    Grid grid;
    grid.active = true;
    const auto a=grid.addNote(0,0,240,100,1,80);
    const auto b=grid.addNote(1,2,480,85,1,60);
    assert(a && b);
    Editor editor;
    editor.select({0,a});
    editor.select({1,b},true);
    const auto original=grid;

    // Each of the six active tabs adjusts both selected notes directly.
    const struct {Property tab; int y; int expected;} cases[]={
        {Property::Velocity, -24, 12}, // Up increases velocity.
        {Property::Offset, -24, 40}, // Step is 240 ticks, 10 ticks/6px.
        {Property::Length, -24, 120}, // 60 ticks per 12px.
        {Property::Repeat, -42, 3},
        {Property::Pitch, -24, 2},
        {Property::Chance, 24, -12} // Down decreases chance.
    };
    for (const auto& c:cases)
    {
        editor.begin(grid);
        const int delta=deltaForVerticalDrag(c.tab,-c.y,grid.stepTicks);
        assert(delta==c.expected);
        assert(editor.applyDelta(grid,c.tab,delta));
        assert(editor.commit(grid));
    }
    assert(grid.lanes[0].events[0].velocity==112);
    assert(grid.lanes[1].events[0].velocity==97);
    assert(grid.lanes[0].events[0].offsetTicks==40);
    assert(grid.lanes[0].events[0].lengthTicks==360);
    assert(grid.lanes[1].events[0].lengthTicks==600);
    assert(grid.lanes[0].events[0].repeats==4);
    assert(grid.lanes[0].events[0].pitchOffset==2);
    assert(grid.lanes[0].events[0].probability==68);
    assert(grid.lanes[1].events[0].probability==48);

    const auto modified=grid;
    for (int i=0;i<6;++i) assert(editor.undo(grid));
    assert(Editor::same(original,grid));
    for (int i=0;i<6;++i) assert(editor.redo(grid));
    assert(Editor::same(modified,grid));

    // Moving back to the original mouse position must make no new history
    // entry; the helper always calculates the delta from the first snapshot.
    const auto snapshot=grid;
    editor.begin(grid);
    assert(editor.applyDelta(grid,Property::Velocity,
                             deltaForVerticalDrag(Property::Velocity,20,grid.stepTicks)));
    grid=snapshot;
    assert(!editor.commit(grid));
    assert(Editor::same(snapshot,grid));
    assert(!editor.canRedo());

    // Alt/precision mode has smaller increments, including both directions.
    assert(deltaForVerticalDrag(Property::Velocity,12,240,true)==2);
    assert(deltaForVerticalDrag(Property::Chance,-12,240,true)==-2);
    assert(deltaForVerticalDrag(Property::Offset,12,240,true)==4);
    assert(deltaForVerticalDrag(Property::Length,12,240,true)==15);
    assert(deltaForVerticalDrag(Property::Repeat,14,240,true)==0);
    assert(deltaForVerticalDrag(Property::Pitch,-28,240,true)==-1);
    assert(deltaForVerticalDrag(Property::Length,0,240)==0);

    const auto* note=Editor::find(grid,{0,a});
    assert(note);
    assert(noteValue(*note,Property::Velocity,grid.stepTicks)=="112");
    assert(noteValue(*note,Property::Offset,grid.stepTicks)=="+40 t");
    assert(noteValue(*note,Property::Length,grid.stepTicks)=="360 t");
    assert(noteValue(*note,Property::Repeat,grid.stepTicks)=="x4");
    assert(noteValue(*note,Property::Pitch,grid.stepTicks)=="+2 st");
    assert(noteValue(*note,Property::Chance,grid.stepTicks)=="68%");
    for (auto p: {Property::Velocity,Property::Offset,Property::Length,
                  Property::Repeat,Property::Pitch,Property::Chance})
        assert(normalizedValue(grid,*note,p)>=0.0f && normalizedValue(grid,*note,p)<=1.0f);

    // Extreme cursor input remains bounded to 32-bit MIDI editing deltas.
    assert(deltaForVerticalDrag(Property::Length,2147483647,1000000000)>=0);
    assert(deltaForVerticalDrag(Property::Length,-2147483647,1000000000)<=0);
    std::cout<<"Step Sequencer note-direct edit: six tabs, group gestures, Alt precision and Undo OK\n";
}
