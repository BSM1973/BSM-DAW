// Liberty — optional direct MIDI editing inside Arrangement clips.
// This is a view/controller over the existing notes, not a second MIDI engine.
bool inlineMidiMode=false,inlineMidiDragging=false,inlineMidiInstrument=false;
bool inlineMidiResizing=false;
// V toggles velocity drawing/editing; the eight Arrangement tools stay as-is.
bool inlineMidiVelocityMode=false,inlineMidiVelocityDragging=false;
// Option-click keeps note deletion; dragging instead previews a duplicate.
bool inlineMidiDuplicating=false,inlineMidiDuplicateMoved=false,inlineMidiDuplicateApplied=false;
int inlineMidiClipId=0,inlineMidiNoteIndex=-1,inlineMidiStartX=0,inlineMidiStartY=0;
int inlineMidiLow=48,inlineMidiCount=24;
std::vector<MidiEngine::NoteEvent> inlineMidiOriginalNotes;
std::set<int> inlineMidiSelectedNotes,inlineMidiDragSelection,inlineMidiMarqueeBaseline;
int inlineMidiSelectedClipId=0;
bool inlineMidiSelectedInstrument=false,inlineMidiMarquee=false;
juce::Point<int> inlineMidiMarqueeStart,inlineMidiMarqueeEnd;
void inlineMidiClearSelection(){
    inlineMidiSelectedNotes.clear();inlineMidiDragSelection.clear();
    inlineMidiMarqueeBaseline.clear();inlineMidiSelectedClipId=0;
    inlineMidiMarquee=false;
}
void inlineMidiFinishGesture(){
    inlineMidiDragging=false;inlineMidiResizing=false;inlineMidiMarquee=false;
    inlineMidiVelocityDragging=false;
    inlineMidiDuplicating=false;inlineMidiDuplicateMoved=false;inlineMidiDuplicateApplied=false;
    inlineMidiNoteIndex=-1;inlineMidiOriginalNotes.clear();
    inlineMidiDragSelection.clear();inlineMidiMarqueeBaseline.clear();
}
// Finalize the Option gesture before the Arrangement Undo snapshot closes.
// Short Option-click deletes the clicked note (existing behaviour); an actual
// drag duplicates the selection, with the originals untouched.
void inlineMidiCompleteGesture(){
    if(inlineMidiDragging&&inlineMidiDuplicating&&!inlineMidiDuplicateMoved
       &&inlineMidiNoteIndex>=0
       &&inlineMidiNoteIndex<(int)inlineMidiOriginalNotes.size()){
        auto& bank=inlineMidiInstrument?instrumentClips:midiClips;
        const auto it=std::find_if(bank.begin(),bank.end(),[&](const Clip& c){
            return c.id==inlineMidiClipId;
        });
        if(it!=bank.end()){
            auto remaining=inlineMidiOriginalNotes;
            remaining.erase(remaining.begin()+inlineMidiNoteIndex);
            if(owner.midiEngine.replaceNotes(remaining)){
                it->notes=owner.midiEngine.getNotesCopy();
                syncPlayback();
                operationMessage=juce::String::fromUTF8("Note MIDI supprimée");
                inlineMidiClearSelection();
                repaint();owner.repaint();
            }
        }
    }
    inlineMidiFinishGesture();
}
bool inlineMidiSelectAll(){
    if(!inlineMidiMode||activeIndex<0)return false;
    const auto& clips=activeInstrument?instrumentClips:midiClips;
    if(activeIndex>=(int)clips.size())return false;
    const auto& clip=clips[(size_t)activeIndex];
    inlineMidiSelectedClipId=clip.id;inlineMidiSelectedInstrument=activeInstrument;
    inlineMidiSelectedNotes.clear();
    for(int i=0;i<(int)clip.notes.size();++i)inlineMidiSelectedNotes.insert(i);
    operationMessage=juce::String(inlineMidiSelectedNotes.size())
        +juce::String::fromUTF8(" notes MIDI sélectionnées");
    repaint();return true;
}
// Move selected Arrangement notes precisely from the keyboard.
// Independent from the Piano Roll and the eight existing mouse tools.
bool inlineMidiNudgeSelected(int keyCode,bool coarse){
    if(!inlineMidiMode||activeArrangeTool!=0||inlineMidiSelectedNotes.empty())
        return false;
    const bool horizontal=keyCode==juce::KeyPress::leftKey
                       ||keyCode==juce::KeyPress::rightKey;
    const bool vertical=keyCode==juce::KeyPress::upKey
                     ||keyCode==juce::KeyPress::downKey;
    if(!horizontal&&!vertical)return false;
    auto& bank=inlineMidiSelectedInstrument?instrumentClips:midiClips;
    const auto it=std::find_if(bank.begin(),bank.end(),[&](const Clip& c){
        return c.id==inlineMidiSelectedClipId;
    });
    if(it==bank.end()){inlineMidiClearSelection();return true;}
    const int clipIndex=(int)std::distance(bank.begin(),it);
    if(!isActiveClip(inlineMidiSelectedInstrument,clipIndex))
        selectClip(inlineMidiSelectedInstrument,clipIndex);
    auto& clip=bank[(size_t)clipIndex];
    auto notes=clip.notes;
    const auto clipEnd=MidiEngine::secondsToTick(clip.lengthSeconds,owner.tempoBpm);
    if(clipEnd<=0)return true;
    std::int64_t earliest=clipEnd,latest=0;
    int lowest=127,highest=0;
    for(const int index:inlineMidiSelectedNotes){
        if(index<0||index>=(int)notes.size()){
            inlineMidiClearSelection();return true;
        }
        const auto& n=notes[(size_t)index];
        if(n.startTick<0||n.lengthTicks<=0||n.startTick>=clipEnd
           ||n.lengthTicks>clipEnd-n.startTick)return true;
        earliest=juce::jmin(earliest,n.startTick);
        latest=juce::jmax(latest,n.startTick+n.lengthTicks);
        lowest=juce::jmin(lowest,(int)n.pitch);
        highest=juce::jmax(highest,(int)n.pitch);
    }
    std::int64_t deltaTicks=0;
    int deltaPitch=0;
    if(horizontal){
        const double seconds=getLibertySnapSeconds(owner.tempoBpm);
        const auto snap=seconds>0?juce::jmax<std::int64_t>(
            1,MidiEngine::secondsToTick(seconds,owner.tempoBpm))
            :MidiEngine::ticksPerQuarterNote/4;
        const auto measure=MidiEngine::ticksPerMeasure(
            owner.timeSignatureNumerator,owner.timeSignatureDenominator);
        const auto step=coarse?juce::jmax<std::int64_t>(1,measure):snap;
        deltaTicks=keyCode==juce::KeyPress::leftKey?-step:step;
        // Do not weaken quantization or change spacing at clip boundaries.
        if(deltaTicks<0 && earliest< -deltaTicks)return true;
        if(deltaTicks>0 && latest>clipEnd-deltaTicks)return true;
    }else{
        const int step=coarse?12:1;
        deltaPitch=keyCode==juce::KeyPress::downKey?-step:step;
        if(lowest+deltaPitch<0||highest+deltaPitch>127)return true;
    }
    // One snapshot per keystroke, preserving the user's separate Undo steps.
    beginArrangementEdit();
    for(const int index:inlineMidiSelectedNotes){
        auto& n=notes[(size_t)index];
        n.startTick+=deltaTicks;
        n.pitch=(std::uint8_t)((int)n.pitch+deltaPitch);
    }
    // Reproduce the engine's musical order while retaining which notes
    // were selected, even when a group crosses unselected notes.
    std::vector<std::pair<MidiEngine::NoteEvent,int>> tagged;
    tagged.reserve(notes.size());
    for(int i=0;i<(int)notes.size();++i)
        tagged.push_back({notes[(size_t)i],i});
    std::sort(tagged.begin(),tagged.end(),[](const auto& a,const auto& b){
        if(a.first.startTick!=b.first.startTick)
            return a.first.startTick<b.first.startTick;
        if(a.first.channel!=b.first.channel)
            return a.first.channel<b.first.channel;
        return a.first.pitch<b.first.pitch;
    });
    if(owner.midiEngine.replaceNotes(notes)){
        clip.notes=owner.midiEngine.getNotesCopy();
        const auto previousSelection=inlineMidiSelectedNotes;
        inlineMidiSelectedNotes.clear();
        for(int i=0;i<(int)tagged.size();++i)
            if(previousSelection.count(tagged[(size_t)i].second))
                inlineMidiSelectedNotes.insert(i);
        syncPlayback();
        operationMessage=juce::String(previousSelection.size())
            +juce::String::fromUTF8(" notes MIDI : ")
            +(horizontal
                ?(deltaTicks<0?juce::String::fromUTF8("déplacement à gauche")
                              :juce::String::fromUTF8("déplacement à droite"))
                :(deltaPitch<0?juce::String::fromUTF8("transposition vers le bas")
                              :juce::String::fromUTF8("transposition vers le haut")));
        repaint();owner.repaint();
    }
    finishArrangementEdit();
    return true;
}
// Set one absolute velocity for every selected note, from the clip's context
// menu. This is intentionally separate from the dock MIDI selection.
bool inlineMidiSetVelocitySelected(int value){
    if(!inlineMidiMode||inlineMidiSelectedNotes.empty()
       ||value<1||value>127)return false;
    auto& bank=inlineMidiSelectedInstrument?instrumentClips:midiClips;
    const auto it=std::find_if(bank.begin(),bank.end(),[&](const Clip& c){
        return c.id==inlineMidiSelectedClipId;
    });
    if(it==bank.end()){inlineMidiClearSelection();return false;}
    const int index=(int)std::distance(bank.begin(),it);
    if(!isActiveClip(inlineMidiSelectedInstrument,index))
        selectClip(inlineMidiSelectedInstrument,index);
    auto notes=it->notes;
    for(const int note:inlineMidiSelectedNotes)
        if(note<0||note>=(int)notes.size()){
            inlineMidiClearSelection();return false;
        }
    bool changed=false;
    for(const int note:inlineMidiSelectedNotes){
        auto& n=notes[(size_t)note];
        if((int)n.velocity!=value){n.velocity=(std::uint8_t)value;changed=true;}
    }
    if(!changed)return true;
    beginArrangementEdit();
    if(owner.midiEngine.replaceNotes(notes)){
        it->notes=owner.midiEngine.getNotesCopy();
        syncPlayback();
        operationMessage=juce::String(inlineMidiSelectedNotes.size())
            +juce::String::fromUTF8(" notes — vélocité : ")
            +juce::String(value);
        repaint();owner.repaint();
    }
    finishArrangementEdit();
    return true;
}
// Apply a linear velocity crescendo/diminuendo in MUSICAL TIME.
// Chord notes sharing a start tick receive the same velocity. No position,
// length, channel or articulation information is altered.
bool inlineMidiRampVelocitySelected(int startVelocity,int endVelocity,
                                   LibertyInlineVelocity::Curve shape=
                                       LibertyInlineVelocity::Curve::linear,
                                   LibertyInlineVelocity::Mode mode=
                                       LibertyInlineVelocity::Mode::absolute,
                                   int intensityPercent=100,
                                   int humanizeAmount=0,
                                   std::uint32_t variationSeed=0x61b3e729U,
                                   int accentAmount=0,
                                   LibertyInlineVelocity::AccentPattern accentPattern=
                                       LibertyInlineVelocity::AccentPattern::strongBeats,
                                   int ghostAmount=0,
                                   int tolerancePercent=0,
                                   int swingPercent=50,
                                   bool progressiveAccents=false,
                                   LibertyInlineVelocity::Curve falloffCurve=
                                       LibertyInlineVelocity::Curve::linear){
    if(!inlineMidiMode||activeArrangeTool!=0
       ||inlineMidiSelectedNotes.size()<2
       ||startVelocity<1||startVelocity>127
       ||endVelocity<1||endVelocity>127)return false;
    auto& bank=inlineMidiSelectedInstrument?instrumentClips:midiClips;
    const auto it=std::find_if(bank.begin(),bank.end(),[&](const Clip& c){
        return c.id==inlineMidiSelectedClipId;
    });
    if(it==bank.end()){inlineMidiClearSelection();return false;}
    const int clipIndex=(int)std::distance(bank.begin(),it);
    if(!isActiveClip(inlineMidiSelectedInstrument,clipIndex))
        selectClip(inlineMidiSelectedInstrument,clipIndex);
    auto notes=it->notes;
    std::int64_t earliest=std::numeric_limits<std::int64_t>::max();
    std::int64_t latest=std::numeric_limits<std::int64_t>::min();
    for(const int index:inlineMidiSelectedNotes){
        if(index<0||index>=(int)notes.size()){
            inlineMidiClearSelection();return false;
        }
        const auto& note=notes[(size_t)index];
        earliest=juce::jmin(earliest,note.startTick);
        latest=juce::jmax(latest,note.startTick);
    }
    if(earliest==latest){
        operationMessage=juce::String::fromUTF8(
            "Progression impossible : choisissez des notes sur plusieurs temps");
        repaint();
        return false;
    }
    const auto selectedCount=inlineMidiSelectedNotes.size();
    std::vector<LibertyInlineVelocity::PreviewNote> selectedNotes;
    selectedNotes.reserve(selectedCount);
    for(const int index:inlineMidiSelectedNotes){
        const auto& note=notes[(size_t)index];
        selectedNotes.push_back({note.startTick,(int)note.velocity});
    }
    const auto curved=LibertyInlineVelocity::transformVelocities(
        selectedNotes,startVelocity,endVelocity,shape,mode,intensityPercent);
    const auto humanized=LibertyInlineVelocity::humanizeVelocities(
        selectedNotes,curved,humanizeAmount,variationSeed);
    const auto results=LibertyInlineVelocity::accentVelocities(
        selectedNotes,humanized,accentAmount,
        owner.timeSignatureNumerator,owner.timeSignatureDenominator,
        MidiEngine::ticksPerQuarterNote,
        MidiEngine::secondsToTick(it->startSeconds,owner.tempoBpm),
        accentPattern,ghostAmount,tolerancePercent,swingPercent,
        progressiveAccents,falloffCurve);
    bool changed=false;
    size_t resultIndex=0;
    for(const int index:inlineMidiSelectedNotes){
        auto& note=notes[(size_t)index];
        const auto value=results[resultIndex++];
        if((int)note.velocity!=value){
            note.velocity=(std::uint8_t)value;
            changed=true;
        }
    }
    if(!changed)return true;
    beginArrangementEdit();
    if(owner.midiEngine.replaceNotes(notes)){
        it->notes=owner.midiEngine.getNotesCopy();
        syncPlayback();
        operationMessage=juce::String(selectedCount)
            +juce::String::fromUTF8(" notes — progression de vélocité ")
            +juce::String(startVelocity)+juce::String::fromUTF8(" → ")
            +juce::String(endVelocity)
            +juce::String::fromUTF8(" — ")
            +(shape==LibertyInlineVelocity::Curve::sCurve
                ?juce::String::fromUTF8("courbe en S")
                :shape==LibertyInlineVelocity::Curve::exponential
                    ?juce::String::fromUTF8("courbe exponentielle")
                    :juce::String::fromUTF8("courbe linéaire"))
            +(mode==LibertyInlineVelocity::Mode::relative
                ?juce::String::fromUTF8(" — Nuances conservées")
                :juce::String::fromUTF8(" — Absolu"))
            +juce::String::fromUTF8(" — Intensité ")
            +juce::String(juce::jlimit(0,100,intensityPercent))+" %"
            +juce::String::fromUTF8(" — Humanisation ±")
            +juce::String(juce::jlimit(0,20,humanizeAmount))
            +juce::String::fromUTF8(" — Accents +")
            +juce::String(juce::jlimit(0,20,accentAmount))
            +juce::String::fromUTF8(" — ")
            +(accentPattern==LibertyInlineVelocity::AccentPattern::everyBeat
                ?juce::String::fromUTF8("Chaque pulsation")
                :accentPattern==LibertyInlineVelocity::AccentPattern::offbeat
                    ?juce::String::fromUTF8("Contretemps")
                    :accentPattern==LibertyInlineVelocity::AccentPattern::backbeat
                        ?juce::String::fromUTF8("Backbeat")
                        :accentPattern==LibertyInlineVelocity::AccentPattern::phrase2
                            ?juce::String::fromUTF8("Phrasé 2 mesures")
                            :accentPattern==LibertyInlineVelocity::AccentPattern::phrase4
                                ?juce::String::fromUTF8("Phrasé 4 mesures")
                                :accentPattern==LibertyInlineVelocity::AccentPattern::phrase8
                                    ?juce::String::fromUTF8("Phrasé 8 mesures")
                                    :accentPattern==LibertyInlineVelocity::AccentPattern::pickup2
                                        ?juce::String::fromUTF8("Relance 2 mesures")
                                        :accentPattern==LibertyInlineVelocity::AccentPattern::pickup4
                                            ?juce::String::fromUTF8("Relance 4 mesures")
                                            :accentPattern==LibertyInlineVelocity::AccentPattern::pickup8
                                                ?juce::String::fromUTF8("Relance 8 mesures")
                                                :accentPattern==LibertyInlineVelocity::AccentPattern::earlyPickup2
                                                    ?juce::String::fromUTF8("Relance croche 2 mesures")
                                                    :accentPattern==LibertyInlineVelocity::AccentPattern::earlyPickup4
                                                        ?juce::String::fromUTF8("Relance croche 4 mesures")
                                                        :accentPattern==LibertyInlineVelocity::AccentPattern::earlyPickup8
                                                            ?juce::String::fromUTF8("Relance croche 8 mesures")
                                                            :accentPattern==LibertyInlineVelocity::AccentPattern::fullPhrase2
                                                                ?juce::String::fromUTF8("Phrasé complet 2 mesures")
                                                                :accentPattern==LibertyInlineVelocity::AccentPattern::fullPhrase4
                                                                    ?juce::String::fromUTF8("Phrasé complet 4 mesures")
                                                                    :accentPattern==LibertyInlineVelocity::AccentPattern::fullPhrase8
                                                                        ?juce::String::fromUTF8("Phrasé complet 8 mesures")
                                                                        :accentPattern==LibertyInlineVelocity::AccentPattern::buildup2
                                                                            ?juce::String::fromUTF8("Montée rythmique 2 mesures")
                                                                            :accentPattern==LibertyInlineVelocity::AccentPattern::buildup4
                                                                                ?juce::String::fromUTF8("Montée rythmique 4 mesures")
                                                                                :accentPattern==LibertyInlineVelocity::AccentPattern::buildup8
                                                                                    ?juce::String::fromUTF8("Montée rythmique 8 mesures")
                                                                                    :accentPattern==LibertyInlineVelocity::AccentPattern::longBuildup2
                                                                                        ?juce::String::fromUTF8("Montée longue 2 mesures")
                                                                                        :accentPattern==LibertyInlineVelocity::AccentPattern::longBuildup4
                                                                                            ?juce::String::fromUTF8("Montée longue 4 mesures")
                                                                                            :accentPattern==LibertyInlineVelocity::AccentPattern::longBuildup8
                                                                                                ?juce::String::fromUTF8("Montée longue 8 mesures")
                                                                                                :accentPattern==LibertyInlineVelocity::AccentPattern::longFade2
                                                                                                    ?juce::String::fromUTF8("Descente longue 2 mesures")
                                                                                                    :accentPattern==LibertyInlineVelocity::AccentPattern::longFade4
                                                                                                        ?juce::String::fromUTF8("Descente longue 4 mesures")
                                                                                                        :accentPattern==LibertyInlineVelocity::AccentPattern::longFade8
                                                                                                            ?juce::String::fromUTF8("Descente longue 8 mesures")
                                                                                                            :accentPattern==LibertyInlineVelocity::AccentPattern::arc2
                                                                                                                ?juce::String::fromUTF8("Arc dynamique 2 mesures")
                                                                                                                :accentPattern==LibertyInlineVelocity::AccentPattern::arc4
                                                                                                                    ?juce::String::fromUTF8("Arc dynamique 4 mesures")
                                                                                                                    :accentPattern==LibertyInlineVelocity::AccentPattern::arc8
                                                                                                                        ?juce::String::fromUTF8("Arc dynamique 8 mesures")
                                                                                                                        :juce::String::fromUTF8("Temps forts"))
            +juce::String::fromUTF8(" — Notes fantômes −")
            +juce::String(juce::jlimit(0,20,ghostAmount))
            +juce::String::fromUTF8(" — Tolérance ")
            +juce::String(juce::jlimit(0,25,tolerancePercent))+" %"
            +juce::String::fromUTF8(" — Swing ")
            +juce::String(juce::jlimit(50,75,swingPercent))+" %"
            +(progressiveAccents
                ?juce::String::fromUTF8(" — Accents progressifs : ")
                    +(falloffCurve==LibertyInlineVelocity::Curve::sCurve
                        ?juce::String::fromUTF8("courbe en S")
                        :falloffCurve==LibertyInlineVelocity::Curve::exponential
                            ?juce::String::fromUTF8("exponentielle")
                            :juce::String::fromUTF8("linéaire"))
                :juce::String());
        repaint();owner.repaint();
    }
    finishArrangementEdit();
    return true;
}
void toggleInlineMidiVelocityMode(){
    if(!inlineMidiMode)return;
    // Velocity editing uses the direct-note selection tool, not the
    // clip eraser, cutter, glue or stretch operations.
    activeArrangeTool=0;
    inlineMidiFinishGesture();
    inlineMidiVelocityMode=!inlineMidiVelocityMode;
    operationMessage=juce::String::fromUTF8(inlineMidiVelocityMode
        ?"VÉLOCITÉ : glisser une note verticalement pour régler la sélection"
        :"VÉLOCITÉ désactivée : déplacement et durée MIDI rétablis");
    repaint();owner.repaint();
}
bool inlineMidiDeleteSelected(){
    if(!inlineMidiMode||inlineMidiSelectedNotes.empty())return false;
    auto& bank=inlineMidiSelectedInstrument?instrumentClips:midiClips;
    const auto it=std::find_if(bank.begin(),bank.end(),[&](const Clip& clip){
        return clip.id==inlineMidiSelectedClipId;
    });
    if(it==bank.end()){inlineMidiClearSelection();return false;}
    const int index=(int)std::distance(bank.begin(),it);
    if(!isActiveClip(inlineMidiSelectedInstrument,index))
        selectClip(inlineMidiSelectedInstrument,index);
    beginArrangementEdit();
    auto notes=it->notes;
    for(auto pos=inlineMidiSelectedNotes.rbegin();pos!=inlineMidiSelectedNotes.rend();++pos){
        if(*pos>=0&&*pos<(int)notes.size())notes.erase(notes.begin()+*pos);
    }
    if(owner.midiEngine.replaceNotes(notes)){
        it->notes=owner.midiEngine.getNotesCopy();
        syncPlayback();
        operationMessage=juce::String::fromUTF8("Notes MIDI sélectionnées supprimées");
    }
    finishArrangementEdit();
    inlineMidiClearSelection();repaint();owner.repaint();
    return true;
}
void inlineMidiMarqueeDrag(juce::Point<int> p){
    if(!inlineMidiMarquee)return;
    const auto& bank=inlineMidiSelectedInstrument?instrumentClips:midiClips;
    const auto it=std::find_if(bank.begin(),bank.end(),[&](const Clip& c){
        return c.id==inlineMidiSelectedClipId;
    });
    if(it==bank.end()){inlineMidiMarquee=false;return;}
    const auto area=inlineMidiArea(*it,inlineMidiSelectedInstrument);
    inlineMidiMarqueeEnd=p;
    // Marquee works in every drag direction, including bottom-to-top.
    const auto rectangle=juce::Rectangle<int>(
        juce::jmin(inlineMidiMarqueeStart.x,inlineMidiMarqueeEnd.x),
        juce::jmin(inlineMidiMarqueeStart.y,inlineMidiMarqueeEnd.y),
        std::abs(inlineMidiMarqueeStart.x-inlineMidiMarqueeEnd.x),
        std::abs(inlineMidiMarqueeStart.y-inlineMidiMarqueeEnd.y))
        .getIntersection(area);
    inlineMidiSelectedNotes=inlineMidiMarqueeBaseline;
    if(rectangle.getWidth()>1 && rectangle.getHeight()>1){
        for(int i=0;i<(int)it->notes.size();++i)
            if(rectangle.intersects(inlineMidiNoteBounds(*it,inlineMidiSelectedInstrument,
                                                        it->notes[(size_t)i])))
                inlineMidiSelectedNotes.insert(i);
    }
    repaint();
}


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
void drawMidiClipTimeGrid(juce::Graphics& g,juce::Rectangle<int> region,bool editing)const {
    if(region.getWidth()<18||region.getHeight()<12)return;
    const double pixels=getLibertyTimelinePixelsPerSecond();
    const double beat=60.0/juce::jmax(1.0,owner.tempoBpm);
    const double measure=secondsPerMeasure();
    if(!std::isfinite(pixels)||pixels<=0 || !std::isfinite(measure)||measure<=0)return;
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(region);
    const double start=juce::jmax(0.0,((double)region.getX()-owner.trackHeaderWidth)/pixels);
    const double end=((double)region.getRight()-owner.trackHeaderWidth)/pixels;
    const auto paintLines=[&](double interval,juce::Colour colour,float thickness,double minPixelSpacing){
        if(!std::isfinite(interval)||interval<=0 || interval*pixels<minPixelSpacing)return;
        const double first=std::ceil(start/interval-1.0e-9);
        if(!std::isfinite(first)||first<0 || first>1.0e10)return;
        g.setColour(colour);
        for(int line=0;line<600;++line){
            const double at=(first+line)*interval;
            if(at>end+interval*1.0e-8)break;
            const double x=owner.trackHeaderWidth+at*pixels;
            if(!std::isfinite(x) || x>region.getRight()+1)break;
            if(x<region.getX()-1)continue;
            g.drawLine((float)x,(float)region.getY(),(float)x,(float)region.getBottom(),thickness);
        }
    };
    const double snap=getLibertySnapSeconds(owner.tempoBpm);
    // Fine SNAP marks are omitted when zoomed out; beats and bars remain visible.
    if(snap>0 && snap<beat)paintLines(snap,juce::Colour(0xffd5ecff).withAlpha(editing?.29f:.22f),1.0f,11.0);
    paintLines(beat,juce::Colour(0xffc2e5ff).withAlpha(editing?.49f:.39f),1.1f,10.0);
    paintLines(measure,juce::Colour(0xffffe0a0).withAlpha(editing?.83f:.70f),1.8f,6.0);
}
// Show note thumbnails in normal mode without activating MIDI editing.
// The clip naming bar has priority; thumbnails occupy only the lower body.
juce::Rectangle<int> inlineMidiPreviewArea(const Clip& c,bool instrument)const {
    int x=0,width=0;
    if(!clipPixelBounds(c,x,width))return {};
    const int lane=owner.getAudioTrackCount()
        +(instrument?owner.getMidiTrackCount():0)+c.trackLane;
    const int height=owner.getArrangeTrackHeight(lane)-8;
    if(width<28||height<27)return {};
    const int title=juce::jmin(21,height-6);
    return {x+4,owner.getArrangeTrackTop(lane)+4+title+1,width-8,height-title-4};
}
juce::Rectangle<int> inlineMidiPreviewNoteBounds(const Clip& c,bool instrument,
    const MidiEngine::NoteEvent& note,const std::pair<int,int>& range)const {
    const auto area=inlineMidiPreviewArea(c,instrument);
    if(area.isEmpty()||note.startTick<0||note.lengthTicks<=0)return {};
    if((int)note.pitch<range.first||(int)note.pitch>=range.first+range.second)return {};
    const double px=getLibertyTimelinePixelsPerSecond();
    if(!std::isfinite(px)||px<=0)return {};
    const double x=owner.trackHeaderWidth+
        (c.startSeconds+MidiEngine::tickToSeconds(note.startTick,owner.tempoBpm))*px;
    const double w=MidiEngine::tickToSeconds(note.lengthTicks,owner.tempoBpm)*px;
    if(!std::isfinite(x)||!std::isfinite(w)||w<=0
       ||x<std::numeric_limits<int>::min()||x>std::numeric_limits<int>::max()
       ||w>std::numeric_limits<int>::max())return {};
    const float pitchHeight=(float)area.getHeight()/range.second;
    const int row=range.first+range.second-1-(int)note.pitch;
    const int top=area.getY()+(int)std::floor(row*pitchHeight);
    const int bottom=area.getY()+(int)std::ceil((row+1)*pitchHeight);
    return {(int)std::lround(x),top,juce::jmax(2,(int)std::lround(w)),
            juce::jmax(1,bottom-top)};
}
juce::Rectangle<int> inlineMidiPreviewNoteBounds(const Clip& c,bool instrument,
    const MidiEngine::NoteEvent& note)const {
    return inlineMidiPreviewNoteBounds(c,instrument,note,inlineMidiRange(c));
}
void drawInlineMidiPreview(juce::Graphics& g,bool instrument,const Clip& c)const {
    if(c.notes.empty())return;
    const auto area=inlineMidiPreviewArea(c,instrument);
    if(area.isEmpty())return;
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(area);
    g.setColour(juce::Colour(0xff091b30).withAlpha(.31f));
    g.fillRoundedRectangle(area.toFloat(),2.0f);
    // The dark backing must not conceal measure, beat or SNAP lines.
    drawMidiClipTimeGrid(g,area,false);
    const auto range=inlineMidiRange(c);
    for(int pitch=range.first;pitch<range.first+range.second;++pitch){
        if(pitch%12!=0)continue;
        const int row=range.first+range.second-1-pitch;
        const float y=area.getY()+row*(float)area.getHeight()/range.second;
        g.setColour(juce::Colour(0xfff4e3b4).withAlpha(.39f));
        g.drawHorizontalLine((int)y,(float)area.getX(),(float)area.getRight());
    }
    for(const auto& note:c.notes){
        // Use one pitch range per clip (linear complexity even for dense MIDI).
        const auto bounds=inlineMidiPreviewNoteBounds(c,instrument,note,range)
            .getIntersection(area);
        if(bounds.isEmpty())continue;
        const float alpha=c.muted||note.muted?.27f:
            juce::jlimit(.60f,1.0f,.58f+note.velocity/250.0f);
        g.setColour(juce::Colour(0xffe0f5ff).withAlpha(alpha));
        g.fillRoundedRectangle(bounds.toFloat(),1.2f);
    }
}
void drawInlineMidiNotes(juce::Graphics& g,bool instrument,const Clip& c)const {
    if(!inlineMidiMode){drawInlineMidiPreview(g,instrument,c);return;}
    const auto area=inlineMidiArea(c,instrument);
    if(area.isEmpty())return;
    juce::Graphics::ScopedSaveState scope(g);
    g.reduceClipRegion(area);
    g.setColour(juce::Colour(0xff10263c).withAlpha(.82f));
    g.fillRoundedRectangle(area.toFloat(),3);
    // Grid must be ABOVE the opaque clip/piano background but BELOW notes.
    drawMidiClipTimeGrid(g,area,true);
    auto range=inlineMidiRange(c);
    if(inlineMidiDragging&&inlineMidiClipId==c.id&&inlineMidiInstrument==instrument)
        range={inlineMidiLow,inlineMidiCount};
    // Semitone rows stay legible; C octave lines are brighter for orientation.
    for(int pitch=range.first;pitch<range.first+range.second;++pitch){
        const float y=area.getY()+
            (range.first+range.second-1-pitch)*(float)area.getHeight()/range.second;
        const bool isOctave=pitch%12==0;
        g.setColour(juce::Colour(isOctave?0xfff3dfa8:0xffbed8ed)
            .withAlpha(isOctave?.61f:.27f));
        g.drawHorizontalLine((int)y,(float)area.getX(),(float)area.getRight());
    }
    for(int index=0;index<(int)c.notes.size();++index){
        const auto& note=c.notes[(size_t)index];
        const auto bounds=inlineMidiNoteBounds(c,instrument,note).getIntersection(area);
        if(bounds.isEmpty())continue;
        const bool selected=inlineMidiSelectedClipId==c.id
            && inlineMidiSelectedInstrument==instrument
            && inlineMidiSelectedNotes.count(index)!=0;
        g.setColour(selected?juce::Colour(0xfff7df90):
            juce::Colour(0xffd4efff).withAlpha(
            c.muted||note.muted?.32f:juce::jlimit(.60f,1.0f,.60f+note.velocity/320.0f)));
        g.fillRoundedRectangle(bounds.toFloat(),2);
        if(selected){
            g.setColour(juce::Colour(0xff091b30));
            g.drawRoundedRectangle(bounds.toFloat().reduced(.5f),2,1.2f);
        }
        // In V mode, colour and a short bar indicate velocity (1..127).
        // The bar is painted inside each note so the grid remains legible.
        if(inlineMidiVelocityMode){
            const float level=juce::jlimit(1.0f,127.0f,(float)note.velocity)/127.0f;
            const float barWidth=(float)bounds.getWidth()*level;
            const float barHeight=juce::jmin(4.0f,juce::jmax(1.0f,(float)bounds.getHeight()/3.0f));
            g.setColour(juce::Colour(0xff12455b));
            g.fillRect((float)bounds.getX(),(float)bounds.getBottom()-barHeight,
                       (float)bounds.getWidth(),barHeight);
            g.setColour(selected?juce::Colour(0xffffd16f):juce::Colour(0xff61d3f1));
            g.fillRect((float)bounds.getX(),(float)bounds.getBottom()-barHeight,
                       barWidth,barHeight);
            if(selected && bounds.getWidth()>=27 && bounds.getHeight()>=11){
                g.setFont(juce::Font(9.0f,juce::Font::bold));
                g.setColour(juce::Colour(0xff112234));
                g.drawFittedText(juce::String((int)note.velocity),
                    bounds.reduced(3,1).withTrimmedBottom((int)barHeight),
                    juce::Justification::centred,1);
            }
        }
        // Narrow right-edge handle: a separate drag gesture adjusts duration.
        const int grip=juce::jlimit(1,7,bounds.getWidth()/3);
        if(bounds.getWidth()>=6&&!inlineMidiVelocityMode){
            g.setColour(inlineMidiResizing&&inlineMidiClipId==c.id
                ?juce::Colour(0xfff0c66d):juce::Colour(0xff477d9c));
            g.fillRoundedRectangle((float)(bounds.getRight()-grip),
                (float)bounds.getY(),(float)grip,(float)bounds.getHeight(),1.0f);
        }
    }
    if(inlineMidiVelocityMode && area.getWidth()>=138 && area.getHeight()>=27){
        const juce::Rectangle<int> hint(area.getX()+4,area.getBottom()-14,124,12);
        g.setColour(juce::Colour(0xff091b30).withAlpha(.89f));
        g.fillRoundedRectangle(hint.toFloat(),3.0f);
        g.setFont(juce::Font(9.0f,juce::Font::bold));
        g.setColour(juce::Colour(0xffffd16f));
        g.drawFittedText(juce::String::fromUTF8("VÉLOCITÉ  •  glisser ↑↓"),
                         hint,juce::Justification::centred,1);
    }
    if(inlineMidiMarquee&&inlineMidiSelectedClipId==c.id
       && inlineMidiSelectedInstrument==instrument){
        const auto marquee=juce::Rectangle<int>(
            juce::jmin(inlineMidiMarqueeStart.x,inlineMidiMarqueeEnd.x),
            juce::jmin(inlineMidiMarqueeStart.y,inlineMidiMarqueeEnd.y),
            std::abs(inlineMidiMarqueeStart.x-inlineMidiMarqueeEnd.x),
            std::abs(inlineMidiMarqueeStart.y-inlineMidiMarqueeEnd.y))
            .getIntersection(area);
        g.setColour(juce::Colour(0xff8fd9ff).withAlpha(.18f));
        g.fillRect(marquee);
        g.setColour(juce::Colour(0xffbde9ff));
        g.drawRect(marquee,1);
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
bool inlineMidiDown(juce::Point<int> p,bool option,bool shift=false){
    bool instrument=false;int index=-1,note=-1;
    if(!inlineMidiAt(p,instrument,index,note)||(note<0&&!option&&!shift))
        return false;
    auto& c=(instrument?instrumentClips:midiClips)[(size_t)index];
    if(inlineMidiSelectedClipId!=c.id
       ||inlineMidiSelectedInstrument!=instrument){
        inlineMidiClearSelection();
        inlineMidiSelectedClipId=c.id;inlineMidiSelectedInstrument=instrument;
    }
    selectClip(instrument,index);
    selectedAudio.clear();selectedClipIds.clear();selectedClipIds.insert(c.id);
    if(shift&&!option){
        if(note>=0){
            if(!inlineMidiSelectedNotes.erase(note))inlineMidiSelectedNotes.insert(note);
            operationMessage=juce::String(inlineMidiSelectedNotes.size())
                +juce::String::fromUTF8(" notes MIDI sélectionnées");
        }else{
            inlineMidiMarquee=true;inlineMidiMarqueeStart=inlineMidiMarqueeEnd=p;
            inlineMidiMarqueeBaseline=inlineMidiSelectedNotes;
            operationMessage=juce::String::fromUTF8("Sélection MIDI : Maj + glissement");
        }
        repaint();return true;
    }
    // Defer Option-click deletion until mouse-up so an Option-drag can
    // duplicate the selected notes without first destroying the originals.
    if(option&&note<0){
        inlineMidiClearSelection();
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
    inlineMidiVelocityDragging=inlineMidiVelocityMode&&!option;
    inlineMidiResizing=!option&&!inlineMidiVelocityDragging
        &&inlineMidiResizeGripAt(c,instrument,c.notes[(size_t)note],p);
    inlineMidiDuplicating=option;
    inlineMidiDuplicateMoved=false;
    inlineMidiDuplicateApplied=false;
    // Both the body and the right-edge handle preserve an existing
    // selection. Clicking an unselected note starts a new single-note edit.
    // A one-note selection follows the existing resize path unchanged.
    if(inlineMidiSelectedNotes.count(note)==0)
        inlineMidiSelectedNotes={note};
    inlineMidiDragging=true;inlineMidiInstrument=instrument;inlineMidiClipId=c.id;
    inlineMidiNoteIndex=note;inlineMidiStartX=p.x;inlineMidiStartY=p.y;
    inlineMidiDragSelection=inlineMidiSelectedNotes;
    operationMessage=inlineMidiVelocityDragging
        ?juce::String::fromUTF8("VÉLOCITÉ : glisser ↑ pour augmenter, ↓ pour diminuer")
        :inlineMidiDuplicating
        ?juce::String::fromUTF8("Option + glisser : dupliquer les notes MIDI sélectionnées")
        :inlineMidiResizing
        ?(inlineMidiDragSelection.size()>1
            ?juce::String::fromUTF8("ÉDITION DIRECTE : étirer les notes sélectionnées")
            :juce::String::fromUTF8("ÉDITION DIRECTE : glisser le bord droit pour changer la durée"))
        :inlineMidiDragSelection.size()>1
          ?juce::String::fromUTF8("ÉDITION DIRECTE : déplacer ou transposer les notes sélectionnées")
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
    if(inlineMidiVelocityDragging){
        auto selected=inlineMidiDragSelection;
        if(selected.empty())selected.insert(inlineMidiNoteIndex);
        int minVelocity=127,maxVelocity=1;
        for(const int index:selected){
            if(index<0||index>=(int)inlineMidiOriginalNotes.size())return;
            const int velocity=(int)inlineMidiOriginalNotes[(size_t)index].velocity;
            if(velocity<1||velocity>127)return;
            minVelocity=juce::jmin(minVelocity,velocity);
            maxVelocity=juce::jmax(maxVelocity,velocity);
        }
        // Every 2 pixels equals 1 velocity unit. One shared clamp prevents
        // any note from exceeding MIDI 1..127 and retains dynamics.
        const int requested=(int)std::lround(
            ((double)inlineMidiStartY-p.y)/2.0);
        const int delta=juce::jlimit(1-minVelocity,127-maxVelocity,requested);
        auto notes=inlineMidiOriginalNotes;
        for(const int index:selected)
            notes[(size_t)index].velocity=(std::uint8_t)(
                (int)notes[(size_t)index].velocity+delta);
        if(owner.midiEngine.replaceNotes(notes)){
            c.notes=owner.midiEngine.getNotesCopy();
            operationMessage=juce::String(selected.size())
                +juce::String::fromUTF8(" notes — vélocité : ")
                +(delta>=0?juce::String("+"):juce::String{})
                +juce::String(delta);
            syncPlayback();repaint();owner.repaint();
        }
        return;
    }
    if(inlineMidiDuplicating){
        // Wait for an actual drag: Option-click must still delete the note.
        if(std::abs(p.x-inlineMidiStartX)+std::abs(p.y-inlineMidiStartY)<4
           && !inlineMidiDuplicateMoved)return;
        inlineMidiDuplicateMoved=true;
        auto selected=inlineMidiDragSelection;
        if(selected.empty())selected.insert(inlineMidiNoteIndex);
        if(inlineMidiOriginalNotes.size()+selected.size()>16384){
            operationMessage=juce::String::fromUTF8("Limite de notes MIDI atteinte");
            return;
        }
        std::int64_t minStart=end,maxEnd=0;
        int minPitch=127,maxPitch=0;
        for(const int index:selected){
            if(index<0||index>=(int)inlineMidiOriginalNotes.size())return;
            const auto& n=inlineMidiOriginalNotes[(size_t)index];
            if(n.startTick<0||n.startTick>=end||n.lengthTicks<=0
               ||n.lengthTicks>end-n.startTick)return;
            minStart=juce::jmin(minStart,n.startTick);
            maxEnd=juce::jmax(maxEnd,n.startTick+n.lengthTicks);
            minPitch=juce::jmin(minPitch,(int)n.pitch);
            maxPitch=juce::jmax(maxPitch,(int)n.pitch);
        }
        const double desired=juce::jmax(0.0,
            MidiEngine::tickToSeconds(original.startTick,owner.tempoBpm)
            +(p.x-inlineMidiStartX)/px);
        auto target=MidiEngine::secondsToTick(desired,owner.tempoBpm);
        const double snap=getLibertySnapSeconds(owner.tempoBpm);
        if(snap>0){
            const auto grid=juce::jmax<std::int64_t>(1,
                MidiEngine::secondsToTick(snap,owner.tempoBpm));
            target=MidiEngine::quantizeTick(target,grid);
        }
        const auto delta=juce::jlimit(-minStart,end-maxEnd,
            target-original.startTick);
        const float rowHeight=(float)area.getHeight()/inlineMidiCount;
        const int requestedPitch=-(int)std::lround(
            (p.y-inlineMidiStartY)/rowHeight);
        const int pitchDelta=juce::jlimit(
            juce::jmax(-minPitch,inlineMidiLow-minPitch),
            juce::jmin(127-maxPitch,inlineMidiLow+inlineMidiCount-1-maxPitch),
            requestedPitch);
        if(delta==0&&pitchDelta==0){
            if(inlineMidiDuplicateApplied
               &&owner.midiEngine.replaceNotes(inlineMidiOriginalNotes)){
                c.notes=owner.midiEngine.getNotesCopy();
                inlineMidiSelectedNotes=inlineMidiDragSelection;
                inlineMidiDuplicateApplied=false;
                syncPlayback();repaint();owner.repaint();
            }
            return;
        }
        // Sorting with source labels keeps the highlight on the new notes
        // after MIDI-engine ordering changes; no source note is modified.
        std::vector<std::pair<MidiEngine::NoteEvent,int>> tagged;
        tagged.reserve(inlineMidiOriginalNotes.size()+selected.size());
        for(const auto& n:inlineMidiOriginalNotes)tagged.push_back({n,-1});
        for(const int index:selected){
            auto copy=inlineMidiOriginalNotes[(size_t)index];
            copy.startTick+=delta;
            copy.pitch=(std::uint8_t)((int)copy.pitch+pitchDelta);
            tagged.push_back({copy,index});
        }
        std::sort(tagged.begin(),tagged.end(),[](const auto& a,const auto& b){
            if(a.first.startTick!=b.first.startTick)
                return a.first.startTick<b.first.startTick;
            if(a.first.channel!=b.first.channel)
                return a.first.channel<b.first.channel;
            return a.first.pitch<b.first.pitch;
        });
        std::vector<MidiEngine::NoteEvent> notes;
        notes.reserve(tagged.size());
        for(const auto& item:tagged)notes.push_back(item.first);
        if(owner.midiEngine.replaceNotes(notes)){
            c.notes=owner.midiEngine.getNotesCopy();
            inlineMidiSelectedNotes.clear();
            for(int i=0;i<(int)tagged.size();++i)
                if(tagged[(size_t)i].second>=0)
                    inlineMidiSelectedNotes.insert(i);
            inlineMidiDuplicateApplied=true;
            operationMessage=juce::String(selected.size())
                +juce::String::fromUTF8(" note(s) MIDI dupliquée(s)");
            syncPlayback();repaint();owner.repaint();
        }
        return;
    }
    if(inlineMidiResizing){
        // All selected notes receive the same duration delta. Quantize the
        // dragged note's END once, then share the musical tick delta so
        // unequal note lengths retain their differences.
        auto selection=inlineMidiDragSelection;
        if(selection.empty())selection.insert(inlineMidiNoteIndex);
        const double endpointSeconds=MidiEngine::tickToSeconds(
            original.startTick+original.lengthTicks,owner.tempoBpm)
            +(p.x-inlineMidiStartX)/px;
        auto newEnd=MidiEngine::secondsToTick(
            juce::jmax(0.0,endpointSeconds),owner.tempoBpm);
        const double snap=getLibertySnapSeconds(owner.tempoBpm);
        const auto grid=snap>0?juce::jmax<std::int64_t>(
            1,MidiEngine::secondsToTick(snap,owner.tempoBpm)):std::int64_t{1};
        if(snap>0)newEnd=MidiEngine::quantizeTick(newEnd,grid);
        // One shared feasible range avoids clipping individual members and
        // guarantees every selected note stays positive and within the clip.
        std::int64_t minDelta=std::numeric_limits<std::int64_t>::min();
        std::int64_t maxDelta=std::numeric_limits<std::int64_t>::max();
        for(const int index:selection){
            if(index<0||index>=(int)inlineMidiOriginalNotes.size())return;
            const auto& n=inlineMidiOriginalNotes[(size_t)index];
            if(n.startTick<0||n.startTick>=end||n.lengthTicks<=0
               ||n.lengthTicks>end-n.startTick)return;
            const auto shortest=juce::jmin(grid,end-n.startTick);
            minDelta=juce::jmax(minDelta,shortest-n.lengthTicks);
            maxDelta=juce::jmin(maxDelta,end-n.startTick-n.lengthTicks);
        }
        if(minDelta>maxDelta)return;
        const auto desiredDelta=newEnd-(original.startTick+original.lengthTicks);
        const auto delta=juce::jlimit(minDelta,maxDelta,desiredDelta);
        auto notes=inlineMidiOriginalNotes;
        for(const int index:selection)
            notes[(size_t)index].lengthTicks+=delta;
        if(owner.midiEngine.replaceNotes(notes)){
            c.notes=owner.midiEngine.getNotesCopy();
            operationMessage=juce::String(selection.size())
                +juce::String::fromUTF8(" note(s) étirée(s) — variation : ")
                +juce::String((double)delta/MidiEngine::ticksPerQuarterNote,2)
                +juce::String::fromUTF8(" temps");
            syncPlayback();repaint();owner.repaint();
        }
        return;
    }
    // One musical delta for the entire group, preserving each note's
    // relative timing, duration, velocity, channel and articulation.
    const double desired=juce::jmax(0.0,
        MidiEngine::tickToSeconds(original.startTick,owner.tempoBpm)
        +(p.x-inlineMidiStartX)/px);
    auto tick=MidiEngine::secondsToTick(desired,owner.tempoBpm);
    const double snap=getLibertySnapSeconds(owner.tempoBpm);
    if(snap>0){
        const auto grid=juce::jmax<std::int64_t>(1,
            MidiEngine::secondsToTick(snap,owner.tempoBpm));
        tick=MidiEngine::quantizeTick(tick,grid);
    }
    auto selection=inlineMidiDragSelection;
    if(selection.empty())selection.insert(inlineMidiNoteIndex);
    std::int64_t minStart=end,maxEnd=0;
    int minPitch=127,maxPitch=0;
    for(const int index:selection){
        if(index<0||index>=(int)inlineMidiOriginalNotes.size())return;
        const auto& n=inlineMidiOriginalNotes[(size_t)index];
        if(n.startTick<0||n.lengthTicks<=0||n.startTick>end
           ||n.lengthTicks>end-n.startTick)return;
        minStart=juce::jmin(minStart,n.startTick);
        maxEnd=juce::jmax(maxEnd,n.startTick+n.lengthTicks);
        minPitch=juce::jmin(minPitch,(int)n.pitch);
        maxPitch=juce::jmax(maxPitch,(int)n.pitch);
    }
    const std::int64_t requestedDelta=tick-original.startTick;
    const std::int64_t delta=juce::jlimit(-minStart,end-maxEnd,requestedDelta);
    const float rowHeight=(float)area.getHeight()/inlineMidiCount;
    const int requestedPitch=-(int)std::lround((p.y-inlineMidiStartY)/rowHeight);
    const int pitchDelta=juce::jlimit(
        juce::jmax(-minPitch,inlineMidiLow-minPitch),
        juce::jmin(127-maxPitch,inlineMidiLow+inlineMidiCount-1-maxPitch),
        requestedPitch);
    auto notes=inlineMidiOriginalNotes;
    for(const int index:selection){
        notes[(size_t)index].startTick+=delta;
        notes[(size_t)index].pitch=(std::uint8_t)
            ((int)notes[(size_t)index].pitch+pitchDelta);
    }
    if(owner.midiEngine.replaceNotes(notes)){
        // The MIDI engine sorts by start, channel and pitch. Reproduce
        // that order with source tags so selected outlines follow the notes.
        std::vector<std::pair<MidiEngine::NoteEvent,int>> tagged;
        tagged.reserve(notes.size());
        for(int i=0;i<(int)notes.size();++i)tagged.push_back({notes[(size_t)i],i});
        std::sort(tagged.begin(),tagged.end(),[](const auto& a,const auto& b){
            if(a.first.startTick!=b.first.startTick)
                return a.first.startTick<b.first.startTick;
            if(a.first.channel!=b.first.channel)
                return a.first.channel<b.first.channel;
            return a.first.pitch<b.first.pitch;
        });
        inlineMidiSelectedNotes.clear();
        for(int i=0;i<(int)tagged.size();++i)
            if(selection.count(tagged[(size_t)i].second))
                inlineMidiSelectedNotes.insert(i);
        c.notes=owner.midiEngine.getNotesCopy();
        syncPlayback();repaint();owner.repaint();
    }
}
void toggleInlineMidiMode(){
    inlineMidiMode=!inlineMidiMode;
    if(inlineMidiMode)activeArrangeTool=0;
    if(!inlineMidiMode)inlineMidiVelocityMode=false;
    inlineMidiFinishGesture();inlineMidiClearSelection();
    operationMessage=juce::String::fromUTF8(inlineMidiMode
        ?"ÉDITION DIRECTE : déplacer ou étirer les notes, Option-clic pour ajouter ou supprimer"
        :"ÉDITION DIRECTE désactivée — double-clic pour le Piano Roll du dock");
    repaint();owner.repaint();
}
void showInlineMidiVelocityCurveDialog(){
    if(!inlineMidiMode||inlineMidiSelectedNotes.size()<2)return;
    auto& clips=inlineMidiSelectedInstrument?instrumentClips:midiClips;
    const auto it=std::find_if(clips.begin(),clips.end(),[&](const Clip& c){
        return c.id==inlineMidiSelectedClipId;
    });
    if(it==clips.end())return;
    const int clipId=it->id;
    const bool instrument=inlineMidiSelectedInstrument;
    const auto selected=inlineMidiSelectedNotes;
    const auto notesAtOpen=it->notes;
    const int meterNumerator=owner.timeSignatureNumerator;
    const int meterDenominator=owner.timeSignatureDenominator;
    const double tempoAtOpen=owner.tempoBpm;
    const double clipStartAtOpen=it->startSeconds;
    const auto clipStartTick=MidiEngine::secondsToTick(
        clipStartAtOpen,tempoAtOpen);
    std::vector<LibertyInlineVelocity::PreviewNote> previewNotes;
    previewNotes.reserve(selected.size());
    for(const int noteIndex:selected){
        if(noteIndex<0||noteIndex>=(int)notesAtOpen.size())return;
        const auto& note=notesAtOpen[(size_t)noteIndex];
        previewNotes.push_back({note.startTick,(int)note.velocity});
    }
    // Start from the actual phrase dynamics; for chords, average the
    // simultaneous notes so the default is independent of note ordering.
    const auto initialValues=LibertyInlineVelocity::boundaryVelocities(previewNotes);
    auto* panel=new LibertyInlineVelocity::DialogContent(
        initialValues.first,initialValues.second,
        LibertyInlineVelocity::Curve::linear,
        [safe=juce::Component::SafePointer<MultiMidiClipController>(this),
         clipId,instrument,selected,notesAtOpen,meterNumerator,
         meterDenominator,tempoAtOpen,clipStartAtOpen]
        (int first,int last,LibertyInlineVelocity::Curve curve,
         LibertyInlineVelocity::Mode mode,int intensityPercent,
         int humanizeAmount,std::uint32_t variationSeed,int accentAmount,
         LibertyInlineVelocity::AccentPattern accentPattern,int ghostAmount,
         int tolerancePercent,int swingPercent,bool progressiveAccents,
         LibertyInlineVelocity::Curve falloffCurve)
        {
            if(safe==nullptr)return;
            if(!safe->inlineMidiMode
               ||safe->inlineMidiSelectedClipId!=clipId
               ||safe->inlineMidiSelectedInstrument!=instrument
               ||safe->inlineMidiSelectedNotes!=selected)return;
            const auto& current=instrument?safe->instrumentClips:safe->midiClips;
            const auto active=std::find_if(current.begin(),current.end(),
                [clipId](const Clip& c){return c.id==clipId;});
            if(active==current.end()||active->notes.size()!=notesAtOpen.size())return;
            // The musical grid seen in the preview must still match the
            // Arrangement's meter, tempo and clip position when applying.
            if(safe->owner.timeSignatureNumerator!=meterNumerator
               ||safe->owner.timeSignatureDenominator!=meterDenominator
               ||std::abs(safe->owner.tempoBpm-tempoAtOpen)>1.0e-9
               ||std::abs(active->startSeconds-clipStartAtOpen)>1.0e-9)return;
            // Prevent applying a stale preview after any notes have changed.
            for(size_t i=0;i<notesAtOpen.size();++i){
                const auto& a=active->notes[i];
                const auto& b=notesAtOpen[i];
                if(a.startTick!=b.startTick||a.lengthTicks!=b.lengthTicks
                   ||a.pitch!=b.pitch||a.velocity!=b.velocity
                   ||a.channel!=b.channel||a.muted!=b.muted
                   ||a.probability!=b.probability)return;
            }
            safe->inlineMidiRampVelocitySelected(
                first,last,curve,mode,intensityPercent,
                humanizeAmount,variationSeed,accentAmount,accentPattern,ghostAmount,
                tolerancePercent,swingPercent,progressiveAccents,falloffCurve);
        },std::move(previewNotes),meterNumerator,meterDenominator,
          MidiEngine::ticksPerQuarterNote,clipStartTick);
    panel->bindInputs();
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle=juce::String::fromUTF8("LIBERTY  ·  COURBE DE VÉLOCITÉ MIDI");
    options.dialogBackgroundColour=juce::Colour(0xff101c2b);
    options.escapeKeyTriggersCloseButton=true;
    options.useNativeTitleBar=false;
    options.resizable=false;
    options.componentToCentreAround=&owner;
    options.content.setOwned(panel);
    options.launchAsync();
}
// All MIDI commands are dispatched through the same validated clip context,
// regardless of whether they originate from the navy-blue editor or a shortcut.
bool inlineMidiExecuteAction(int choice,int clipId,bool instrument){
    auto& bank=instrument?instrumentClips:midiClips;
    auto it=std::find_if(bank.begin(),bank.end(),
        [clipId](const Clip& c){return c.id==clipId;});
    if(it==bank.end())return false;
    const int index=(int)std::distance(bank.begin(),it);
    // A single click from the MIDI editor must be sufficient: no separate
    // mouse selection or previous activation of the Selection tool required.
    if(choice==1){
        if(!inlineMidiMode)toggleInlineMidiMode();
        else {
            toggleInlineMidiMode();
            repaint();owner.repaint();
            return true;
        }
        selectClip(instrument,index);
        return inlineMidiSelectAll();
    }
    if(choice==2 || choice==3){
        if(!inlineMidiMode)toggleInlineMidiMode();
        activeArrangeTool=0;
        selectClip(instrument,index);
        if(inlineMidiSelectedClipId!=clipId
            ||inlineMidiSelectedInstrument!=instrument
            ||inlineMidiSelectedNotes.empty())
            inlineMidiSelectAll();
        if(choice==2)toggleInlineMidiVelocityMode();
        repaint();owner.repaint();
        return true;
    }
    if(choice<1001 || (choice>1127 && choice<2001) || choice>2005)
        return false;
    if(it->notes.empty())return false;
    if(!inlineMidiMode)toggleInlineMidiMode();
    activeArrangeTool=0;
    // Synchronize the target clip only when the active engine represents a
    // different clip. Selecting it repeatedly would overwrite edited notes.
    if(!isActiveClip(instrument,index))selectClip(instrument,index);
    const bool hasCompatibleSelection=
        inlineMidiSelectedClipId==clipId
        &&inlineMidiSelectedInstrument==instrument;
    if(!hasCompatibleSelection||inlineMidiSelectedNotes.empty()
       ||(choice>=2001&&inlineMidiSelectedNotes.size()<2)){
        inlineMidiSelectedClipId=clipId;
        inlineMidiSelectedInstrument=instrument;
        inlineMidiSelectedNotes.clear();
        for(int i=0;i<(int)it->notes.size();++i)
            inlineMidiSelectedNotes.insert(i);
    }
    if(choice>=1001&&choice<=1127)
        return inlineMidiSetVelocitySelected(choice-1000);
    if(inlineMidiSelectedNotes.size()<2)return false;
    if(choice==2001)return inlineMidiRampVelocitySelected(64,100);
    if(choice==2002)return inlineMidiRampVelocitySelected(40,120);
    if(choice==2003)return inlineMidiRampVelocitySelected(100,64);
    if(choice==2004)return inlineMidiRampVelocitySelected(120,40);
    if(choice==2005){showInlineMidiVelocityCurveDialog();return true;}
    return false;
}

// Actual JUCE controls, not a decorative picture or a custom PopupMenu.
// Buttons keep the standard keyboard/mouse accessibility and existing engine.
class InlineMidiActionsPanel final : public juce::Component
{
public:
    using State=std::array<int,4>; // direct, velocity, selected, total
    InlineMidiActionsPanel(std::function<bool(int)> action,
                           std::function<State()> read)
      : dispatch(std::move(action)),readState(std::move(read))
    {
        setSize(608,584);
        static const int actions[]={
            1,2,3,1032,1064,1080,1100,1127,2001,2002,2003,2004,2005,0};
        const juce::String labels[]={
            juce::String::fromUTF8("ÉDITION DIRECTE  [E]"),
            juce::String::fromUTF8("MODE VÉLOCITÉ  [V]"),
            juce::String::fromUTF8("SÉLECTIONNER TOUTES LES NOTES"),
            juce::String::fromUTF8("32 - TRÈS DOUX"),
            juce::String::fromUTF8("64 - DOUX"),
            juce::String::fromUTF8("80 - MODÉRÉ"),
            juce::String::fromUTF8("100 - FORT"),
            juce::String::fromUTF8("127 - MAXIMUM"),
            juce::String::fromUTF8("CRESCENDO DOUX  64 à 100"),
            juce::String::fromUTF8("CRESCENDO FORT  40 à 120"),
            juce::String::fromUTF8("DIMINUENDO DOUX  100 à 64"),
            juce::String::fromUTF8("DIMINUENDO FORT  120 à 40"),
            juce::String::fromUTF8("PERSONNALISER LA COURBE"),
            juce::String::fromUTF8("FERMER")};
        for(size_t i=0;i<buttons.size();++i){
            auto& b=buttons[i];
            b.setButtonText(labels[i]);
            b.setColour(juce::TextButton::buttonColourId,
                        juce::Colour(i==12?0xff264956:0xff21374a));
            b.setColour(juce::TextButton::buttonOnColourId,
                        juce::Colour(0xff366074));
            b.setColour(juce::TextButton::textColourOffId,
                        juce::Colour(i==13?0xffb9cbd9:0xfff1f7fd));
            b.setColour(juce::TextButton::textColourOnId,
                        juce::Colour(0xffffd48a));
            b.setTooltip(i<2
                ?juce::String::fromUTF8("Activer ou désactiver le mode MIDI dans l'Arrangement")
                :i==2
                    ?juce::String::fromUTF8("Sélectionner toutes les notes du clip affiché")
                    :i<8
                        ?juce::String::fromUTF8("Appliquer la vélocité aux notes sélectionnées")
                        :i<13
                            ?juce::String::fromUTF8("Appliquer une progression aux notes sélectionnées")
                            :juce::String::fromUTF8("Fermer l'éditeur"));
            b.onClick=[this,i,choice=actions[i]]{
                if(i==13){
                    if(auto* w=findParentComponentOfClass<juce::DialogWindow>())
                        w->exitModalState(0);
                    return;
                }
                if(i==12){
                    auto callback=dispatch;
                    if(auto* w=findParentComponentOfClass<juce::DialogWindow>())
                        w->exitModalState(0);
                    // Open the existing curve editor after this window closes.
                    juce::MessageManager::callAsync([callback,choice]{
                        if(callback)callback(choice);
                    });
                    return;
                }
                const bool applied=dispatch&&dispatch(choice);
                refresh();
                if(!applied)repaint();
            };
            addAndMakeVisible(b);
        }
        refresh();
    }

    void refresh(){
        const auto state=readState?readState():State{0,0,0,0};
        buttons[0].setButtonText(juce::String::fromUTF8(state[0]
            ?"ÉDITION DIRECTE ACTIVE  [E]"
            :"ACTIVER ÉDITION DIRECTE  [E]"));
        buttons[1].setButtonText(juce::String::fromUTF8(state[1]
            ?"MODE VÉLOCITÉ ACTIF  [V]"
            :"ACTIVER MODE VÉLOCITÉ  [V]"));
        // Commands can activate direct editing and select the actual clip
        // themselves. Disable only truly impossible operations.
        buttons[1].setEnabled(state[3]>0);
        buttons[2].setEnabled(state[3]>0);
        for(int i=3;i<=7;++i)buttons[(size_t)i].setEnabled(state[3]>0);
        for(int i=8;i<=12;++i)buttons[(size_t)i].setEnabled(state[3]>=2);
        selected=state[2];total=state[3];
        repaint();
    }

    bool isActionEnabled(int index)const {
        return index>=0&&index<(int)buttons.size()
            &&buttons[(size_t)index].isEnabled();
    }
    void resized()override {
        const int margin=24,w=getWidth()-2*margin;
        buttons[0].setBounds(margin,101,(w-10)/2,43);
        buttons[1].setBounds(margin+(w-10)/2+10,101,(w-10)/2,43);
        buttons[2].setBounds(margin,176,w,39);
        const int five=(w-4*7)/5;
        for(int i=0;i<5;++i)
            buttons[(size_t)(3+i)].setBounds(margin+i*(five+7),270,five,44);
        for(int i=0;i<4;++i)
            buttons[(size_t)(8+i)].setBounds(
                margin+(i%2)*(w/2+5),366+(i/2)*49,w/2-5,42);
        buttons[12].setBounds(margin,478,w,42);
        buttons[13].setBounds(getWidth()-142,534,118,34);
    }
    void paint(juce::Graphics& g)override {
        const auto bg=getLocalBounds().toFloat();
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xff20394d),0.f,0.f,
            juce::Colour(0xff0f1b2b),0.f,(float)getHeight(),false));
        g.fillRoundedRectangle(bg.reduced(1.f),12.f);
        g.setColour(juce::Colour(0xff5b839a));
        g.drawRoundedRectangle(bg.reduced(1.f),12.f,1.5f);
        g.setColour(juce::Colour(0xffffd08a));
        g.fillRoundedRectangle(juce::Rectangle<float>(24,23,4,33),2.f);
        g.setFont(juce::Font(25.f,juce::Font::bold));
        g.setColour(juce::Colour(0xfff4f8fb));
        g.drawText(juce::String::fromUTF8("ÉDITION MIDI"),40,22,520,35,
                   juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff92b8cd));
        g.setFont(juce::Font(12.f));
        g.drawText(juce::String::fromUTF8("Édition des notes directement dans l'Arrangement"),
                   26,63,545,20,juce::Justification::centredLeft);
        g.drawText(juce::String(selected)+" / "+juce::String(total)
                   +juce::String::fromUTF8(selected>0
                       ?" notes sélectionnées"
                       :" notes dans le clip - commande sur toutes les notes"),
                   26,148,getWidth()-52,22,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff52768b));
        g.drawHorizontalLine(228,24.f,(float)getWidth()-24.f);
        g.drawHorizontalLine(325,24.f,(float)getWidth()-24.f);
        g.setColour(juce::Colour(0xffffd08a));
        g.setFont(juce::Font(13.5f,juce::Font::bold));
        g.drawText(juce::String::fromUTF8("DÉFINIR LA VÉLOCITÉ"),
                   25,237,400,25,juce::Justification::centredLeft);
        g.drawText(juce::String::fromUTF8("PROGRESSION DE VÉLOCITÉ"),
                   25,335,450,25,juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff91aebd));
        g.setFont(juce::Font(11.f));
        g.drawText(juce::String::fromUTF8("Les changements sont annulables (Undo/Redo)."),
                   25,541,390,26,juce::Justification::centredLeft);
    }
private:
    std::function<bool(int)> dispatch;
    std::function<State()> readState;
    std::array<juce::TextButton,14> buttons;
    int selected=0,total=0;
};

bool inlineMidiContextMenu(juce::Point<int> p){
    bool ins=false;
    const int hit=findClipAt(p,ins);
    if(hit<0)return false;
    const auto& clicked=(ins?instrumentClips:midiClips)[(size_t)hit];
    const int targetId=clicked.id;
    // Right-clicking a visible note selects that note when there is no
    // compatible selection. Right-clicking within an existing multi-note
    // selection preserves the full group.
    if(inlineMidiMode && activeArrangeTool==0
       &&inlineMidiArea(clicked,ins).contains(p)){
        int note=-1;
        for(int i=(int)clicked.notes.size()-1;i>=0;--i)
            if(inlineMidiNoteBounds(clicked,ins,clicked.notes[(size_t)i]).contains(p)){
                note=i;break;
            }
        if(note>=0){
            const bool same=inlineMidiSelectedClipId==targetId
                &&inlineMidiSelectedInstrument==ins;
            if(!same||inlineMidiSelectedNotes.count(note)==0){
                selectClip(ins,hit);
                inlineMidiSelectedClipId=targetId;
                inlineMidiSelectedInstrument=ins;
                inlineMidiSelectedNotes={note};
            }
        }
    }
    // The context panel is bound to this clip; when no notes are
    // currently selected, its commands transparently apply to all notes.
    juce::Component::SafePointer<MultiMidiClipController> safe(this);
    auto* panel=new InlineMidiActionsPanel(
        [safe,targetId,ins](int choice)->bool {
            return safe&&safe->inlineMidiExecuteAction(choice,targetId,ins);
        },
        [safe,targetId,ins]()->InlineMidiActionsPanel::State {
            if(!safe)return {0,0,0,0};
            const auto& bank=ins?safe->instrumentClips:safe->midiClips;
            const auto it=std::find_if(bank.begin(),bank.end(),
                [targetId](const Clip& c){return c.id==targetId;});
            if(it==bank.end())return {0,0,0,0};
            const bool selection=safe->inlineMidiMode
                &&safe->inlineMidiSelectedClipId==targetId
                &&safe->inlineMidiSelectedInstrument==ins;
            return {safe->inlineMidiMode?1:0,
                    safe->inlineMidiVelocityMode?1:0,
                    selection?(int)safe->inlineMidiSelectedNotes.size():0,
                    (int)it->notes.size()};
        });
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle=juce::String::fromUTF8("LIBERTY · ÉDITION MIDI");
    options.dialogBackgroundColour=juce::Colour(0xff101c2b);
    options.useNativeTitleBar=false;
    options.escapeKeyTriggersCloseButton=true;
    options.resizable=false;
    options.componentToCentreAround=&owner;
    options.content.setOwned(panel);
    options.launchAsync();
    return true;
}
