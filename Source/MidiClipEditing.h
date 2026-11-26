#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <limits>
#include <vector>
#include <set>

namespace LibertyMidiClipEditing
{
template<class Clip>
Clip copyClipAt(const Clip& original,int id,int lane,double seconds)
{
    auto copy=original;copy.id=id;copy.trackLane=lane;copy.startSeconds=seconds;
    return copy;
}
template<class Clip>
void eraseSelectedClips(std::vector<Clip>& clips,const std::set<int>& selection)
{
    clips.erase(std::remove_if(clips.begin(),clips.end(),
        [&](const Clip& clip){return selection.count(clip.id)!=0;}),clips.end());
}
inline double clampGroupShift(const std::vector<double>& origins, double requested)
{
    if(origins.empty() || !std::isfinite(requested))return 0.0;
    return std::max(requested,-*std::min_element(origins.begin(),origins.end()));
}
// Position-only operations, never alter clip durations or source media.
// Distribution is stable in timeline order, with the outer starts as anchors.
enum class PositionOperation { Quantize, AlignStarts, AlignEnds, Distribute };
struct ClipPosition { double start,length; };
inline bool groupPositions(std::vector<ClipPosition>& positions,PositionOperation operation,double grid)
{
    if(positions.size()<2||!std::isfinite(grid)||grid<0)return false;
    if(operation==PositionOperation::Quantize&&grid<=0)return false;
    double first=std::numeric_limits<double>::max(),last=0,end=0;
    for(const auto& p:positions){
        if(!std::isfinite(p.start)||p.start<0||!std::isfinite(p.length)||p.length<=0||!std::isfinite(p.start+p.length))return false;
        first=std::min(first,p.start);last=std::max(last,p.start);end=std::max(end,p.start+p.length);
    }
    auto snap=[grid](double v){return grid>0?std::round(v/grid)*grid:v;};
    auto result=positions;
    if(operation==PositionOperation::Quantize){
        for(auto& p:result)p.start=snap(p.start);
    }else if(operation==PositionOperation::AlignStarts){
        for(auto& p:result)p.start=snap(first);
    }else if(operation==PositionOperation::AlignEnds){
        // Round upward so alignment can never produce a negative start.
        const double target=grid>0?std::ceil(end/grid)*grid:end;
        for(auto& p:result)p.start=target-p.length;
    }else if(operation==PositionOperation::Distribute){
        if(result.size()<3)return false;
        std::vector<std::size_t> order(result.size());
        for(std::size_t i=0;i<order.size();++i)order[i]=i;
        std::stable_sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b){return positions[a].start<positions[b].start;});
        const double left=snap(first),right=snap(last);
        for(std::size_t i=0;i<order.size();++i)
            result[order[i]].start=snap(left+(right-left)*(static_cast<double>(i)/static_cast<double>(order.size()-1)));
    }else return false;
    for(const auto& p:result)if(!std::isfinite(p.start)||p.start<0||!std::isfinite(p.start+p.length))return false;
    positions.swap(result);return true;
}
template<class Note>
bool stretchNotes(std::vector<Note>& notes,double ratio)
{
    if(!std::isfinite(ratio)||ratio<=0)return false;
    auto scaled=notes;
    for(auto& note:scaled){
        const long double start=(long double)note.startTick*ratio,length=(long double)note.lengthTicks*ratio;
        if(start<0||length<=0||start+length>=(long double)std::numeric_limits<std::int64_t>::max())return false;
        note.startTick=(std::int64_t)std::llround(start);note.lengthTicks=std::max<std::int64_t>(1,std::llround(length));
    }
    notes=std::move(scaled);return true;
}
// Preserve every note attribute, including mute and probability.
template<class Note>
void splitNotes(const std::vector<Note>& notes, std::int64_t cut,
                std::vector<Note>& left, std::vector<Note>& right)
{
    left.clear(); right.clear();
    for (const auto& note : notes)
    {
        if (note.startTick < 0 || note.lengthTicks <= 0) continue;
        const auto end = note.startTick > std::numeric_limits<std::int64_t>::max() - note.lengthTicks
            ? std::numeric_limits<std::int64_t>::max() : note.startTick + note.lengthTicks;
        if (note.startTick < cut)
        {
            auto part = note;
            part.lengthTicks = std::min(end, cut) - note.startTick;
            if (part.lengthTicks > 0) left.push_back(part);
        }
        if (end > cut)
        {
            auto part = note;
            part.startTick = std::max(note.startTick, cut) - cut;
            part.lengthTicks = end - std::max(note.startTick, cut);
            if (part.lengthTicks > 0) right.push_back(part);
        }
    }
}
}
