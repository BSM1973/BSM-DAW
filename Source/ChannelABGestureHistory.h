#pragma once

// Tracks only physical parameter gestures for CANAL A/B history.
// A continuous rotary drag is one Undo step, but independent knobs,
// numeric entry and keyboard changes remain separate steps.
namespace LibertyChannelABGestureHistory
{
class Tracker final
{
public:
    static constexpr int knobCount=8;

    void begin(int knob) noexcept
    {
        if(!valid(knob))return;
        active=knob;
        alreadyRecorded=false;
    }

    bool shouldRecord(int knob) noexcept
    {
        if(!valid(knob))return false;
        if(active!=knob)return true;
        if(alreadyRecorded)return false;
        alreadyRecorded=true;
        return true;
    }

    void end(int knob) noexcept
    {
        if(knob!=active)return;
        active=-1;
        alreadyRecorded=false;
    }

    void reset() noexcept
    {
        active=-1;
        alreadyRecorded=false;
    }

private:
    static constexpr bool valid(int knob) noexcept
    {
        return knob>=0 && knob<knobCount;
    }

    int active=-1;
    bool alreadyRecorded=false;
};
}
