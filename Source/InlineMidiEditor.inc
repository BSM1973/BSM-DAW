// Liberty — optional direct MIDI editing inside Arrangement clips.
// This is a view/controller over the existing notes, not a second MIDI engine.
bool inlineMidiMode=false,inlineMidiDragging=false,inlineMidiInstrument=false;
bool inlineMidiResizing=false;
int inlineMidiClipId=0,inlineMidiNoteIndex=-1,inlineMidiStartX=0,inlineMidiStartY=0;
int inlineMidiLow=48,inlineMidiCount=24;
std::vector<MidiEngine::NoteEvent> inlineMidiOriginalNotes;

std::pair<int,int> inlineMidiRange(const Clip& c)const {
    if(c.notes.empty())return {48,24};
    int lo=127,hi=0;
    for(const auto& n:c.notes){lo=juce::jmin(lo,(int)n.pitch);hi=juce::jmax(hi,(int)n.pitch);}
    const int count=juce::jlimit(12,36,hi-lo+7);
    return {juce::jlimit(0,128-count,(lo+hi)/2-count/2),count};
}
juce::Rectangle<int> inlineMidiArea(const Clip& c,bool instrument)const {
    int x=0,width=0;
    if(!clipPixelBounds(c,x,width))return {};
    const int logical=owner.getAudioTrackCount()
        +(instrument?owner.getMidiTrackCount():0)+c.trackLane;
    const int height=owner.getArrangeTrackHeight(logical)-8;
    if(width<55||height<63)return {};
    return {x+5,owner.getArrangeTrackTop(logical)+29,width-10,height-30};
}
juce::Rectangle<int> inlineMidiNoteBounds(const Clip& c,bool instrument,
    const MidiEngine::NoteEvent& note)const {
    const auto area=inlineMidiArea(c,instrument);
    if(area.isEmpty()||note.startTick<0||note.lengthTicks<=0)return {};
    auto range=inlineMidiRange(c);
    if(inlineMidiDragging&&inlineMidiClipId==c.id&&inlineMidiInstrument==instrument)
        range={inlineMidiLow,inlineMidiCount};
    if((int)note.pitch<range.first||(int)note.pitch>=range.first+range.second)return {};
    const double px=getLibertyTimelinePixelsPerSecond();
    if(!std::isfinite(px)||px<=0)return {};
    const double left=owner.trackHeaderWidth+
        (c.startSeconds+MidiEngine::tickToSeconds(note.startTick,owner.tempoBpm))*px;
    const double width=MidiEngine::tickToSeconds(note.lengthTicks,owner.tempoBpm)*px;
    if(!std::isfinite(left)||!std::isfinite(width)||width<=0
       ||left<std::numeric_limits<int>::min()||left>std::numeric_limits<int>::max()
       ||width>std::numeric_limits<int>::max())return {};
    const float step=(float)area.getHeight()/range.second;
    const int row=range.first+range.second-1-(int)note.pitch;
    const int top=area.getY()+(int)std::floor(row*step);
    return {(int)std::lround(left),top,juce::jmax(3,(int)std::lround(width)),
            juce::jmax(2,(int)std::ceil(step)-1)};
}
// Keep the grip within the visible note rectangle and inside the clip.
bool inlineMidiResizeGripAt(const Clip& c,bool instrument,
    const MidiEngine::NoteEvent& note,juce::Point<int> p)const {
    const auto visible=inlineMidiNoteBounds(c,instrument,note)
        .getIntersection(inlineMidiArea(c,instrument));
    if(visible.isEmpty()||!visible.contains(p))return false;
    // Short notes retain a small body for moving; do not steal the whole note.
    const int grip=juce::jlimit(1,7,visible.getWidth()/3);
    return p.x>=visible.getRight()-grip;
}
void drawInlineMidiNotes(juce::Graphics& g,bool instrument,const Clip& c)const {
    if(!inlineMidiMode)return;
    const auto area=inlineMidiArea(c,instrument);
    if(area.isEmpty())return;
    juce::Graphics::ScopedSaveState scope(g);
    g.reduceClipRegion(area);
    g.setColour(juce::Colour(0xff10263c).withAlpha(.76f));
    g.fillRoundedRectangle(area.toFloat(),3);
    auto range=inlineMidiRange(c);
    if(inlineMidiDragging&&inlineMidiClipId==c.id&&inlineMidiInstrument==instrument)
        range={inlineMidiLow,inlineMidiCount};
    for(int pitch=range.first;pitch<range.first+range.second;++pitch)
        if(pitch%12==0){
            const float y=area.getY()+
                (range.first+range.second-1-pitch)*(float)area.getHeight()/range.second;
            g.setColour(juce::Colour(0xff9dd4ee).withAlpha(.25f));
            g.drawHorizontalLine((int)y,(float)area.getX(),(float)area.getRight());
        }
    for(const auto& note:c.notes){
        const auto bounds=inlineMidiNoteBounds(c,instrument,note).getIntersection(area);
        if(bounds.isEmpty())continue;
        g.setColour(juce::Colour(0xffd4efff).withAlpha(
            c.muted||note.muted?.32f:juce::jlimit(.60f,1.0f,.60f+note.velocity/320.0f)));
        g.fillRoundedRectangle(bounds.toFloat(),2);
        // Narrow right-edge handle: a separate drag gesture adjusts duration.
        const int grip=juce::jlimit(1,7,bounds.getWidth()/3);
        if(bounds.getWidth()>=6){
            g.setColour(inlineMidiResizing&&inlineMidiClipId==c.id
                ?juce::Colour(0xfff0c66d):juce::Colour(0xff477d9c));
            g.fillRoundedRectangle((float)(bounds.getRight()-grip),
                (float)bounds.getY(),(float)grip,(float)bounds.getHeight(),1.0f);
        }
    }
}
bool inlineMidiAt(juce::Point<int> p,bool& instrument,int& clip,int& note)const {
    clip=-1;note=-1;
    if(!inlineMidiMode||activeArrangeTool!=0)return false;
    clip=findClipAt(p,instrument);
    if(clip<0)return false;
    const auto& c=(instrument?instrumentClips:midiClips)[(size_t)clip];
    if(!inlineMidiArea(c,instrument).contains(p))return false;
    for(int i=(int)c.notes.size()-1;i>=0;--i)
        if(inlineMidiNoteBounds(c,instrument,c.notes[(size_t)i]).contains(p)){
            note=i;break;
        }
    return true;
}
bool inlineMidiDown(juce::Point<int> p,bool option){
    bool instrument=false;int index=-1,note=-1;
    if(!inlineMidiAt(p,instrument,index,note)||(note<0&&!option))return false;
    selectClip(instrument,index);
    auto& c=(instrument?instrumentClips:midiClips)[(size_t)index];
    selectedAudio.clear();selectedClipIds.clear();selectedClipIds.insert(c.id);
    if(option&&note>=0){
        c.notes.erase(c.notes.begin()+note);
        if(owner.midiEngine.replaceNotes(c.notes)){syncPlayback();repaint();owner.repaint();}
        operationMessage=juce::String::fromUTF8("Note MIDI supprimée");
        return true;
    }
    if(option&&note<0){
        const auto area=inlineMidiArea(c,instrument);
        const auto range=inlineMidiRange(c);
        const int row=juce::jlimit(0,range.second-1,
            (p.y-area.getY())*range.second/area.getHeight());
        const int pitch=range.first+range.second-1-row;
        const double px=getLibertyTimelinePixelsPerSecond();
        if(px<=0)return true;
        const double time=juce::jmax(0.0,(p.x-owner.trackHeaderWidth)/px-c.startSeconds);
        const auto end=MidiEngine::secondsToTick(c.lengthSeconds,owner.tempoBpm);
        const double snap=getLibertySnapSeconds(owner.tempoBpm);
        const auto grid=snap>0?juce::jmax<std::int64_t>(1,MidiEngine::secondsToTick(snap,owner.tempoBpm)):0;
        auto tick=MidiEngine::secondsToTick(time,owner.tempoBpm);
        if(grid>0)tick=MidiEngine::quantizeTick(tick,grid);
        if(tick>=end||c.notes.size()>=16384)return true;
        MidiEngine::NoteEvent added;added.startTick=tick;
        added.lengthTicks=juce::jmin(end-tick,
            grid>0?grid:MidiEngine::ticksPerQuarterNote/4);
        added.pitch=(std::uint8_t)pitch;
        c.notes.push_back(added);
        if(owner.midiEngine.replaceNotes(c.notes)){syncPlayback();repaint();owner.repaint();}
        operationMessage=juce::String::fromUTF8("Note MIDI ajoutée");
        return true;
    }
    inlineMidiDragging=true;inlineMidiInstrument=instrument;inlineMidiClipId=c.id;
    inlineMidiNoteIndex=note;inlineMidiStartX=p.x;inlineMidiStartY=p.y;
    inlineMidiResizing=inlineMidiResizeGripAt(c,instrument,c.notes[(size_t)note],p);
    operationMessage=inlineMidiResizing
        ?juce::String::fromUTF8("ÉDITION DIRECTE : glisser le bord droit pour changer la durée")
        :juce::String::fromUTF8("ÉDITION DIRECTE : déplacer ou transposer la note");
    inlineMidiOriginalNotes=c.notes;
    const auto range=inlineMidiRange(c);
    inlineMidiLow=range.first;inlineMidiCount=range.second;
    return true;
}
void inlineMidiDrag(juce::Point<int> p){
    if(!inlineMidiDragging||inlineMidiNoteIndex<0
       ||inlineMidiNoteIndex>=(int)inlineMidiOriginalNotes.size())return;
    auto& bank=inlineMidiInstrument?instrumentClips:midiClips;
    const auto it=std::find_if(bank.begin(),bank.end(),
        [&](const Clip& c){return c.id==inlineMidiClipId;});
    if(it==bank.end())return;
    auto& c=*it;
    const auto area=inlineMidiArea(c,inlineMidiInstrument);
    const double px=getLibertyTimelinePixelsPerSecond();
    if(area.isEmpty()||px<=0)return;
    const auto original=inlineMidiOriginalNotes[(size_t)inlineMidiNoteIndex];
    const auto end=MidiEngine::secondsToTick(c.lengthSeconds,owner.tempoBpm);
    if(original.startTick<0||original.startTick>=end
       ||original.lengthTicks<=0||original.lengthTicks>end-original.startTick)return;
    if(inlineMidiResizing){
        // Quantize the note END, not the clip or the note's start.
        // Resizing is relative to the original mouse-down to avoid drift.
        const double endpointSeconds=MidiEngine::tickToSeconds(
            original.startTick+original.lengthTicks,owner.tempoBpm)
            +(p.x-inlineMidiStartX)/px;
        auto newEnd=MidiEngine::secondsToTick(juce::jmax(0.0,endpointSeconds),owner.tempoBpm);
        const double snap=getLibertySnapSeconds(owner.tempoBpm);
        const auto grid=snap>0?juce::jmax<std::int64_t>(1,
            MidiEngine::secondsToTick(snap,owner.tempoBpm)):std::int64_t{1};
        if(snap>0)newEnd=MidiEngine::quantizeTick(newEnd,grid);
        // Always leave a positive duration and never cross the MIDI clip end.
        const auto minLength=juce::jmin(grid,end-original.startTick);
        newEnd=juce::jlimit(original.startTick+minLength,end,newEnd);
        auto notes=inlineMidiOriginalNotes;
        notes[(size_t)inlineMidiNoteIndex].lengthTicks=newEnd-original.startTick;
        if(owner.midiEngine.replaceNotes(notes)){
            c.notes=owner.midiEngine.getNotesCopy();
            operationMessage=juce::String::fromUTF8("Durée MIDI : ")
                +juce::String((double)(newEnd-original.startTick)
                    /MidiEngine::ticksPerQuarterNote,2)
                +juce::String::fromUTF8(" temps");
            syncPlayback();repaint();owner.repaint();
        }
        return;
    }
    const double desired=juce::jmax(0.0,
        MidiEngine::tickToSeconds(original.startTick,owner.tempoBpm)
        +(p.x-inlineMidiStartX)/px);
    auto tick=MidiEngine::secondsToTick(desired,owner.tempoBpm);
    const double snap=getLibertySnapSeconds(owner.tempoBpm);
    if(snap>0){
        const auto grid=juce::jmax<std::int64_t>(1,MidiEngine::secondsToTick(snap,owner.tempoBpm));
        tick=MidiEngine::quantizeTick(tick,grid);
    }
    tick=juce::jlimit<std::int64_t>(0,end-original.lengthTicks,tick);
    const float rowHeight=(float)area.getHeight()/inlineMidiCount;
    const int pitch=juce::jlimit(inlineMidiLow,inlineMidiLow+inlineMidiCount-1,
        (int)original.pitch-(int)std::lround((p.y-inlineMidiStartY)/rowHeight));
    auto notes=inlineMidiOriginalNotes;
    notes[(size_t)inlineMidiNoteIndex].startTick=tick;
    notes[(size_t)inlineMidiNoteIndex].pitch=(std::uint8_t)pitch;
    if(owner.midiEngine.replaceNotes(notes)){
        c.notes=owner.midiEngine.getNotesCopy();
        syncPlayback();repaint();owner.repaint();
    }
}
void toggleInlineMidiMode(){
    inlineMidiMode=!inlineMidiMode;
    inlineMidiDragging=false;inlineMidiResizing=false;
    inlineMidiOriginalNotes.clear();inlineMidiNoteIndex=-1;
    operationMessage=juce::String::fromUTF8(inlineMidiMode
        ?"ÉDITION DIRECTE : déplacer ou étirer les notes, Option-clic pour ajouter ou supprimer"
        :"ÉDITION DIRECTE désactivée — double-clic pour le Piano Roll du dock");
    repaint();owner.repaint();
}
bool inlineMidiContextMenu(juce::Point<int> p){
    bool ins=false;
    if(findClipAt(p,ins)<0)return false;
    juce::PopupMenu menu;
    menu.addSectionHeader(juce::String::fromUTF8("ÉDITION MIDI"));
    menu.addItem(1,juce::String::fromUTF8("ÉDITION DIRECTE DES NOTES  [E]"),true,inlineMidiMode);
    juce::Component::SafePointer<MultiMidiClipController> safe(this);
    const auto screen=localPointToGlobal(p);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({screen.x,screen.y,1,1}),
        [safe](int choice){if(safe&&choice==1)safe->toggleInlineMidiMode();});
    return true;
}
