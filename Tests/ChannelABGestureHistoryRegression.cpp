#include "../Source/ChannelABGestureHistory.h"
#include "../Source/ChannelABHistory.h"
#include <cassert>
#include <iostream>

int main()
{
    LibertyChannelABGestureHistory::Tracker gesture;
    LibertyChannelABHistory::History<int> history;
    int value=0;

    auto change=[&](int knob,int next)
    {
        if(gesture.shouldRecord(knob))history.record(value);
        value=next;
    };

    assert(!history.canUndo() && !history.canRedo());
    // One continuous drag should always consume exactly one Undo step.
    gesture.begin(0);
    change(0,1);change(0,2);change(0,3);
    assert(history.undoDepth()==1);
    gesture.end(0);
    assert(history.undo(value) && value==0);
    assert(history.redoDepth()==1);
    assert(history.redo(value) && value==3);

    // The next drag on that same knob is a separate Undo step.
    gesture.begin(0);
    change(0,4);change(0,5);
    gesture.end(0);
    assert(history.undoDepth()==2);
    assert(history.undo(value) && value==3);

    // New changes clear the redo path and do not merge distinct knobs.
    gesture.begin(1);
    change(1,6);change(1,7);
    change(2,8); // independent change during a knob-1 gesture
    gesture.end(0); // stale end must not cancel knob-1 gesture
    change(1,9); // still the same knob-1 gesture
    gesture.end(1);
    assert(history.undoDepth()==3);
    assert(history.redoDepth()==0);
    assert(history.undo(value) && value==7);
    assert(history.undo(value) && value==3);
    assert(history.undo(value) && value==0);

    // Direct numeric edits are individual steps (no dragging).
    gesture.reset();
    history.clear();
    value=10;
    change(7,11);change(7,12);
    assert(history.undoDepth()==2);
    assert(history.undo(value) && value==11);
    assert(history.undo(value) && value==10);

    // Empty gestures and invalid indices never create history.
    history.clear();
    gesture.begin(3);
    gesture.end(3);
    assert(history.undoDepth()==0);
    assert(!gesture.shouldRecord(-1));
    assert(!gesture.shouldRecord(8));
    assert(history.undoDepth()==0);
    std::cout<<"CANAL gesture history: grouped drags, independent knobs, numeric edits OK\n";
}
