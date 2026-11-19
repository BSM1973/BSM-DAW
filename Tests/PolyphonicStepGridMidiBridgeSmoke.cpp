#include "PolyphonicStepGridMidiBridge.h"
#include "PolyphonicStepGridTransport.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <cstdint>
#include <cmath>
struct MidiNote { std::int64_t startTick = 0, lengthTicks = 240;
    std::uint8_t pitch = 60, velocity = 100, channel = 1;
    bool muted = false; std::uint8_t probability = 100; };
int main()
{
    using namespace LibertyPolyGridMidi;
    std::vector<MidiNote> source = {
        {0,240,72,120,1,false,100},
        {0,480,64,85,3,false,75},
        {480,720,60,77,1,false,100},
        {960,240,64,95,2,false,25}
    };
    LibertyPolyphonicGrid grid;
    assert(importPianoRoll(source, grid, 16, 240));
    assert(grid.active && grid.hasNotes());
    assert(grid.render().size() == source.size());
    assert(matchesPianoRoll(grid, source));
    const auto backup = grid;
    // Notes on the same pitch/step, too many pitches and micro-timing cannot
    // be represented exactly: the original grid must survive import failure.
    auto bad = source; bad.push_back(source.front());
    assert(!importPianoRoll(bad, grid, 16, 240));
    assert(grid.render().size() == backup.render().size());
    bad = source; bad[0].startTick = 5;
    assert(!importPianoRoll(bad, grid, 16, 240));
    bad.clear();
    for (int i=0;i<9;i++) bad.push_back({0,240,(std::uint8_t)(60+i),100,1,false,100});
    assert(!importPianoRoll(bad, grid, 16, 240));
    // Loop plays the same pitch once in each 16-step cycle (at 120 BPM: 2 s).
    int on=0, resume=0;
    LibertyStepTransport::eachLoopOccurrence(1.99,2.02,0.0,2.0,0.0,0.125,
        [&](double start,double end){++on;assert(std::abs(start-2.0)<1e-8);assert(end>2.1);});
    assert(on==1);
    LibertyStepTransport::eachLoopOccurrence(2.03,2.05,0.0,2.0,0.0,0.125,
        [&](double start,double end){++resume;assert(start<2.03&&end>2.03);});
    assert(resume==1);
    int other=0;
    LibertyStepTransport::eachLoopOccurrence(2.5,2.51,0.0,2.0,0.0,0.125,
        [&](double,double){++other;});
    assert(other==0);
    int count=0;
    LibertyStepTransport::eachLoopOccurrence(0.0,4000.0,0.0,2.0,0.0,0.125,
        [&](double,double){++count;});
    assert(count<=512);
    std::cout<<"Step Sequencer phase 4: Piano Roll bridge and transport cycles OK\n";
}
