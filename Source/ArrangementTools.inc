// Shared arrangement transactions. Buffers are retained by shared ownership;
// undo never writes to a source file or to a published audio buffer.
using AudioClip = AudioEngine::AudioClipMetadata;
using AudioKey = std::pair<int,juce::String>;
struct ArrangementState {
    std::vector<Clip> midi,instruments;
    std::vector<std::vector<AudioClip>> audio;
    int selected=-1; bool instrument=false;
    juce::String signature;
    std::map<juce::String,juce::String> manualNames;
};
struct ArrangementEdit { ArrangementState before,after; };
std::vector<ArrangementEdit> undoEdits,redoEdits;
std::unique_ptr<ArrangementState> pendingEdit;
std::set<AudioKey> selectedAudio;
std::map<AudioKey,double> audioOrigins;
AudioClip audioClipboard,audioDragOriginal;
bool audioClipboardValid=false;
int audioDragTrack=-1,audioDragMode=0;
double audioDragX=0,audioDragLength=0;
std::vector<MidiEngine::NoteEvent> stretchOriginalNotes;

ArrangementState captureArrangement() {
    syncActiveClip();
    ArrangementState state;state.midi=midiClips;state.instruments=instrumentClips;
    state.selected=activeIndex;state.instrument=activeInstrument;
    juce::XmlElement xml("Arrangement");saveProjectClips(xml);
    xml.deleteAllChildElementsWithTagName("ClipNames");
    state.signature=xml.toString()+namingSignature();
    for(const auto& e:clipNames.entries)state.manualNames[e.first]=e.second.manual;
    for(int t=0;t<owner.getAudioTrackCount();++t){
        auto clips=owner.audioEngine.getArrangementAudioClips(t);
        state.signature << "|track=" << t;
        for(const auto& c:clips){juce::XmlElement e("Audio");AudioEngine::writeAudioClipProperties(e,c);
            state.signature << e.toString() << ":" << juce::String::toHexString((juce::int64)(intptr_t)c.sourceBuffer.get());}
        state.audio.push_back(std::move(clips));
    }
    return state;
}
void beginArrangementEdit(){if(!pendingEdit)pendingEdit=std::make_unique<ArrangementState>(captureArrangement());}
void finishArrangementEdit(){
    if(!pendingEdit)return;
    auto after=captureArrangement();
    if(pendingEdit->signature!=after.signature){
        undoEdits.push_back({std::move(*pendingEdit),std::move(after)});
        if(undoEdits.size()>16)undoEdits.erase(undoEdits.begin());
        redoEdits.clear();
        auto retainedBytes=[&](){std::set<const void*> seen;std::uint64_t bytes=0;
            for(const auto& edit:undoEdits)for(const auto* state:{&edit.before,&edit.after})for(const auto& lane:state->audio)for(const auto& clip:lane)
                if(clip.sourceBuffer&&seen.insert(clip.sourceBuffer.get()).second)bytes+=(std::uint64_t)clip.sourceBuffer->getNumSamples()*clip.sourceBuffer->getNumChannels()*sizeof(float);
            return bytes;};
        while(undoEdits.size()>1&&retainedBytes()>512ull*1024*1024)undoEdits.erase(undoEdits.begin());
    }
    pendingEdit.reset();
}
void clearArrangementHistory(){undoEdits.clear();redoEdits.clear();pendingEdit.reset();selectedAudio.clear();audioOrigins.clear();audioClipboardValid=false;audioDragTrack=-1;}
bool restoreArrangement(const ArrangementState& state){
    if(state.audio.size()!=(size_t)owner.getAudioTrackCount())return false;
    const auto previous=captureArrangement();
    for(int t=0;t<owner.getAudioTrackCount();++t){
        if(!owner.audioEngine.replaceArrangementAudioClips(t,state.audio[(size_t)t])){
            for(int back=0;back<t;++back)owner.audioEngine.replaceArrangementAudioClips(back,previous.audio[(size_t)back]);
            return false;
        }
        refreshAudio(t);
    }
    for(auto& e:clipNames.entries){const auto it=state.manualNames.find(e.first);e.second.manual=it==state.manualNames.end()?juce::String{}:it->second;}
    activeIndex=-1;midiClips=state.midi;instrumentClips=state.instruments;
    owner.midiEngine.clear();
    if(state.selected>=0)selectClip(state.instrument,state.selected);
    selectedClipIds.clear();selectedAudio.clear();syncPlayback();owner.repaint();repaint();return true;
}
bool undoArrangement(bool redo){
    finishArrangementEdit();auto& from=redo?redoEdits:undoEdits;auto& to=redo?undoEdits:redoEdits;
    if(from.empty()){operationMessage=juce::String::fromUTF8("Aucune opération d’arrangement à restaurer");repaint();return true;}
    // A dock edit or a changed track structure must never be silently overwritten.
    const auto current=captureArrangement();const auto& expected=redo?from.back().before:from.back().after;
    if(current.signature!=expected.signature){clearArrangementHistory();operationMessage=juce::String::fromUTF8("Historique réinitialisé après une modification externe");repaint();return true;}
    if(!restoreArrangement(redo?from.back().after:from.back().before)){operationMessage=juce::String::fromUTF8("Restauration impossible");repaint();return true;}
    to.push_back(std::move(from.back()));from.pop_back();operationMessage=juce::String::fromUTF8(redo?"Opération rétablie":"Opération annulée");repaint();return true;
}
int audioLaneAt(juce::Point<int> p)const {
    if(p.x<owner.trackHeaderWidth||p.y<owner.getArrangeTop()||p.y>=owner.getMixerTop())return -1;
    const int t=owner.getArrangeTrackAtY(p.y);return t>=0&&t<owner.getAudioTrackCount()?t:-1;
}
juce::Rectangle<int> audioBounds(int track,const AudioClip& c)const {
    Clip proxy;proxy.startSeconds=c.startSeconds;proxy.lengthSeconds=c.lengthSeconds;int x=0,w=0;
    if(!clipPixelBounds(proxy,x,w))return {};
    return {x,owner.getArrangeTrackTop(track)+4,w,owner.getArrangeTrackHeight(track)-8};
}
juce::String audioAt(juce::Point<int> p)const {
    const int t=audioLaneAt(p);if(t<0)return {};
    const auto clips=owner.audioEngine.getArrangementAudioClips(t);
    for(auto it=clips.rbegin();it!=clips.rend();++it)if(audioBounds(t,*it).contains(p))return it->id;
    return {};
}
// The fade grip follows the current endpoint, not the clip corner.
int audioFadeHandleAt(juce::Point<int> p) const {
    if(activeArrangeTool!=0)return 0;
    const int t=audioLaneAt(p);if(t<0)return 0;
    const auto id=audioAt(p);if(id.isEmpty())return 0;
    for(const auto& c:owner.audioEngine.getArrangementAudioClips(t)){
        if(c.id!=id || !c.sourceBuffer || c.lengthSeconds<=0.0)continue;
        const auto box=audioBounds(t,c);
        if(box.getWidth()<=36 || box.getHeight()<=32 || std::abs(p.y-(box.getY()+24))>6)return 0;
        const float fi=juce::jlimit(0.0f,(float)box.getWidth(),(float)(box.getWidth()*c.fadeInSeconds/c.lengthSeconds));
        const float fo=juce::jlimit(0.0f,(float)box.getWidth(),(float)(box.getWidth()*c.fadeOutSeconds/c.lengthSeconds));
        const float left=std::abs((float)p.x-(box.getX()+fi));
        const float right=std::abs((float)p.x-(box.getRight()-fo));
        if(left<=7.0f && left<=right)return 8;
        if(right<=7.0f)return 9;
        return 0;
    }
    return 0;
}
void refreshAudio(int track){
    if(!owner.audioEngine.hasAudioFile(track)){
        owner.trackSourceFiles[(size_t)track]=juce::File{};owner.pendingAudioFileNames[(size_t)track].clear();
        owner.pendingAudioLengths[(size_t)track]=0;owner.pendingAudioStartSeconds[(size_t)track]=0;owner.pendingAudioWarpStates[(size_t)track]={};
    }
    owner.rebuildWaveformCache(track);owner.repaint();repaint();
}
void paintAudioSelection(juce::Graphics& g){
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(owner.trackHeaderWidth,owner.getArrangeTop(),owner.getWidth()-owner.trackHeaderWidth,owner.getMixerTop()-owner.getArrangeTop());
    for(int t=0;t<owner.getAudioTrackCount();++t)for(const auto& c:owner.audioEngine.getArrangementAudioClips(t)){
        const auto box=audioBounds(t,c).toFloat();
        if(!c.sourceBuffer){g.setColour(juce::Colour(0xff233e55));g.fillRoundedRectangle(box,5);
            g.setColour(juce::Colour(0xffbdefff));g.setFont(11);g.drawText(juce::String::fromUTF8("Région audio vide"),box.toNearestInt().reduced(8),juce::Justification::centredLeft);}
        if(c.muted){g.setColour(juce::Colour(0xff101b2a).withAlpha(.70f));g.fillRoundedRectangle(box,5);
            if(box.getHeight()>44){g.setColour(juce::Colour(0xffbdefff));g.drawText("MUET",box.toNearestInt().reduced(8).withTrimmedTop(18),juce::Justification::centredLeft);}}
        if(selectedAudio.count({t,c.id})){g.setColour(juce::Colour(0xff63c7e8));g.drawRoundedRectangle(box.reduced(1),5,2);}
    }
}
// Draw after names so the editable grips stay visible; keep them below the title strip.
void paintAudioFades(juce::Graphics& g){
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(owner.trackHeaderWidth,owner.getArrangeTop(),owner.getWidth()-owner.trackHeaderWidth,owner.getMixerTop()-owner.getArrangeTop());
    for(int t=0;t<owner.getAudioTrackCount();++t)for(const auto& c:owner.audioEngine.getArrangementAudioClips(t)){
        if(!c.sourceBuffer || c.lengthSeconds<=0.0)continue;
        const auto box=audioBounds(t,c).toFloat();
        if(box.getWidth()<=36.0f || box.getHeight()<=32.0f)continue;
        const float fi=juce::jlimit(0.0f,box.getWidth(),(float)(box.getWidth()*c.fadeInSeconds/c.lengthSeconds));
        const float fo=juce::jlimit(0.0f,box.getWidth(),(float)(box.getWidth()*c.fadeOutSeconds/c.lengthSeconds));
        const float top=box.getY()+24.0f,bottom=box.getBottom()-5.0f;
        g.setColour(juce::Colour(0xfff0c66d));
        if(fi>0.0f)g.drawLine(box.getX(),bottom,box.getX()+fi,top,1.5f);
        if(fo>0.0f)g.drawLine(box.getRight()-fo,top,box.getRight(),bottom,1.5f);
        g.fillEllipse(box.getX()+fi-3.0f,top-3.0f,6.0f,6.0f);
        g.fillEllipse(box.getRight()-fo-3.0f,top-3.0f,6.0f,6.0f);
    }
}
void eraseArrangementSelection(){
    syncActiveClip();activeIndex=-1;
    LibertyMidiClipEditing::eraseSelectedClips(midiClips,selectedClipIds);
    LibertyMidiClipEditing::eraseSelectedClips(instrumentClips,selectedClipIds);
    for(int t=0;t<owner.getAudioTrackCount();++t){
        auto clips=owner.audioEngine.getArrangementAudioClips(t);const auto count=clips.size();
        clips.erase(std::remove_if(clips.begin(),clips.end(),[&](const auto& c){return selectedAudio.count({t,c.id})!=0;}),clips.end());
        if(count!=clips.size()&&owner.audioEngine.replaceArrangementAudioClips(t,clips))refreshAudio(t);
    }
    owner.midiEngine.clear();selectedClipIds.clear();selectedAudio.clear();syncPlayback();owner.repaint();repaint();
}
void startGroup(double anchor,float x){
    syncActiveClip();groupOrigins.clear();audioOrigins.clear();
    for(const auto& c:midiClips)if(selectedClipIds.count(c.id))groupOrigins[c.id]=c.startSeconds;
    for(const auto& c:instrumentClips)if(selectedClipIds.count(c.id))groupOrigins[c.id]=c.startSeconds;
    for(int t=0;t<owner.getAudioTrackCount();++t)for(const auto& c:owner.audioEngine.getArrangementAudioClips(t))
        if(selectedAudio.count({t,c.id}))audioOrigins[{t,c.id}]=c.startSeconds;
    groupMouseX=x;groupAnchorSeconds=anchor;movingGroup=true;
}
bool handleAudioDown(juce::Point<int> p){
    const int t=audioLaneAt(p);if(t<0)return false;
    owner.selectedTrack=t;
    auto clips=owner.audioEngine.getArrangementAudioClips(t);const auto id=audioAt(p);
    auto it=std::find_if(clips.begin(),clips.end(),[&](const auto& c){return c.id==id;});
    const bool hit=it!=clips.end();const double px=getLibertyTimelinePixelsPerSecond();if(px<=0)return true;
    const double seconds=snapPosition((p.x-owner.trackHeaderWidth)/px);
    if(activeArrangeTool==0||activeArrangeTool==1){
        if(hit&&selectedAudio.count({t,id})&&(activeArrangeTool==1||selectedAudio.size()+selectedClipIds.size()>1)){
            startGroup(it->startSeconds,(float)p.x);return true;
        }
        selectedClipIds.clear();selectedAudio.clear();
        if(activeArrangeTool==1){syncActiveClip();activeIndex=-1;rectangleAnchor=rectangleEnd=p;selectingClips=true;return true;}
    }
    if(activeArrangeTool==3&&hit){
        if(!selectedAudio.count({t,id})){selectedClipIds.clear();selectedAudio.clear();selectedAudio.insert({t,id});}
        eraseArrangementSelection();return true;
    }
    if(hit){
        selectedAudio.clear();selectedClipIds.clear();selectedAudio.insert({t,id});
        if(activeArrangeTool==4){if(!owner.audioEngine.splitArrangementAudioClip(t,id,seconds))operationMessage=juce::String::fromUTF8("Placez la coupe à l’intérieur du clip");}
        else if(activeArrangeTool==5){audioClipboard=*it;audioClipboardValid=true;clipboardValid=false;operationMessage=juce::String::fromUTF8("Clip audio copié — cliquez dans le vide d’une piste audio pour coller");}
        else if(activeArrangeTool==6){it->muted=!it->muted;owner.audioEngine.replaceArrangementAudioClips(t,clips);}
        else if(activeArrangeTool==0||activeArrangeTool==7){
            audioDragOriginal=*it;audioDragTrack=t;audioDragX=p.x;audioDragLength=it->lengthSeconds;
            const auto box=audioBounds(t,*it);
            // Only the actual golden grips change a fade; the borders still resize.
            const int fadeGrip=audioFadeHandleAt(p);
            audioDragMode=activeArrangeTool==7?4:
                (fadeGrip!=0?fadeGrip:
                 p.x<=box.getX()+resizeZone?2:(p.x>=box.getRight()-resizeZone?3:1));
        }
    }else if(activeArrangeTool==2 || activeArrangeTool==5){
        AudioClip c;
        if(activeArrangeTool==5){
            if(!audioClipboardValid){operationMessage=juce::String::fromUTF8("Copiez d’abord un clip audio pour cette piste");repaint();return true;}
            c=audioClipboard;
        }else {c.lengthSeconds=4*secondsPerMeasure();c.sourceName=juce::String::fromUTF8("Région audio vide");
            c.sourceSampleRate=48000;
            if(!std::isfinite(c.lengthSeconds)||c.lengthSeconds*48000>32000000)return true;
            auto silence=std::make_shared<juce::AudioBuffer<float>>(1,juce::jmax(1,(int)std::llround(c.lengthSeconds*48000)));silence->clear();c.sourceBuffer=silence;
        }
        c.id=juce::Uuid().toString();c.startSeconds=seconds;clips.push_back(c);
        if(owner.audioEngine.replaceArrangementAudioClips(t,clips)){selectedAudio.clear();selectedAudio.insert({t,c.id});operationMessage=juce::String::fromUTF8(activeArrangeTool==5?"Clip audio collé":"Région audio vide créée");}
    }
    refreshAudio(t);return true;
}
void dragAudio(juce::Point<int> p){
    const double px=getLibertyTimelinePixelsPerSecond();if(px<=0)return;
    const double delta=(p.x-audioDragX)/px;const double minimum=juce::jmax(.01,getLibertySnapSeconds(owner.tempoBpm));
    auto clips=owner.audioEngine.getArrangementAudioClips(audioDragTrack);
    for(auto& c:clips)if(c.id==audioDragOriginal.id){
        c=audioDragOriginal;
        if(audioDragMode==8){
            c.fadeInSeconds=juce::jlimit(0.0,c.lengthSeconds,c.fadeInSeconds+delta);
            operationMessage=juce::String::fromUTF8("Fondu d’entrée : ")+juce::String(c.fadeInSeconds,2)+" s";
        }else if(audioDragMode==9){
            c.fadeOutSeconds=juce::jlimit(0.0,c.lengthSeconds,c.fadeOutSeconds-delta);
            operationMessage=juce::String::fromUTF8("Fondu de sortie : ")+juce::String(c.fadeOutSeconds,2)+" s";
        }
        else if(audioDragMode==1)c.startSeconds=snapPosition(c.startSeconds+delta);
        else if(audioDragMode==2){const double end=c.startSeconds+c.lengthSeconds;
            const double start=juce::jlimit(juce::jmax(0.0,c.startSeconds-c.offsetSeconds),juce::jmax(c.startSeconds,end-minimum),snapPosition(c.startSeconds+delta));
            c.offsetSeconds+=start-c.startSeconds;c.startSeconds=start;c.lengthSeconds=end-start;
            if(c.envelopeLengthSeconds<=0)c.envelopeLengthSeconds=audioDragOriginal.offsetSeconds+audioDragOriginal.lengthSeconds;
        }else {audioDragLength=juce::jmax(minimum,snapPosition(c.lengthSeconds+delta));
            if(audioDragMode==4){operationMessage=juce::String::fromUTF8("Étirement audio : ")+juce::String(audioDragLength,2)+" s";repaint();return;}
            if(c.envelopeLengthSeconds<=0)c.envelopeLengthSeconds=c.offsetSeconds+c.lengthSeconds;c.lengthSeconds=audioDragLength;
        }
    }
    if(owner.audioEngine.replaceArrangementAudioClips(audioDragTrack,clips))refreshAudio(audioDragTrack);
}

bool runArrangementTests(){
    const auto directory=juce::File("/private/tmp").getChildFile("Liberty-arrangement-test-"+juce::Uuid().toString());
    struct Cleanup {juce::File dir;~Cleanup(){dir.deleteRecursively();}} cleanup{directory};
    auto require=[](bool ok,const char* label){if(!ok)throw std::runtime_error(label);};
    try {
        directory.createDirectory();
        LibertyClipNames names;names.ensure("a","piano");
        require(names.display("a","Piano")=="Piano","single clip name");
        names.ensure("b","piano");require(names.display("a","Piano")=="Piano 01"&&names.display("b","Piano")=="Piano 02","numbered clip names");
        require(names.rename("a",juce::String::fromUTF8("Mélodie d’été")),"manual clip name");
        require(names.display("a","Autre")==juce::String::fromUTF8("Mélodie d’été")&&names.display("b","Autre")=="Autre 02","track rename and manual override");
        juce::XmlElement namingXml("Project");names.save(namingXml);LibertyClipNames restoredNames;restoredNames.load(namingXml);
        restoredNames.ensure("c","piano");require(restoredNames.display("c","Piano")=="Piano 03","serial after save and deletion");
        require(restoredNames.display("a","Autre")==juce::String::fromUTF8("Mélodie d’été"),"accented name persistence");
        restoredNames.ensure("copy","guitar");require(restoredNames.display("copy","Guitare")=="Guitare","destination serial");
        for(int i=4;i<=100;++i)restoredNames.ensure("n"+juce::String(i),"piano");
        require(restoredNames.display("n100","Piano")=="Piano 100","serial beyond 99");
        auto source=std::make_shared<juce::AudioBuffer<float>>(1,48000);
        for(int i=0;i<48000;++i)source->setSample(0,i,.2f*std::sin(i*.07f));
        AudioClip c;c.id="original";c.sourceName=juce::String::fromUTF8("Échantillon original");c.sourceBuffer=source;c.sourceSampleRate=48000;c.lengthSeconds=1;
        c.clipGain=.7f;c.reverse=true;c.fadeInSeconds=.02;c.fadeOutSeconds=.05;
        require(owner.audioEngine.replaceArrangementAudioClips(0,{c}),"seed audio");
        const double px=getLibertyTimelinePixelsPerSecond();
        auto point=[&](int track,double seconds){return juce::Point<int>(owner.trackHeaderWidth+(int)std::lround(seconds*px),owner.getArrangeTrackTop(track)+34);};
        // Exercise the same fade grips used by the arrangement mouse path.
        // This also verifies a complete Undo/Redo roundtrip of fade metadata.
        const auto fadeBox=audioBounds(0,c);
        if(px>20.0 && fadeBox.getWidth()>60 && fadeBox.getHeight()>32){
            const int dx=juce::jmax(2,(int)std::lround(.12*px));
            const juce::Point<int> fadeInPoint{
                fadeBox.getX()+(int)std::lround(fadeBox.getWidth()*c.fadeInSeconds/c.lengthSeconds),fadeBox.getY()+24};
            activeArrangeTool=0;
            require(audioFadeHandleAt(fadeInPoint)==8,"fade in grip hit");
            beginArrangementEdit();
            require(handleAudioDown(fadeInPoint)&&audioDragMode==8,"fade in mouse routing");
            dragAudio(fadeInPoint.translated(dx,0));
            audioDragTrack=-1;audioDragMode=0;finishArrangementEdit();
            const auto changed=owner.audioEngine.getArrangementAudioClips(0).front();
            require(std::abs(changed.fadeInSeconds-(c.fadeInSeconds+(double)dx/px))<.00001,
                    "fade in drag duration");
            require(changed.sourceBuffer==source&&changed.fadeOutSeconds==c.fadeOutSeconds
                    && changed.clipGain==c.clipGain&&changed.reverse==c.reverse,
                    "fade drag must retain audio properties");
            undoArrangement(false);
            require(std::abs(owner.audioEngine.getArrangementAudioClips(0).front().fadeInSeconds-c.fadeInSeconds)<.00001,
                    "undo fade");
            undoArrangement(true);
            require(std::abs(owner.audioEngine.getArrangementAudioClips(0).front().fadeInSeconds-changed.fadeInSeconds)<.00001,
                    "redo fade");
            undoArrangement(false); // Restore the starting clip for the remaining tests.
            const juce::Point<int> fadeOutPoint{
                fadeBox.getRight()-(int)std::lround(fadeBox.getWidth()*c.fadeOutSeconds/c.lengthSeconds),fadeBox.getY()+24};
            require(audioFadeHandleAt(fadeOutPoint)==9,"fade out grip hit");
            beginArrangementEdit();
            require(handleAudioDown(fadeOutPoint)&&audioDragMode==9,"fade out mouse routing");
            dragAudio(fadeOutPoint.translated(-dx,0));
            audioDragTrack=-1;audioDragMode=0;finishArrangementEdit();
            require(std::abs(owner.audioEngine.getArrangementAudioClips(0).front().fadeOutSeconds
                             -(c.fadeOutSeconds+(double)dx/px))<.00001,"fade out drag duration");
            undoArrangement(false);
            require(std::abs(owner.audioEngine.getArrangementAudioClips(0).front().fadeOutSeconds-c.fadeOutSeconds)<.00001,
                    "undo fade out");
        }

        activeArrangeTool=4;beginArrangementEdit();handleAudioDown(point(0,.5));finishArrangementEdit();
        require(owner.audioEngine.getArrangementAudioClips(0).size()==2,"scissors route");
        undoArrangement(false);require(owner.audioEngine.getArrangementAudioClips(0).size()==1,"undo scissors");
        undoArrangement(true);require(owner.audioEngine.getArrangementAudioClips(0).size()==2,"redo scissors");
        activeArrangeTool=6;beginArrangementEdit();handleAudioDown(point(0,.75));finishArrangementEdit();
        auto regions=owner.audioEngine.getArrangementAudioClips(0);
        require(regions[1].muted&&!owner.audioEngine.isTrackMuted(0),"independent mute");
        activeArrangeTool=5;handleAudioDown(point(0,.75));beginArrangementEdit();handleAudioDown(point(1,2));finishArrangementEdit();
        require(owner.audioEngine.getArrangementAudioClips(1).size()==1,"audio paste");
        const auto pasted=owner.audioEngine.getArrangementAudioClips(1).front();
        require(pasted.sourceBuffer==source&&pasted.reverse&&pasted.muted&&pasted.clipGain==.7f&&pasted.offsetSeconds==.5,"paste properties");
        activeArrangeTool=2;beginArrangementEdit();handleAudioDown(point(2,0));finishArrangementEdit();
        const auto empty=owner.audioEngine.getArrangementAudioClips(2).front();
        require(empty.sourceBuffer&&empty.sourceBuffer->getMagnitude(0,empty.sourceBuffer->getNumSamples())==0,"empty audio region");
        require(owner.openAudioClipInDockAt(point(0,.75)),"dock extra region");
        require(owner.audioEngine.getAudioClipId(0)==regions[1].id&&!owner.dockMixerMode,"dock target");
        owner.audioEngine.setTrackClipGain(0,.3f);
        require(owner.audioEngine.getArrangementAudioClips(0)[1].clipGain==.7f,"dock isolation");
        clearArrangementHistory();
        Clip midi;midi.id=allocateClipId();midi.trackLane=0;midi.startSeconds=2;midi.lengthSeconds=1;midi.userLength=true;
        MidiEngine::NoteEvent note;note.startTick=120;note.lengthTicks=240;note.pitch=64;note.velocity=90;note.channel=1;midi.notes={note};midiClips.push_back(midi);
        owner.setSize(1600,3000);
        rectangleAnchor={owner.trackHeaderWidth,owner.getArrangeTop()};
        rectangleEnd={owner.trackHeaderWidth+(int)(4*px),owner.getArrangeTrackTop(owner.getAudioTrackCount())+owner.getArrangeTrackHeight(owner.getAudioTrackCount())};
        updateRectangleSelection();require(selectedClipIds.count(midi.id)&&selectedAudio.size()>=3,"mixed rectangle");
        const auto originalMidiStart=midiClips.front().startSeconds;
        const auto originalAudioStart=owner.audioEngine.getArrangementAudioClips(0).front().startSeconds;
        beginArrangementEdit();startGroup(originalMidiStart,0);moveSelectedGroup(1.25);finishArrangementEdit();
        require(midiClips.front().startSeconds==originalMidiStart+1.25&&owner.audioEngine.getArrangementAudioClips(0).front().startSeconds==originalAudioStart+1.25,"mixed move");
        undoArrangement(false);require(midiClips.front().startSeconds==originalMidiStart,"undo mixed move");
        updateRectangleSelection();
        beginArrangementEdit();eraseArrangementSelection();finishArrangementEdit();
        require(midiClips.empty()&&owner.audioEngine.getArrangementAudioClips(0).empty(),"mixed erase");
        undoArrangement(false);require(midiClips.size()==1&&owner.audioEngine.getArrangementAudioClips(0).size()==2,"mixed undo");
        // Naming uses the actual clip identities, without changing notes or media.
        syncClipNames();const auto audioKey="audio:"+owner.audioEngine.getArrangementAudioClips(0).front().id;
        const auto midiKey="midi:"+juce::String(midiClips.front().id);
        const auto namingBefore=clipNames.entries.at(audioKey).number;
        setLibertyTrackName(0,"Guitare");require(clipNames.display(audioKey,trackClipName(0)).startsWith("Guitare "),"live track name");
        setLibertyTrackName(0,juce::String::fromUTF8("Guitare électrique"));require(clipNames.display(audioKey,trackClipName(0)).startsWith(juce::String::fromUTF8("Guitare électrique ")),"live accented track rename");
        beginArrangementEdit();require(clipNames.rename(audioKey,juce::String::fromUTF8("Prise préférée")),"rename audio");finishArrangementEdit();
        undoArrangement(false);require(clipNames.entries.at(audioKey).manual.isEmpty(),"undo name");
        undoArrangement(true);require(clipNames.entries.at(audioKey).manual==juce::String::fromUTF8("Prise préférée"),"redo name");
        require(clipNames.rename(midiKey,juce::String::fromUTF8("Thème principal")),"rename MIDI");
        const auto named=namedClips();for(const auto& namedClip:named)if(namedClip.key==audioKey){
            require(nameKeyAt(nameBounds(namedClip).getCentre())==audioKey,"title hit area");
            require(nameKeyAt({namedClip.bounds.getCentreX(),namedClip.bounds.getBottom()-5}).isEmpty(),"body editor hit area");
        }
        // Exercise the real project writer, relative media paths and reader.
        const auto project=directory.getChildFile("Régions.liberty");
        require(owner.saveProjectToFile(project),"save project");
        const auto saved=owner.audioEngine.getArrangementAudioClips(0);
        const auto moved=directory.getChildFile("Déplacé");moved.createDirectory();
        require(project.copyFileTo(moved.getChildFile(project.getFileName())),"portable project copy");
        const auto media=directory.getChildFile("Régions_Media");
        require(media.copyDirectoryTo(moved.getChildFile(media.getFileName()))&&media.moveFileTo(directory.getChildFile("Médias originaux")),"portable media copy");
        require(owner.loadProjectFromFile(moved.getChildFile(project.getFileName())),"load relocated project");
        const auto loaded=owner.audioEngine.getArrangementAudioClips(0);
        require(loaded.size()==saved.size()&&loaded[0].id==saved[0].id&&loaded[0].muted==saved[0].muted
            &&loaded[0].offsetSeconds==saved[0].offsetSeconds&&loaded[0].clipGain==saved[0].clipGain,"project properties");
        require(loaded[0].sourceBuffer&&loaded[1].sourceBuffer,"project media");
        for(int i=0;i<48000;++i)require(std::abs(loaded[0].sourceBuffer->getSample(0,i)-source->getSample(0,i))<.000001f,"saved media fidelity");
        require(midiClips.size()==1&&midiClips[0].notes[0].startTick==120,"project MIDI");
        require(clipNames.entries.at(audioKey).manual==juce::String::fromUTF8("Prise préférée")&&clipNames.entries.at(audioKey).number==namingBefore,"audio name project persistence");
        require(clipNames.entries.at(midiKey).manual==juce::String::fromUTF8("Thème principal"),"MIDI name project persistence");
        const auto lastSerial=clipNames.counters.at("audio:0");
        selectedAudio.clear();for(const auto& clip:loaded)selectedAudio.insert({0,clip.id});eraseArrangementSelection();
        require(owner.saveProjectToFile(project)&&owner.loadProjectFromFile(project),"save deletion");
        require(owner.audioEngine.getArrangementAudioClips(0).empty(),"deleted media must not reappear");
        activeArrangeTool=2;handleAudioDown(point(0,0));syncClipNames();
        const auto createdKey="audio:"+owner.audioEngine.getArrangementAudioClips(0).front().id;
        require(clipNames.entries.at(createdKey).number>lastSerial,"no serial recycling after project reload");
        activeArrangeTool=5;handleAudioDown(point(0,.5));handleAudioDown(point(0,20));syncClipNames();
        const auto copied=owner.audioEngine.getArrangementAudioClips(0).back();
        require(clipNames.entries.at("audio:"+copied.id).number>clipNames.entries.at(createdKey).number,"audio paste unique number");
        const int midiRow=owner.getAudioTrackCount();
        activeArrangeTool=5;applyClipTool(point(midiRow,2.5));applyClipTool(point(midiRow,6));syncClipNames();
        require(midiClips.size()==2,"MIDI paste route");
        const auto pastedMidiKey="midi:"+juce::String(midiClips.back().id);
        require(clipNames.entries.at(pastedMidiKey).manual.isEmpty()&&clipNames.entries.at(pastedMidiKey).number>clipNames.entries.at(midiKey).number,"MIDI copy receives automatic unique name");
        require(midiClips.back().notes[0].startTick==120&&midiClips.back().notes[0].lengthTicks==240,"naming preserves MIDI content");
        // Capture the real JUCE painting for visual inspection, without opening a window.
        owner.setSize(1600,1500);setBounds(owner.getLocalBounds());toFront(false);
        auto preview=owner.createComponentSnapshot({owner.trackHeaderWidth,owner.getArrangeTop(),900,juce::jmin(1000,owner.getMixerTop()-owner.getArrangeTop())});
        auto output=juce::File("/private/tmp/Liberty-build-arm64/clip-names-preview.png").createOutputStream();
        if(output){juce::PNGImageFormat format;format.writeImageToStream(preview,*output);}
        return true;
    }catch(const std::exception& e){std::fprintf(stderr,"ArrangementToolsSmoke: %s\n",e.what());return false;}
}

void moveSelectedGroup(double delta){
        std::vector<double> origins;for(const auto& entry:groupOrigins)origins.push_back(entry.second);for(const auto& entry:audioOrigins)origins.push_back(entry.second);
        const double shift=LibertyMidiClipEditing::clampGroupShift(origins,delta);
        auto move=[&](auto& clips){for(auto& clip:clips){const auto it=groupOrigins.find(clip.id);if(it!=groupOrigins.end())clip.startSeconds=it->second+shift;}};
        move(midiClips);move(instrumentClips);
        for(int t=0;t<owner.getAudioTrackCount();++t){auto clips=owner.audioEngine.getArrangementAudioClips(t);bool changed=false;
            for(auto& c:clips){const auto it=audioOrigins.find({t,c.id});if(it!=audioOrigins.end()){c.startSeconds=it->second+shift;changed=true;}}
            if(changed&&owner.audioEngine.replaceArrangementAudioClips(t,clips))refreshAudio(t);
        }
        syncPlayback();repaint();owner.repaint();
}
