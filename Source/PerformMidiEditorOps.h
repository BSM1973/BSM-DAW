#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace LibertyPerformMidiEdit
{
constexpr std::int64_t ticksPerBeat=960;
constexpr std::int64_t defaultGrid=240;

template<class Note>
bool edit(std::vector<Note>& notes,int index,std::int64_t start,
          std::int64_t duration,int pitch,int velocity,std::int64_t clipTicks)
{
    if(index<0 || index>=(int)notes.size() || clipTicks<=0)return false;
    auto& note=notes[(std::size_t)index];
    note.startTick=std::clamp<std::int64_t>(start,0,clipTicks-1);
    note.lengthTicks=std::clamp<std::int64_t>(duration,1,clipTicks-note.startTick);
    note.pitch=(std::uint8_t)std::clamp(pitch,0,127);
    note.velocity=(std::uint8_t)std::clamp(velocity,1,127);
    return true;
}
inline std::int64_t snap(std::int64_t ticks,std::int64_t grid=defaultGrid) noexcept
{
    if(grid<=0)return ticks;
    return ((ticks+grid/2)/grid)*grid;
}
inline std::uint32_t nextRandom(std::uint32_t& seed) noexcept
{
    seed=seed*1664525u+1013904223u;
    return seed;
}
template<class Note>
void humanize(std::vector<Note>& notes,std::int64_t clipTicks,
              int amount,std::uint32_t seed)
{
    amount=std::clamp(amount,0,100);
    const int ticks=(amount*120)/100;
    const int velocity=(amount*22)/100;
    for(auto& note:notes)
    {
        if(note.muted || clipTicks<=1)continue;
        const int delta=ticks==0?0:(int)(nextRandom(seed)%(std::uint32_t)(2*ticks+1))-ticks;
        const int dv=velocity==0?0:
            (int)(nextRandom(seed)%(std::uint32_t)(2*velocity+1))-velocity;
        const auto len=std::clamp<std::int64_t>(note.lengthTicks,1,clipTicks);
        note.startTick=std::clamp<std::int64_t>(note.startTick+delta,0,clipTicks-len);
        note.lengthTicks=len;
        note.velocity=(std::uint8_t)std::clamp((int)note.velocity+dv,1,127);
    }
}
template<class Note>
struct History
{
    std::vector<std::vector<Note>> undo;
    std::vector<std::vector<Note>> redo;
    static constexpr std::size_t maxEntries=48;
    void checkpoint(const std::vector<Note>& notes)
    {
        if(undo.size()>=maxEntries)undo.erase(undo.begin());
        undo.push_back(notes);
        redo.clear();
    }
    bool undoStep(std::vector<Note>& notes)
    {
        if(undo.empty())return false;
        redo.push_back(notes);
        notes=std::move(undo.back());
        undo.pop_back();
        return true;
    }
    bool redoStep(std::vector<Note>& notes)
    {
        if(redo.empty())return false;
        if(undo.size()>=maxEntries)undo.erase(undo.begin());
        undo.push_back(notes);
        notes=std::move(redo.back());
        redo.pop_back();
        return true;
    }
    void clear(){undo.clear();redo.clear();}
};
}
