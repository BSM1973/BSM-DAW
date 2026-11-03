#pragma once
#include <cstddef>
#include <utility>
#include <vector>

// Undo/Redo belongs to the CANAL editing dialog only. A snapshot can include
// both A/B drafts, the active bank, and display-only preset references.
// This class neither knows about nor touches the real-time audio engine.
namespace LibertyChannelABHistory
{
template<class Snapshot,std::size_t MaxEntries=64>
class History final
{
public:
    bool canUndo() const noexcept { return !undone.empty(); }
    bool canRedo() const noexcept { return !redone.empty(); }
    std::size_t undoDepth() const noexcept { return undone.size(); }

    void record(const Snapshot& before)
    {
        pushBounded(undone,before);
        redone.clear();
    }

    bool undo(Snapshot& current)
    {
        if(undone.empty())return false;
        pushBounded(redone,current);
        current=std::move(undone.back());
        undone.pop_back();
        return true;
    }

    bool redo(Snapshot& current)
    {
        if(redone.empty())return false;
        pushBounded(undone,current);
        current=std::move(redone.back());
        redone.pop_back();
        return true;
    }

    void clear() noexcept
    {
        undone.clear();
        redone.clear();
    }

private:
    static void pushBounded(std::vector<Snapshot>& stack,const Snapshot& snapshot)
    {
        if constexpr(MaxEntries==0)return;
        if(stack.size()>=MaxEntries)stack.erase(stack.begin());
        stack.push_back(snapshot);
    }

    std::vector<Snapshot> undone;
    std::vector<Snapshot> redone;
};
}
