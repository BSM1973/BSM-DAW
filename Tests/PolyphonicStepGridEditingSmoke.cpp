#include "PolyphonicStepGridEditing.h"
#include "PolyphonicStepGridLayout.h"
#include <cassert>
#include <iostream>

int main()
{
    using Grid = LibertyPolyphonicGrid;
    using Editing = LibertyPolyGridEditing;
    Grid grid;
    grid.active = true;
    Editing edit;

    // Three notes, two simultaneous pitches; a long note crossing several steps.
    const auto a = grid.addNote(0, 0, 240);
    const auto b = grid.addNote(1, 0, 720);
    const auto c = grid.addNote(5, 6, 960);
    assert(a && b && c && grid.render().size() == 3);
    const auto baseline = grid;

    // Shift/Command selection, independent of note length and initial pitch.
    edit.select({0,a});
    edit.toggle({1,b});
    assert(edit.selection().size() == 2 && edit.contains({0,a}) && edit.contains({1,b}));
    edit.begin(grid);
    assert(edit.moveFrom(baseline,grid,1,2));
    edit.clearSelection();
    edit.select({1,a}); edit.select({2,b},true);
    assert(edit.commit(grid));
    assert(grid.noteAt(1,2)->id == a && grid.noteAt(2,2)->id == b);
    assert(grid.noteAt(1,2)->lengthTicks == 240 && grid.noteAt(2,2)->lengthTicks == 720);
    assert(!edit.moveFrom(baseline,grid,-1,-1)); // Out of bounds, no damage.
    assert(grid.noteAt(1,2)->id == a);

    // One Undo step must undo the whole multi-note move.
    assert(edit.undo(grid));
    assert(Grid{}.stepCount == grid.stepCount);
    assert(grid.noteAt(0,0)->id == a && grid.noteAt(1,0)->id == b);
    assert(edit.redo(grid) && grid.noteAt(1,2)->id == a);

    // Group-resize, with per-note lengths retained as independent values.
    const auto beforeResize=grid;
    edit.begin(grid);
    assert(edit.resizeFrom(beforeResize,grid,2));
    assert(edit.commit(grid));
    assert(grid.noteAt(1,2)->lengthTicks == 720);
    assert(grid.noteAt(2,2)->lengthTicks == 1200);
    assert(edit.undo(grid));
    assert(grid.noteAt(1,2)->lengthTicks == 240);
    assert(edit.redo(grid));

    // Each six property editor mutates real note data with a single history item.
    const auto start=grid;
    const auto editAndCheck=[&](Editing::Property p,int delta)
    {
        edit.begin(grid);
        assert(edit.applyDelta(grid,p,delta));
        assert(edit.commit(grid));
    };
    editAndCheck(Editing::Property::Velocity,-12);
    editAndCheck(Editing::Property::Offset,60);
    editAndCheck(Editing::Property::Length,240);
    editAndCheck(Editing::Property::Repeat,2);
    editAndCheck(Editing::Property::Pitch,3);
    editAndCheck(Editing::Property::Chance,-25);
    const auto first=Editing::find(grid,{1,a});
    const auto second=Editing::find(grid,{2,b});
    assert(first && second);
    assert(first->velocity==88 && second->velocity==88);
    assert(first->offsetTicks==60 && second->offsetTicks==60);
    assert(first->repeats==3 && first->pitchOffset==3 && first->probability==75);
    assert(grid.render().size() == 7); // 3 repeats * 2 notes + original C3.
    assert(grid.render()[0].pitch == 72); // A3 + three semitones
    for (int i=0;i<6;++i) assert(edit.undo(grid));
    assert(Editing::same(start,grid));
    for (int i=0;i<6;++i) assert(edit.redo(grid));
    assert(edit.canUndo() && !edit.canRedo());

    // Selection rectangle includes note lengths, not just their first cell.
    edit.clearSelection();
    edit.selectRectangle(grid,1,4,2,5);
    assert(edit.selection().size()==2);
    edit.begin(grid);
    assert(edit.removeSelected(grid));
    assert(edit.commit(grid));
    assert(grid.render().size()==1);
    assert(edit.undo(grid));
    assert(grid.render().size()==7);
    assert(edit.redo(grid));
    assert(edit.selection().empty());

    // Snapshots must not contain no-op edits.
    edit.begin(grid);
    assert(!edit.applyDelta(grid,Editing::Property::Chance,0));
    assert(!edit.commit(grid));
    edit.clearHistory();
    assert(!edit.canUndo() && !edit.canRedo());

    // All user-visible controls are inside their own rectangles at compact sizes.
    for (const int w: {510,760,950,1200})
    for (const int h: {230,320,500})
    {
        const auto l=LibertyPolyGridUI::Layout::make(w,h);
        assert(l.valid);
        assert(!l.undoButton.intersects(l.redoButton));
        assert(!l.undoButton.intersects(l.patternHeader));
        assert(!l.editMinus.intersects(l.editValue));
        assert(!l.editValue.intersects(l.editPlus));
        assert(!l.editMinus.intersects(l.editorTabs[0]));
        assert(!l.editPlus.intersects(l.editorTabs[5]));
        assert(l.editPlus.x+l.editPlus.w<=w);
    }
    std::cout << "Liberty Step Sequencer Phase 3: move, resize, six properties, Undo/Redo OK\n";
}
