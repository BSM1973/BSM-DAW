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
