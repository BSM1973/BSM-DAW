#include "PolyphonicStepGridPages.h"
#include "PolyphonicStepGridLayout.h"
#include "PolyphonicStepGridEditing.h"
#include <cassert>
#include <iostream>

int main()
{
    LibertyPolyphonicGrid grid;
    LibertyPolyGridEditing history;
    int page = 0;
    grid.active = true;
    const auto first = grid.addNote(0, 0, grid.stepTicks);
    assert(first && grid.stepCount == 16);
    assert(LibertyPolyGridPages::willExtend(grid,page));
    grid.lanes[1].steps = 8; // Explicit polymeter must survive extension.
    history.begin(grid);
    assert(LibertyPolyGridPages::advance(grid,page));
    assert(history.commit(grid));
    assert(page == 1 && grid.stepCount == 32);
    assert(grid.lanes[0].steps == 32);
    assert(grid.lanes[1].steps == 8);
    const auto second = grid.addNote(0,20,grid.stepTicks * 2);
    assert(second && grid.noteAt(0,20));
    assert(grid.render().size() == 2);
    assert(LibertyPolyGridPages::willExtend(grid,page));
    history.begin(grid);
    assert(LibertyPolyGridPages::advance(grid,page));
    assert(history.commit(grid));
    assert(page == 2 && grid.stepCount == 48 && grid.lanes[0].steps == 48);
    const auto third = grid.addNote(0,40,grid.stepTicks);
    assert(third);
    history.begin(grid);
    assert(LibertyPolyGridPages::advance(grid,page));
    assert(history.commit(grid));
    assert(page == 3 && grid.stepCount == 64);
    const auto fourth = grid.addNote(0,60,grid.stepTicks * 3);
    assert(fourth);
    assert(!LibertyPolyGridPages::willExtend(grid,page));
    assert(!LibertyPolyGridPages::advance(grid,page));
    assert(page == 0 && grid.stepCount == 64);
    assert(grid.render().size()==4);
    assert(grid.noteAt(0,20)->id == second);
    assert(grid.noteAt(0,40)->id == third);
    assert(grid.noteAt(0,60)->id == fourth);
    assert(LibertyPolyGridPages::firstStep(0)==0);
    assert(LibertyPolyGridPages::firstStep(3)==48);
    assert(LibertyPolyGridPages::lastStep(3)==64);
    const auto layout=LibertyPolyGridUI::Layout::make(760,300);
    assert(layout.valid);
    assert(!layout.previewButton.intersects(layout.clipPanel));
    assert(!layout.previewButton.intersects(layout.undoButton));
    assert(!layout.previewButton.intersects(layout.banks[0]));
    assert(!layout.previewButton.intersects(layout.createClip));
    // Three expansions are three atomic history entries; undo must restore
    // original note cycle and row length without deleting the first note.
    assert(history.undo(grid));
    assert(grid.stepCount == 48);
    assert(grid.noteAt(0,0)->id == first);
    assert(history.undo(grid));
    assert(grid.stepCount == 32);
    assert(history.undo(grid));
    assert(grid.stepCount == 16);
    assert(grid.noteAt(0,0)->id == first);
    std::cout<<"Liberty Step Sequencer pages: 16 -> 32 -> 48 -> 64, no note loss and Undo OK\n";
}
