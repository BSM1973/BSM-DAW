#include "../Source/ChannelABHistory.h"
#include <array>
#include <cassert>
#include <iostream>
#include <string>

struct State
{
    std::array<std::array<float,3>,2> banks{};
    int active=0;
    std::array<std::string,2> presetNames{};
};

int main()
{
    LibertyChannelABHistory::History<State,3> history;
    State current{};
    current.presetNames={"INIT A","INIT B"};
    assert(!history.canUndo() && !history.canRedo());
    assert(!history.undo(current) && !history.redo(current));
    // Copy one value A->B. Undo must not affect other values or labels.
    history.record(current);
    current.banks[1][0]=4.5f;
    assert(history.canUndo() && !history.canRedo());
    assert(history.undo(current));
    assert(current.banks[1][0]==0.f && current.presetNames[1]=="INIT B");
    assert(history.canRedo());
    assert(history.redo(current));
    assert(current.banks[1][0]==4.5f && current.banks[1][1]==0.f);
    // Save a bank switch and a preset change as one reversible transaction.
    history.record(current);
    current.active=1;
    current.presetNames[1]="BRILLANT";
    assert(history.undo(current));
    assert(current.active==0 && current.presetNames[1]=="INIT B");
    assert(history.redo(current));
    assert(current.active==1 && current.presetNames[1]=="BRILLANT");
    // A new edit after Undo invalidates the old Redo path.
    assert(history.undo(current));
    history.record(current);
    current.banks[0][2]=-7.f;
    assert(!history.canRedo());
    assert(history.undo(current));
    assert(current.banks[0][2]==0.f);
    // Bounded history does not grow beyond the configured capacity.
    for(int i=0;i<8;++i)
    {
        history.record(current);
        current.banks[0][0]=(float)i;
    }
    assert(history.undoDepth()==3);
    for(int i=0;i<3;++i)assert(history.undo(current));
    assert(!history.undo(current));
    history.clear();
    assert(!history.canUndo() && !history.canRedo());
    std::cout<<"CANAL A/B Undo/Redo: snapshots, labels, redo invalidation, bounded depth OK\n";
}
