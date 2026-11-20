#include "MidiClipEditing.h"
#include <cassert>
#include <iostream>
struct Note { std::int64_t startTick, lengthTicks; int pitch; bool muted; int probability; };
int main() {
    std::vector<Note> notes{{0,480,60,false,100},{480,480,61,false,100},
        {960,240,62,false,100},{1200,480,63,false,100},{720,480,64,true,37}};
    auto scaled=notes;
    assert(LibertyMidiClipEditing::stretchNotes(scaled,2));
    for(size_t i=0;i<notes.size();++i){assert(scaled[i].startTick==notes[i].startTick*2);assert(scaled[i].lengthTicks==notes[i].lengthTicks*2);assert(scaled[i].pitch==notes[i].pitch);assert(scaled[i].probability==notes[i].probability);}
    assert(!LibertyMidiClipEditing::stretchNotes(scaled,std::numeric_limits<double>::infinity()));
    std::vector<Note> left,right;
    LibertyMidiClipEditing::splitNotes(notes,960,left,right);
    assert(left.size()==3 && right.size()==3);
    assert(left[0].lengthTicks==480 && left[1].lengthTicks==480);
    assert(right[0].startTick==0 && right[0].pitch==62);
    assert(right[1].startTick==240 && right[1].lengthTicks==480);
    assert(left[2].startTick==720 && left[2].lengthTicks==240);
    assert(right[2].startTick==0 && right[2].lengthTicks==240);
    assert(right[2].muted && right[2].probability==37);
    for (const auto& original: notes) {
        std::int64_t duration=0;
        for (const auto& n:left) if(n.pitch==original.pitch) duration+=n.lengthTicks;
        for (const auto& n:right) if(n.pitch==original.pitch) {
            duration+=n.lengthTicks;
            assert(n.startTick+960>=original.startTick);
        }
        assert(duration==original.lengthTicks);
    }
    LibertyMidiClipEditing::splitNotes(notes,0,left,right);
    assert(left.empty() && right.size()==notes.size());
    LibertyMidiClipEditing::splitNotes(notes,2000,left,right);
    assert(left.size()==notes.size() && right.empty());
    const std::vector<double> origins{3.0,1.0,7.5};
    const double shift=LibertyMidiClipEditing::clampGroupShift(origins,-8.0);
    assert(shift==-1.0);
    assert(origins[0]+shift-(origins[1]+shift)==2.0);
    assert(origins[2]+shift-(origins[0]+shift)==4.5);
    assert(LibertyMidiClipEditing::clampGroupShift(origins,2.25)==2.25);
    assert(LibertyMidiClipEditing::clampGroupShift({},2.0)==0.0);
    struct Clip { int id,trackLane; double startSeconds,lengthSeconds; bool muted; std::vector<Note> notes; };
    Clip original{1,0,2.0,4.0,true,notes};
    auto copy=LibertyMidiClipEditing::copyClipAt(original,2,1,10.0);
    assert(copy.id==2 && copy.trackLane==1 && copy.startSeconds==10.0);
    assert(copy.lengthSeconds==original.lengthSeconds && copy.muted);
    assert(copy.notes.size()==notes.size());
    for(size_t i=0;i<notes.size();++i){
        assert(copy.notes[i].startTick==notes[i].startTick);
        assert(copy.notes[i].lengthTicks==notes[i].lengthTicks);
        assert(copy.notes[i].pitch==notes[i].pitch);
        assert(copy.notes[i].muted==notes[i].muted && copy.notes[i].probability==notes[i].probability);
    }
    copy.notes[0].pitch=12;assert(original.notes[0].pitch==60);
    std::vector<Clip> clips{original,copy,LibertyMidiClipEditing::copyClipAt(original,3,0,20.0)};
    LibertyMidiClipEditing::eraseSelectedClips(clips,{1,3});
    assert(clips.size()==1 && clips[0].id==2 && clips[0].startSeconds==10.0);
    LibertyMidiClipEditing::eraseSelectedClips(clips,{});assert(clips.size()==1);
    std::cout << "Découpe MIDI : tests réussis\n";
}
