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
    inlineMidiFinishGesture();inlineMidiClearSelection();
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
        if(box.getWidth()<=36 || box.getHeight()<=32 || std::abs(p.y-(box.getY()+24))>10)return 0;
        const float fi=juce::jlimit(0.0f,(float)box.getWidth(),(float)(box.getWidth()*c.fadeInSeconds/c.lengthSeconds));
        const float fo=juce::jlimit(0.0f,(float)box.getWidth(),(float)(box.getWidth()*c.fadeOutSeconds/c.lengthSeconds));
        const float left=std::abs((float)p.x-(box.getX()+fi));
        const float right=std::abs((float)p.x-(box.getRight()-fo));
        if(left<=10.0f && left<=right)return 8;
        if(right<=10.0f)return 9;
        return 0;
    }
    return 0;
}
struct CrossfadeGrip { int track=-1; juce::String id; int mode=0; };
CrossfadeGrip crossfadeGripAt(juce::Point<int> p)const {
    if(activeArrangeTool!=0)return {};
    const int track=audioLaneAt(p);
    const double pixels=getLibertyTimelinePixelsPerSecond();
    if(track<0 || !std::isfinite(pixels) || pixels<=0)return {};
    const auto clips=owner.audioEngine.getArrangementAudioClips(track);
    for(auto it=clips.rbegin();it!=clips.rend();++it){
        const auto& c=*it;
        if(!c.sourceBuffer || c.autoCrossfadeInEnd<=c.autoCrossfadeInStart)continue;
        const auto bounds=audioBounds(track,c);
        if(bounds.getHeight()<43 || bounds.getWidth()<=36)continue;
        const int y=bounds.getBottom()-11;
        if(std::abs(p.y-y)>9)continue;
        const double left=owner.trackHeaderWidth+c.autoCrossfadeInStart*pixels;
        const double right=owner.trackHeaderWidth+c.autoCrossfadeInEnd*pixels;
        if(right-left<15.0){
            if(std::abs(p.x-0.5*(left+right))<=9)return {track,c.id,11};
            continue;
        }
        const double dl=std::abs(p.x-left),dr=std::abs(p.x-right);
        if(dl<=9.0 && dl<=dr)return {track,c.id,10};
        if(dr<=9.0)return {track,c.id,11};
    }
    return {};
}
AudioKey crossfadeAt(juce::Point<int> p)const {
    const int track=audioLaneAt(p);
    const double px=getLibertyTimelinePixelsPerSecond();
    if(track<0 || !std::isfinite(px) || px<=0.0)return {-1,{}};
    const double time=((double)p.x-owner.trackHeaderWidth)/px;
    for(const auto& c:owner.audioEngine.getArrangementAudioClips(track)){
        if(c.autoCrossfadeOverlapEnd>c.autoCrossfadeOverlapStart && c.autoCrossfadeOverlapStart>=0.0
           && time>=c.autoCrossfadeOverlapStart && time<c.autoCrossfadeOverlapEnd
           && audioBounds(track,c).contains(p))return {track,c.id};
    }
    return {-1,{}};
}
bool showAudioCrossfadeMenu(juce::Point<int> p){
    const auto hit=crossfadeAt(p);
    if(hit.first<0)return false;
    const auto clips=owner.audioEngine.getArrangementAudioClips(hit.first);
    const auto it=std::find_if(clips.begin(),clips.end(),[&](const auto& c){return c.id==hit.second;});
    if(it==clips.end())return false;
    juce::PopupMenu menu;
    menu.addSectionHeader(juce::String::fromUTF8("COURBE DE FONDU CROISÉ"));
    menu.addItem(1,juce::String::fromUTF8("Linéaire"),true,it->crossfadeCurve==0);
    menu.addItem(2,juce::String::fromUTF8("Puissance égale"),true,it->crossfadeCurve==1);
    menu.addItem(3,juce::String::fromUTF8("Courbe en S"),true,it->crossfadeCurve==2);
    const auto screen=localPointToGlobal(p);
    juce::Component::SafePointer<MultiMidiClipController> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea({screen.x,screen.y,1,1}),
                       [safe,track=hit.first,id=hit.second](int choice){
        if(!safe || choice<1 || choice>3)return;
        auto current=safe->owner.audioEngine.getArrangementAudioClips(track);
        auto entry=std::find_if(current.begin(),current.end(),[&](const auto& c){return c.id==id;});
        if(entry==current.end() || entry->autoCrossfadeInEnd<=entry->autoCrossfadeInStart)return;
        if(entry->crossfadeCurve==choice-1)return;
        safe->beginArrangementEdit();
        entry->crossfadeCurve=choice-1;
        if(safe->owner.audioEngine.replaceArrangementAudioClips(track,current)){
            safe->operationMessage=juce::String::fromUTF8("Courbe de fondu croisé modifiée");
            safe->owner.repaint();safe->repaint();
        }
        safe->finishArrangementEdit();
    });
    return true;
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
void paintAudioFadeGrip(juce::Graphics& g,float x,float y)const {
    // Opaque dark halo plus pale ring: visible on every one of Liberty's 32 colours.
    g.setColour(juce::Colour(0xff07121f).withAlpha(.93f));
    g.fillEllipse(x-9.0f,y-9.0f,18.0f,18.0f);
    g.setColour(juce::Colour(0xfff4f9ff));
    g.fillEllipse(x-7.4f,y-7.4f,14.8f,14.8f);
    g.setColour(juce::Colour(0xffffc95e));
    g.fillEllipse(x-5.7f,y-5.7f,11.4f,11.4f);
    g.setColour(juce::Colour(0xff713e0b));
    g.drawEllipse(x-5.7f,y-5.7f,11.4f,11.4f,1.0f);
}
void paintAudioFades(juce::Graphics& g){
    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(owner.trackHeaderWidth,owner.getArrangeTop(),owner.getWidth()-owner.trackHeaderWidth,owner.getMixerTop()-owner.getArrangeTop());
    for(int t=0;t<owner.getAudioTrackCount();++t)for(const auto& c:owner.audioEngine.getArrangementAudioClips(t)){
        if(!c.sourceBuffer || c.lengthSeconds<=0.0)continue;
        const auto box=audioBounds(t,c).toFloat();
        if(box.getWidth()<=36.0f || box.getHeight()<=32.0f)continue;
        // Derived crossfades are displayed without changing the manual gold grips.
        // Both ramps form an X inside the shared region of two adjacent clips.
        const double pixelsPerSecond=getLibertyTimelinePixelsPerSecond();
        auto drawCrossfade=[&](double from,double until,bool increasing){
            if(from<0.0 || until<=from || pixelsPerSecond<=0.0)return;
            const float x1=juce::jlimit(box.getX(),box.getRight(),
                box.getX()+(float)((from-c.startSeconds)*pixelsPerSecond));
            const float x2=juce::jlimit(box.getX(),box.getRight(),
                box.getX()+(float)((until-c.startSeconds)*pixelsPerSecond));
            if(x2-x1<2.0f)return;
            const float y1=box.getY()+30.0f,y2=box.getBottom()-7.0f;
            g.setColour(juce::Colour(0x1fffcb80));
            g.fillRect(juce::Rectangle<float>(x1,y1,x2-x1,juce::jmax(0.0f,y2-y1)));
            g.setColour(juce::Colour(0xffffd88c));
            juce::Path curve;
            for(int step=0;step<=24;++step){
                const double progress=(double)step/24.0;
                const float x=x1+(float)progress*(x2-x1);
                const float level=(float)LibertyAudioCrossfade::level(c.autoCrossfadeCurve,progress,increasing);
                const float y=y2-level*(y2-y1);
                if(step==0)curve.startNewSubPath(x,y);else curve.lineTo(x,y);
            }
            g.setColour(juce::Colour(0xff0b1b29).withAlpha(.83f));
            g.strokePath(curve,juce::PathStrokeType(3.8f));
            g.setColour(juce::Colour(0xffffd88c));
            g.strokePath(curve,juce::PathStrokeType(2.2f));
        };
        drawCrossfade(c.autoCrossfadeInStart,c.autoCrossfadeInEnd,true);
        drawCrossfade(c.autoCrossfadeOutStart,c.autoCrossfadeOutEnd,false);
        if(c.autoCrossfadeInEnd>c.autoCrossfadeInStart && box.getHeight()>=43){
            const float left=owner.trackHeaderWidth+(float)(c.autoCrossfadeInStart*pixelsPerSecond);
            const float right=owner.trackHeaderWidth+(float)(c.autoCrossfadeInEnd*pixelsPerSecond);
            const float y=box.getBottom()-11.0f;
            if(right-left<15.0f){
                paintAudioFadeGrip(g,(left+right)*0.5f,y);
            }else{
                paintAudioFadeGrip(g,left,y);
                paintAudioFadeGrip(g,right,y);
            }
        }
        const float fi=juce::jlimit(0.0f,box.getWidth(),(float)(box.getWidth()*c.fadeInSeconds/c.lengthSeconds));
        const float fo=juce::jlimit(0.0f,box.getWidth(),(float)(box.getWidth()*c.fadeOutSeconds/c.lengthSeconds));
        const float top=box.getY()+24.0f,bottom=box.getBottom()-5.0f;
        if(fi>0.0f){
            g.setColour(juce::Colour(0xff0b1b29).withAlpha(.85f));
            g.drawLine(box.getX(),bottom,box.getX()+fi,top,4.0f);
            g.setColour(juce::Colour(0xffffd88c));
            g.drawLine(box.getX(),bottom,box.getX()+fi,top,2.4f);
        }
        if(fo>0.0f){
            g.setColour(juce::Colour(0xff0b1b29).withAlpha(.85f));
            g.drawLine(box.getRight()-fo,top,box.getRight(),bottom,4.0f);
            g.setColour(juce::Colour(0xffffd88c));
            g.drawLine(box.getRight()-fo,top,box.getRight(),bottom,2.4f);
        }
        paintAudioFadeGrip(g,box.getX()+fi,top);
        paintAudioFadeGrip(g,box.getRight()-fo,top);
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
    auto clips=owner.audioEngine.getArrangementAudioClips(t);
    const auto crossGrip=crossfadeGripAt(p);
    const auto id=crossGrip.mode!=0?crossGrip.id:audioAt(p);
    auto it=std::find_if(clips.begin(),clips.end(),[&](const auto& c){return c.id==id;});
    const bool hit=it!=clips.end();const double px=getLibertyTimelinePixelsPerSecond();if(px<=0)return true;
    const double seconds=snapPosition((p.x-owner.trackHeaderWidth)/px);
    if(activeArrangeTool==0||activeArrangeTool==1){
        if(hit&&selectedAudio.count({t,id})&&audioFadeHandleAt(p)==0&&crossGrip.mode==0
           &&(activeArrangeTool==1||selectedAudio.size()+selectedClipIds.size()>1)){
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
                (crossGrip.mode!=0?crossGrip.mode:
                 fadeGrip!=0?fadeGrip:
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
        else if(audioDragMode==10||audioDragMode==11){
            const double limit=audioDragOriginal.autoCrossfadeOverlapEnd-audioDragOriginal.autoCrossfadeOverlapStart;
            if(limit<=0.0)break;
            const double original=audioDragOriginal.autoCrossfadeInEnd-audioDragOriginal.autoCrossfadeInStart;
            const double requested=original+(audioDragMode==10?-2.0:2.0)*delta;
            const double adjusted=juce::jlimit(juce::jmin(.01,limit),limit,requested);
            c.crossfadeDurationSeconds=adjusted;
            operationMessage=juce::String::fromUTF8("Durée du fondu croisé : ")+juce::String(adjusted,3)+" s";
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
    if(owner.audioEngine.replaceArrangementAudioClips(audioDragTrack,clips)){
        // A fade edits envelope metadata only; no waveform peak cache needs rebuilding.
        if(audioDragMode==8||audioDragMode==9||audioDragMode==10||audioDragMode==11){owner.repaint();repaint();}
        else refreshAudio(audioDragTrack);
    }
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
        // Larger on-screen targets must be usable near the golden centre.
        if(px>20.0 && fadeBox.getWidth()>60 && fadeBox.getHeight()>32){
            const int handleX=fadeBox.getX()+(int)std::lround(fadeBox.getWidth()*c.fadeInSeconds/c.lengthSeconds);
            require(audioFadeHandleAt({handleX+8,fadeBox.getY()+24})==8,
                "larger manual fade grab region");
        }
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

        // The real arrangement drag must update only the crossfade duration.
        if(px>40 && audioBounds(0,c).getHeight()>=43){
            AudioClip neighbour=c;neighbour.id="fade-neighbour";neighbour.startSeconds=.5;
            require(owner.audioEngine.replaceArrangementAudioClips(0,{c,neighbour}),"crossfade fixture");
            const auto original=owner.audioEngine.getArrangementAudioClips(0);
            const auto& secondClip=original[1];
            const int gripX=owner.trackHeaderWidth+(int)std::lround(secondClip.autoCrossfadeInEnd*px);
            const int gripY=audioBounds(0,secondClip).getBottom()-11;
            const juce::Point<int> grip{gripX,gripY};
            activeArrangeTool=0;
            require(crossfadeGripAt(grip).mode==11,"crossfade grip hit");
            beginArrangementEdit();
            require(handleAudioDown(grip)&&audioDragMode==11,"crossfade grip routing");
            const int dragPixels=juce::jmax(2,(int)std::round(.05*px));
            dragAudio(grip.translated(-dragPixels,0));
            audioDragTrack=-1;audioDragMode=0;finishArrangementEdit();
            const auto adjusted=owner.audioEngine.getArrangementAudioClips(0);
            const double wanted=.5-2.0*dragPixels/px;
            require(adjusted[1].startSeconds==.5 && adjusted[0].startSeconds==0,
                    "crossfade grip must not move clips");
            require(std::abs(adjusted[1].crossfadeDurationSeconds-wanted)<1.e-8,
                    "crossfade grip saved duration");
            require(undoArrangement(false),"crossfade undo command");
            require(owner.audioEngine.getArrangementAudioClips(0)[1].crossfadeDurationSeconds==0,
                    "crossfade undo restored automatic length");
            require(undoArrangement(true),"crossfade redo command");
            require(std::abs(owner.audioEngine.getArrangementAudioClips(0)[1].crossfadeDurationSeconds-wanted)<1.e-8,
                    "crossfade redo restored duration");
            require(owner.audioEngine.replaceArrangementAudioClips(0,{c}),"crossfade fixture cleanup");
            clearArrangementHistory();
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
        // Test direct in-place MIDI editing through the actual controller.
        owner.setArrangeTrackHeight(midiRow,120);setBounds(owner.getLocalBounds());
        midiClips.back().lengthSeconds=juce::jmax(3.0,midiClips.back().lengthSeconds);
        inlineMidiMode=true;activeArrangeTool=0;
        const int inlineIndex=(int)midiClips.size()-1;
        const auto originalNote=midiClips[(size_t)inlineIndex].notes[0];
        const auto hitRect=inlineMidiNoteBounds(midiClips[(size_t)inlineIndex],false,originalNote);
        require(!hitRect.isEmpty(),"inline MIDI note bounds");
        const auto hitPoint=hitRect.getCentre();
        bool inlineInstrumentHit=false;int inlineHitClip=-1,inlineHitNote=-1;
        require(inlineMidiAt(hitPoint,inlineInstrumentHit,inlineHitClip,inlineHitNote)
            && !inlineInstrumentHit && inlineHitClip==inlineIndex && inlineHitNote==0,
            "inline MIDI note hit testing");
        beginArrangementEdit();
        require(inlineMidiDown(hitPoint,false)&&inlineMidiDragging,"inline MIDI note drag begins");
        const int noteDelta=juce::jmax(8,(int)std::lround(.30*getLibertyTimelinePixelsPerSecond()));
        inlineMidiDrag(hitPoint.translated(noteDelta,0));
        inlineMidiDragging=false;inlineMidiOriginalNotes.clear();inlineMidiNoteIndex=-1;
        finishArrangementEdit();
        const auto updatedNote=midiClips[(size_t)inlineIndex].notes[0];
        require(updatedNote.startTick!=originalNote.startTick,"inline MIDI note time edit");
        require(updatedNote.pitch==originalNote.pitch && updatedNote.lengthTicks==originalNote.lengthTicks
                && updatedNote.velocity==originalNote.velocity,"inline MIDI preserves note attributes");
        undoArrangement(false);
        require(midiClips[(size_t)inlineIndex].notes[0].startTick==originalNote.startTick,
                "inline MIDI undo");
        undoArrangement(true);
        require(midiClips[(size_t)inlineIndex].notes[0].startTick==updatedNote.startTick,
                "inline MIDI redo");
        // The grid must remain visible above the colour-filled MIDI region.
        // Draw at actual Arrangement coordinates: no synthetic Graphics
        // transform, which would otherwise alter the clip-region intersection.
        {
            const double pxNow=getLibertyTimelinePixelsPerSecond();
            const double barSeconds=secondsPerMeasure();
            if(std::isfinite(pxNow)&&pxNow>0 && std::isfinite(barSeconds)&&barSeconds>0){
                const double barCoordinate=owner.trackHeaderWidth+barSeconds*pxNow;
                // Work near the first bar if the current zoom is extremely wide.
                const int barX=(barCoordinate>0.0&&barCoordinate<5000.0)
                    ?(int)std::lround(barCoordinate):owner.trackHeaderWidth;
                const int pictureWidth=juce::jmax(800,barX+70);
                juce::Image gridCheck(juce::Image::ARGB,pictureWidth,120,true);
                juce::Graphics gridGraphics(gridCheck);
                gridGraphics.fillAll(juce::Colour(0xff244d72));
                const int x=juce::jmax(owner.trackHeaderWidth,barX-40);
                drawMidiClipTimeGrid(gridGraphics,{x,10,80,80},true);
                const auto on=gridCheck.getPixelAt(barX,47);
                const auto off=gridCheck.getPixelAt(barX+4,47);
                require(on!=off,"MIDI clip grid visible over clip fill");
            }
        }
        // Direct resize uses a separate right-edge gesture; dragging the note body
        // above already verified moving the note without changing its duration.
        const auto resizeBefore=midiClips[(size_t)inlineIndex].notes[0];
        const auto resizedClip=midiClips[(size_t)inlineIndex];
        const auto resizeRect=inlineMidiNoteBounds(resizedClip,false,resizeBefore)
            .getIntersection(inlineMidiArea(resizedClip,false));
        require(resizeRect.getWidth()>=3,"inline MIDI resize visible bounds");
        const juce::Point<int> resizePoint{resizeRect.getRight()-1,resizeRect.getCentreY()};
        require(inlineMidiResizeGripAt(resizedClip,false,resizeBefore,resizePoint),
                "inline MIDI right-edge grip hit");
        beginArrangementEdit();
        require(inlineMidiDown(resizePoint,false) && inlineMidiDragging && inlineMidiResizing,
                "inline MIDI starts note resize rather than move");
        // Extend by two beats so the later shrink is a real edit, not a
        // no-op at the default quarter-note SNAP minimum.
        const int growPixels=juce::jmax(8,(int)std::lround(1.0*getLibertyTimelinePixelsPerSecond()));
        inlineMidiDrag(resizePoint.translated(growPixels,0));
        inlineMidiDragging=false;inlineMidiResizing=false;
        inlineMidiOriginalNotes.clear();inlineMidiNoteIndex=-1;finishArrangementEdit();
        const auto grownNote=midiClips[(size_t)inlineIndex].notes[0];
        require(grownNote.lengthTicks>resizeBefore.lengthTicks,
                "inline MIDI resize extends duration");
        require(grownNote.startTick==resizeBefore.startTick
                && grownNote.pitch==resizeBefore.pitch
                && grownNote.velocity==resizeBefore.velocity
                && grownNote.channel==resizeBefore.channel
                && grownNote.muted==resizeBefore.muted
                && grownNote.probability==resizeBefore.probability,
                "inline MIDI resize preserves note properties");
        undoArrangement(false);
        require(midiClips[(size_t)inlineIndex].notes[0].lengthTicks==resizeBefore.lengthTicks,
                "inline MIDI resize undo");
        undoArrangement(true);
        require(midiClips[(size_t)inlineIndex].notes[0].lengthTicks==grownNote.lengthTicks,
                "inline MIDI resize redo");
        // Large negative/positive drags remain inside the clip, with a positive
        // note duration, and can be undone like other Arrangement changes.
        const auto shortenedClip=midiClips[(size_t)inlineIndex];
        const auto longBounds=inlineMidiNoteBounds(shortenedClip,false,grownNote)
            .getIntersection(inlineMidiArea(shortenedClip,false));
        const juce::Point<int> longGrip{longBounds.getRight()-1,longBounds.getCentreY()};
        beginArrangementEdit();
        require(inlineMidiDown(longGrip,false)&&inlineMidiResizing,"inline MIDI shrink grip");
        inlineMidiDrag(longGrip.translated(-100000,0));
        inlineMidiDragging=false;inlineMidiResizing=false;
        inlineMidiOriginalNotes.clear();inlineMidiNoteIndex=-1;finishArrangementEdit();
        const auto shortNote=midiClips[(size_t)inlineIndex].notes[0];
        require(shortNote.lengthTicks>0 && shortNote.startTick==grownNote.startTick,
                "inline MIDI minimum duration");
        require(shortNote.lengthTicks<grownNote.lengthTicks,
                "inline MIDI shrink changes note duration");
        undoArrangement(false);
        require(midiClips[(size_t)inlineIndex].notes[0].lengthTicks==grownNote.lengthTicks,
                "inline MIDI shrink undo");
        // Ensure the 8 legacy tools retain their same positions and override
        // direct note editing when another tool is active.
        for(int tool=0;tool<8;++tool){
            require(toolAt(toolBounds(tool).getCentre())==tool,"eight arrangement tools unchanged");
            activeArrangeTool=tool;
            if(tool!=0){
                bool otherIns=false;int otherClip=-1,otherNote=-1;
                require(!inlineMidiAt(resizePoint,otherIns,otherClip,otherNote),
                    "inline MIDI only responds to selection tool");
            }
        }
        activeArrangeTool=0;inlineMidiMode=false;
        // Preview MIDI phrases without activating editing in compact lanes.
        owner.setArrangeTrackHeight(midiRow,54);setBounds(owner.getLocalBounds());
        const auto previewClip=midiClips[(size_t)inlineIndex];
        require(inlineMidiArea(previewClip,false).isEmpty(),
                "compact lane must not enable full note editor");
        const auto mini=inlineMidiPreviewArea(previewClip,false);
        require(!mini.isEmpty() && mini.getHeight()>=5,
                "compact MIDI preview has a visible region");
        const auto miniNote=inlineMidiPreviewNoteBounds(previewClip,false,
            previewClip.notes.front()).getIntersection(mini);
        require(!miniNote.isEmpty(),"compact MIDI note preview is visible");
        bool previewIns=false;int previewIndex=-1,previewNote=-1;
        require(!inlineMidiAt(miniNote.getCentre(),previewIns,previewIndex,previewNote),
                "read-only MIDI preview does not enable edit hit testing");
        const auto beforePreview=stateSignature();
        {
            juce::Image image(juce::Image::ARGB,juce::jmax(1,mini.getRight()+2),
                juce::jmax(1,mini.getBottom()+2),true);
            juce::Graphics graphics(image);
            graphics.fillAll(juce::Colour(0xff091b30));
            drawInlineMidiNotes(graphics,false,previewClip);
            require(image.getPixelAt(miniNote.getCentreX(),miniNote.getCentreY())
                    !=juce::Colour(0xff091b30),
                    "MIDI preview paints notes in normal mode");
        }
        require(stateSignature()==beforePreview,
                "rendering a MIDI preview must not change clip data");
        // Multi-select in the Arrangement: Shift-click toggles individual notes,
        // a Shift rectangle selects a group, and moving one selected note
        // preserves all offsets and attributes in the shared edit transaction.
        owner.setArrangeTrackHeight(midiRow,180);setBounds(owner.getLocalBounds());
        inlineMidiMode=true;activeArrangeTool=0;
        const int multiClip=(int)midiClips.size()-1;
        // Select the fixture first: selectClip syncs the previously active
        // Piano Roll state into its clip and must not overwrite our test notes.
        selectClip(false,multiClip);
        auto& phrase=midiClips[(size_t)multiClip];
        phrase.lengthSeconds=juce::jmax(4.0,phrase.lengthSeconds);
        MidiEngine::NoteEvent first=phrase.notes.front();
        first.startTick=240;first.lengthTicks=720;first.pitch=60;first.velocity=93;
        MidiEngine::NoteEvent second=first;
        second.pitch=64;second.velocity=71;second.channel=2;
        MidiEngine::NoteEvent third=first;
        third.startTick=1920;third.pitch=67;third.velocity=46;
        phrase.notes={first,second,third};
        require(owner.midiEngine.replaceNotes(phrase.notes),"MIDI multi-select fixture notes");
        syncPlayback();
        inlineMidiClearSelection();clearArrangementHistory();
        const auto& multiInitial=midiClips[(size_t)multiClip];
        const auto n1=inlineMidiNoteBounds(multiInitial,false,multiInitial.notes[0]).getCentre();
        const auto n2=inlineMidiNoteBounds(multiInitial,false,multiInitial.notes[1]).getCentre();
        require(inlineMidiDown(n1,false,true),"shift-select first MIDI note");
        require(inlineMidiDown(n2,false,true),"shift-select second MIDI note");
        require(inlineMidiSelectedNotes.size()==2 && inlineMidiSelectedNotes.count(0)
                && inlineMidiSelectedNotes.count(1),"additive MIDI selection");
        require(inlineMidiDown(n2,false,true) && inlineMidiSelectedNotes.size()==1,
                "shift-click deselects MIDI note");
        require(inlineMidiDown(n2,false,true) && inlineMidiSelectedNotes.size()==2,
                "shift-click reselects MIDI note");
        const auto beforeGroup=midiClips[(size_t)multiClip].notes;
        beginArrangementEdit();
        require(inlineMidiDown(n1,false) && inlineMidiDragging && !inlineMidiResizing,
                "drag selected note starts MIDI group move");
        require(inlineMidiDragSelection.size()==2,"two notes in drag snapshot");
        inlineMidiDrag(n1.translated(juce::jmax(8,(int)std::lround(
                           .5*getLibertyTimelinePixelsPerSecond())),-8));
        inlineMidiFinishGesture();finishArrangementEdit();
        const auto movedGroup=midiClips[(size_t)multiClip].notes;
        require(movedGroup.size()==3,"MIDI group keeps all notes");
        require(movedGroup[0].startTick!=beforeGroup[0].startTick
                && movedGroup[0].startTick-beforeGroup[0].startTick
                    ==movedGroup[1].startTick-beforeGroup[1].startTick,
                "MIDI group preserves note time offsets");
        require((int)movedGroup[0].pitch-(int)beforeGroup[0].pitch
                 ==(int)movedGroup[1].pitch-(int)beforeGroup[1].pitch,
                "MIDI group pitch intervals preserved");
        require(movedGroup[2].startTick==beforeGroup[2].startTick
                && movedGroup[2].pitch==beforeGroup[2].pitch,
                "unselected MIDI notes stay fixed");
        require(movedGroup[0].lengthTicks==beforeGroup[0].lengthTicks
                && movedGroup[1].lengthTicks==beforeGroup[1].lengthTicks
                && movedGroup[0].velocity==beforeGroup[0].velocity
                && movedGroup[1].velocity==beforeGroup[1].velocity
                && movedGroup[1].channel==beforeGroup[1].channel,
                "MIDI group preserves duration velocity and channel");
        undoArrangement(false);
        require(midiClips[(size_t)multiClip].notes[0].startTick==beforeGroup[0].startTick
                && midiClips[(size_t)multiClip].notes[1].startTick==beforeGroup[1].startTick,
                "MIDI group undo");
        undoArrangement(true);
        require(midiClips[(size_t)multiClip].notes[0].startTick==movedGroup[0].startTick
                && midiClips[(size_t)multiClip].notes[1].startTick==movedGroup[1].startTick,
                "MIDI group redo");
        // Group resize: drag the right edge of a selected note, preserving
        // the relative lengths, starts, pitch, velocity and channel.
        inlineMidiClearSelection();
        const auto beforeResizeGroup=midiClips[(size_t)multiClip].notes;
        const auto g1=inlineMidiNoteBounds(midiClips[(size_t)multiClip],false,
            beforeResizeGroup[0]).getCentre();
        const auto g2=inlineMidiNoteBounds(midiClips[(size_t)multiClip],false,
            beforeResizeGroup[1]).getCentre();
        require(inlineMidiDown(g1,false,true),"group resize select first");
        require(inlineMidiDown(g2,false,true),"group resize select second");
        require(inlineMidiSelectedNotes.size()==2,"group resize selection retained");
        const auto gripRect=inlineMidiNoteBounds(midiClips[(size_t)multiClip],false,
                beforeResizeGroup[0])
            .getIntersection(inlineMidiArea(midiClips[(size_t)multiClip],false));
        const auto resizeGrip=juce::Point<int>(gripRect.getRight()-1,
                                                gripRect.getCentreY());
        require(inlineMidiResizeGripAt(midiClips[(size_t)multiClip],false,
                beforeResizeGroup[0],resizeGrip),"group resize grip geometry");
        beginArrangementEdit();
        require(inlineMidiDown(resizeGrip,false) && inlineMidiResizing
                && inlineMidiDragSelection.size()==2,
                "group resize retains both notes");
        const int extendPx=juce::jmax(8,(int)std::lround(
            0.5*getLibertyTimelinePixelsPerSecond()));
        inlineMidiDrag(resizeGrip.translated(extendPx,0));
        inlineMidiFinishGesture();finishArrangementEdit();
        const auto resizedGroup=midiClips[(size_t)multiClip].notes;
        require(resizedGroup.size()==beforeResizeGroup.size()
                && resizedGroup[0].lengthTicks>beforeResizeGroup[0].lengthTicks,
                "group resize extends note lengths");
        const auto groupLengthDelta=resizedGroup[0].lengthTicks
                                    -beforeResizeGroup[0].lengthTicks;
        require(resizedGroup[1].lengthTicks-beforeResizeGroup[1].lengthTicks
                ==groupLengthDelta,"group resize keeps relative lengths");
        require(resizedGroup[2].lengthTicks==beforeResizeGroup[2].lengthTicks
                && resizedGroup[2].startTick==beforeResizeGroup[2].startTick,
                "group resize never changes unselected notes");
        for(int k=0;k<2;++k){
            const auto& after=resizedGroup[(size_t)k];
            const auto& before=beforeResizeGroup[(size_t)k];
            require(after.startTick==before.startTick
                    && after.pitch==before.pitch
                    && after.velocity==before.velocity
                    && after.channel==before.channel
                    && after.muted==before.muted
                    && after.probability==before.probability,
                    "group resize keeps MIDI attributes");
        }
        undoArrangement(false);
        require(midiClips[(size_t)multiClip].notes[0].lengthTicks
                    ==beforeResizeGroup[0].lengthTicks
                && midiClips[(size_t)multiClip].notes[1].lengthTicks
                    ==beforeResizeGroup[1].lengthTicks,
                "group resize undo");
        undoArrangement(true);
        require(midiClips[(size_t)multiClip].notes[0].lengthTicks
                    ==resizedGroup[0].lengthTicks
                && midiClips[(size_t)multiClip].notes[1].lengthTicks
                    ==resizedGroup[1].lengthTicks,
                "group resize redo");
        inlineMidiClearSelection();
        // Shrinking a group cannot pass the shortest minimum or clip end.
        const auto beforeShrink=midiClips[(size_t)multiClip].notes;
        const auto sh1=inlineMidiNoteBounds(midiClips[(size_t)multiClip],false,
            beforeShrink[0]).getCentre();
        const auto sh2=inlineMidiNoteBounds(midiClips[(size_t)multiClip],false,
            beforeShrink[1]).getCentre();
        require(inlineMidiDown(sh1,false,true)&&inlineMidiDown(sh2,false,true),
                "group shrink selection");
        const auto shrinkRect=inlineMidiNoteBounds(midiClips[(size_t)multiClip],false,
                beforeShrink[0])
            .getIntersection(inlineMidiArea(midiClips[(size_t)multiClip],false));
        const auto shrinkGrip=juce::Point<int>(shrinkRect.getRight()-1,
                                                shrinkRect.getCentreY());
        beginArrangementEdit();
        require(inlineMidiDown(shrinkGrip,false)&&inlineMidiResizing
                && inlineMidiDragSelection.size()==2,"group shrink starts");
        inlineMidiDrag(shrinkGrip.translated(-100000,0));
        inlineMidiFinishGesture();finishArrangementEdit();
        const auto shortened=midiClips[(size_t)multiClip].notes;
        require(shortened[0].lengthTicks>0&&shortened[1].lengthTicks>0
                &&shortened[0].lengthTicks<beforeShrink[0].lengthTicks
                &&shortened[1].lengthTicks<beforeShrink[1].lengthTicks,
                "group shrink clamps to positive durations");
        undoArrangement(false);
        require(midiClips[(size_t)multiClip].notes[0].lengthTicks
                    ==beforeShrink[0].lengthTicks
                && midiClips[(size_t)multiClip].notes[1].lengthTicks
                    ==beforeShrink[1].lengthTicks,
                "group shrink undo");
        // Option-drag copies multiple selected notes without moving sources.
        // The gesture spans a single Arrangement Undo transaction.
        inlineMidiClearSelection();
        const auto sourceNotes=midiClips[(size_t)multiClip].notes;
        const auto copyFirst=inlineMidiNoteBounds(midiClips[(size_t)multiClip],false,
            sourceNotes[0]).getCentre();
        const auto copySecond=inlineMidiNoteBounds(midiClips[(size_t)multiClip],false,
            sourceNotes[1]).getCentre();
        require(inlineMidiDown(copyFirst,false,true)
                &&inlineMidiDown(copySecond,false,true)
                &&inlineMidiSelectedNotes.size()==2,"duplicate selected MIDI notes");
        beginArrangementEdit();
        require(inlineMidiDown(copyFirst,true) && inlineMidiDragging
                && inlineMidiDuplicating && inlineMidiDragSelection.size()==2,
                "Option drag starts group duplication");
        require(midiClips[(size_t)multiClip].notes.size()==sourceNotes.size(),
                "Option down does not delete source notes");
        const int copyPixels=juce::jmax(8,(int)std::lround(
            0.5*getLibertyTimelinePixelsPerSecond()));
        inlineMidiDrag(copyFirst.translated(copyPixels,0));
        inlineMidiCompleteGesture();finishArrangementEdit();
        const auto copiedNotes=midiClips[(size_t)multiClip].notes;
        require(copiedNotes.size()==sourceNotes.size()+2,
                "Option drag creates exactly two new MIDI notes");
        const auto notesEqual=[](const MidiEngine::NoteEvent& a,
                                 const MidiEngine::NoteEvent& b){
            return a.startTick==b.startTick && a.lengthTicks==b.lengthTicks
                && a.pitch==b.pitch && a.velocity==b.velocity
                && a.channel==b.channel && a.muted==b.muted
                && a.probability==b.probability;
        };
        for(const auto& source:sourceNotes)
            require(std::any_of(copiedNotes.begin(),copiedNotes.end(),
                [&](const auto& note){return notesEqual(note,source);}),
                "Option drag keeps every original note unchanged");
        require(inlineMidiSelectedNotes.size()==2,
                "duplicated MIDI notes become the selected group");
        const auto copiedA=copiedNotes[(size_t)*inlineMidiSelectedNotes.begin()];
        const auto copiedB=copiedNotes[(size_t)*std::next(inlineMidiSelectedNotes.begin())];
        require(copiedA.startTick!=sourceNotes[0].startTick
                && copiedB.startTick!=sourceNotes[1].startTick,
                "copied MIDI group moves by a musical delta");
        require(copiedA.startTick-copiedB.startTick
                ==sourceNotes[0].startTick-sourceNotes[1].startTick,
                "copied MIDI group preserves source timing intervals");
        require(copiedA.lengthTicks==sourceNotes[0].lengthTicks
                &&copiedB.lengthTicks==sourceNotes[1].lengthTicks
                &&copiedA.velocity==sourceNotes[0].velocity
                &&copiedB.velocity==sourceNotes[1].velocity,
                "duplicate MIDI group preserves duration and velocity");
        undoArrangement(false);
        const auto restoredNotes=midiClips[(size_t)multiClip].notes;
        require(restoredNotes.size()==sourceNotes.size(),
                "Option drag Undo removes only the copies");
        for(size_t n=0;n<sourceNotes.size();++n)
            require(notesEqual(restoredNotes[n],sourceNotes[n]),
                "Option drag Undo restores exact original MIDI notes");
        undoArrangement(true);
        require(midiClips[(size_t)multiClip].notes.size()==copiedNotes.size(),
                "Option drag Redo restores MIDI copies");
        // Previous Option-click removal remains available, with no accidental
        // deletion on mouse-down: deletion occurs only on mouse-up.
        inlineMidiClearSelection();
        const auto beforeOptionClick=midiClips[(size_t)multiClip].notes;
        const auto clickPoint=inlineMidiNoteBounds(midiClips[(size_t)multiClip],false,
            beforeOptionClick[0]).getCentre();
        beginArrangementEdit();
        require(inlineMidiDown(clickPoint,true) &&inlineMidiDuplicating,
                "Option click begins a pending gesture");
        require(midiClips[(size_t)multiClip].notes.size()==beforeOptionClick.size(),
                "Option click must not delete on mouse-down");
        inlineMidiCompleteGesture();finishArrangementEdit();
        require(midiClips[(size_t)multiClip].notes.size()+1==beforeOptionClick.size(),
                "Option click still deletes exactly one MIDI note");
        undoArrangement(false);
        require(midiClips[(size_t)multiClip].notes.size()==beforeOptionClick.size(),
                "Option click delete can be undone");
        inlineMidiClearSelection();
        // Marquee in the note area uses Shift + drag from empty space.
        const auto activeArea=inlineMidiArea(midiClips[(size_t)multiClip],false);
        const auto emptyPoint=juce::Point<int>(activeArea.getX()+2,activeArea.getBottom()-2);
        require(inlineMidiDown(emptyPoint,false,true) && inlineMidiMarquee,
                "shift-drag starts MIDI note rectangle");
        inlineMidiMarqueeDrag(activeArea.getTopRight().translated(-1,1));
        inlineMidiFinishGesture();
        require(inlineMidiSelectedNotes.size()>=2,
                "MIDI marquee selects intersecting notes");
        // A change to another Arrangement tool must disable note hit testing.
        for(int tool=1;tool<8;++tool){
            activeArrangeTool=tool;
            bool ins=false;int ci=-1,ni=-1;
            require(!inlineMidiAt(n1,ins,ci,ni),
                    "eight existing Arrangement tools take precedence");
        }
        activeArrangeTool=0;
        // Keyboard nudge stays local to the selected MIDI notes and is
        // independently undoable, with the SNAP and all MIDI values intact.
        // Restore a three-note fixture: the duplication test above intentionally
        // creates coincident notes, which would make index-based assertions
        // ambiguous after the MIDI engine re-sorts equal-start copies.
        inlineMidiClearSelection();
        midiClips[(size_t)multiClip].notes={first,second,third};
        require(owner.midiEngine.replaceNotes(
            midiClips[(size_t)multiClip].notes),"keyboard nudge fixture notes");
        syncPlayback();clearArrangementHistory();
        const auto keyboardBefore=midiClips[(size_t)multiClip].notes;
        require(keyboardBefore.size()>=3,"nudge fixture has three notes");
        inlineMidiSelectedClipId=midiClips[(size_t)multiClip].id;
        inlineMidiSelectedInstrument=false;
        inlineMidiSelectedNotes={0,1};
        require(inlineMidiNudgeSelected(juce::KeyPress::rightKey,false),
            "MIDI keyboard right nudge dispatched");
        const auto keyboardMoved=midiClips[(size_t)multiClip].notes;
        const auto snapSeconds=getLibertySnapSeconds(owner.tempoBpm);
        const auto stepTicks=snapSeconds>0?juce::jmax<std::int64_t>(1,
            MidiEngine::secondsToTick(snapSeconds,owner.tempoBpm))
            :MidiEngine::ticksPerQuarterNote/4;
        require(keyboardMoved[0].startTick-keyboardBefore[0].startTick==stepTicks
            && keyboardMoved[1].startTick-keyboardBefore[1].startTick==stepTicks,
            "MIDI keyboard respects horizontal snap");
        require(keyboardMoved[2].startTick==keyboardBefore[2].startTick
            && keyboardMoved[0].lengthTicks==keyboardBefore[0].lengthTicks
            && keyboardMoved[1].lengthTicks==keyboardBefore[1].lengthTicks
            && keyboardMoved[0].velocity==keyboardBefore[0].velocity
            && keyboardMoved[1].channel==keyboardBefore[1].channel,
            "MIDI nudge preserves other notes and properties");
        require(inlineMidiSelectedNotes.size()==2,
            "MIDI note selection follows the moved notes");
        undoArrangement(false);
        require(midiClips[(size_t)multiClip].notes[0].startTick==keyboardBefore[0].startTick
            && midiClips[(size_t)multiClip].notes[1].startTick==keyboardBefore[1].startTick,
            "MIDI keyboard undo");
        undoArrangement(true);
        require(midiClips[(size_t)multiClip].notes[0].startTick==keyboardMoved[0].startTick
            && midiClips[(size_t)multiClip].notes[1].startTick==keyboardMoved[1].startTick,
            "MIDI keyboard redo");
        // Undo/Redo clears the transient selection by design.
        inlineMidiSelectedClipId=midiClips[(size_t)multiClip].id;
        inlineMidiSelectedInstrument=false;
        inlineMidiSelectedNotes={0,1};
        require(inlineMidiNudgeSelected(juce::KeyPress::upKey,false),
            "MIDI up arrow dispatched");
        const auto keyboardPitched=midiClips[(size_t)multiClip].notes;
        require(keyboardPitched[0].pitch==keyboardMoved[0].pitch+1
            && keyboardPitched[1].pitch==keyboardMoved[1].pitch+1
            && keyboardPitched[2].pitch==keyboardMoved[2].pitch,
            "MIDI keyboard transposes selected notes by semitone");
        undoArrangement(false);
        require(midiClips[(size_t)multiClip].notes[0].pitch==keyboardMoved[0].pitch,
            "MIDI pitch nudge undo");
        inlineMidiSelectedClipId=midiClips[(size_t)multiClip].id;
        inlineMidiSelectedInstrument=false;
        inlineMidiSelectedNotes={0,1};
        const auto beforeOctave=midiClips[(size_t)multiClip].notes;
        require(inlineMidiNudgeSelected(juce::KeyPress::upKey,true),
            "Shift up arrow dispatched");
        const auto octave=midiClips[(size_t)multiClip].notes;
        require(octave[0].pitch==beforeOctave[0].pitch+12
            && octave[1].pitch==beforeOctave[1].pitch+12,
            "Shift up arrow transposes notes by octave");
        undoArrangement(false);
        // Velocity editing works on multi-selection without shifting note
        // start, pitch, duration or any other MIDI expression data.
        inlineMidiMode=true;inlineMidiVelocityMode=true;
        inlineMidiClearSelection();
        const auto beforeVelocity=midiClips[(size_t)multiClip].notes;
        require(beforeVelocity.size()==3,"velocity test fixture");
        inlineMidiSelectedClipId=midiClips[(size_t)multiClip].id;
        inlineMidiSelectedInstrument=false;
        inlineMidiSelectedNotes={0,1};
        const auto velocityNoteBounds=inlineMidiNoteBounds(
            midiClips[(size_t)multiClip],false,beforeVelocity[0])
            .getIntersection(inlineMidiArea(midiClips[(size_t)multiClip],false));
        require(!velocityNoteBounds.isEmpty(),"velocity editing note visible");
        const auto velocityPoint=velocityNoteBounds.getCentre();
        beginArrangementEdit();
        require(inlineMidiDown(velocityPoint,false) && inlineMidiVelocityDragging
            && inlineMidiDragSelection.size()==2 && !inlineMidiResizing,
            "velocity drag captures selected notes");
        inlineMidiDrag(velocityPoint.translated(0,-20));
        inlineMidiCompleteGesture();finishArrangementEdit();
        const auto afterVelocity=midiClips[(size_t)multiClip].notes;
        require(afterVelocity.size()==beforeVelocity.size()
            && afterVelocity[0].velocity==beforeVelocity[0].velocity+10
            && afterVelocity[1].velocity==beforeVelocity[1].velocity+10
            && afterVelocity[2].velocity==beforeVelocity[2].velocity,
            "velocity drag applies shared offset only to selected notes");
        for(size_t k=0;k<beforeVelocity.size();++k){
            const auto& a=beforeVelocity[k];const auto& b=afterVelocity[k];
            require(a.startTick==b.startTick && a.lengthTicks==b.lengthTicks
                && a.pitch==b.pitch && a.channel==b.channel
                && a.muted==b.muted && a.probability==b.probability,
                "velocity changes preserve timing pitch channel and probability");
        }
        undoArrangement(false);
        require(midiClips[(size_t)multiClip].notes[0].velocity
                   ==beforeVelocity[0].velocity
            && midiClips[(size_t)multiClip].notes[1].velocity
                   ==beforeVelocity[1].velocity,
            "group velocity undo");
        undoArrangement(true);
        require(midiClips[(size_t)multiClip].notes[0].velocity
                   ==afterVelocity[0].velocity,
            "group velocity redo");
        // Mouse drag clamp preserves the original relative difference even
        // when one note reaches MIDI velocity 127.
        inlineMidiSelectedClipId=midiClips[(size_t)multiClip].id;
        inlineMidiSelectedInstrument=false;inlineMidiSelectedNotes={0,1};
        const auto beforeMax=midiClips[(size_t)multiClip].notes;
        const auto maxPoint=inlineMidiNoteBounds(
            midiClips[(size_t)multiClip],false,beforeMax[0]).getCentre();
        beginArrangementEdit();
        require(inlineMidiDown(maxPoint,false)&&inlineMidiVelocityDragging,
            "velocity saturation drag starts");
        inlineMidiDrag(maxPoint.translated(0,-500));
        inlineMidiCompleteGesture();finishArrangementEdit();
        const auto saturated=midiClips[(size_t)multiClip].notes;
        require(saturated[0].velocity<=127 && saturated[1].velocity<=127
            && (int)saturated[0].velocity-(int)saturated[1].velocity
              ==(int)beforeMax[0].velocity-(int)beforeMax[1].velocity,
            "velocity saturation maintains dynamic differences");
        undoArrangement(false);
        // Fixed velocity command is a single undoable edit; unselected
        // notes and the dock's independent controls are unchanged.
        inlineMidiSelectedClipId=midiClips[(size_t)multiClip].id;
        inlineMidiSelectedInstrument=false;inlineMidiSelectedNotes={0,1};
        const auto beforeFixed=midiClips[(size_t)multiClip].notes;
        require(inlineMidiSetVelocitySelected(64),"set fixed velocity");
        const auto fixed=midiClips[(size_t)multiClip].notes;
        require(fixed[0].velocity==64 && fixed[1].velocity==64
            && fixed[2].velocity==beforeFixed[2].velocity,
            "fixed velocity changes selection only");
        undoArrangement(false);
        require(midiClips[(size_t)multiClip].notes[0].velocity
                    ==beforeFixed[0].velocity
            && midiClips[(size_t)multiClip].notes[1].velocity
                    ==beforeFixed[1].velocity,
            "fixed velocity undo");
        // Linear velocity ramps use note start times, not selection order.
        // Simultaneous chord notes share the beginning value.
        inlineMidiClearSelection();
        const auto rampBefore=midiClips[(size_t)multiClip].notes;
        require(rampBefore.size()==3
            && rampBefore[0].startTick==rampBefore[1].startTick
            && rampBefore[2].startTick>rampBefore[1].startTick,
            "velocity ramp chord/time fixture");
        inlineMidiSelectedClipId=midiClips[(size_t)multiClip].id;
        inlineMidiSelectedInstrument=false;
        inlineMidiSelectedNotes={0,1,2};
        require(inlineMidiRampVelocitySelected(40,120),
            "crescendo menu action");
        const auto crescendo=midiClips[(size_t)multiClip].notes;
        require(crescendo[0].velocity==40 && crescendo[1].velocity==40
            && crescendo[2].velocity==120,
            "crescendo follows timeline and treats chord notes equally");
        for(size_t k=0;k<rampBefore.size();++k){
            const auto& oldNote=rampBefore[k];
            const auto& newNote=crescendo[k];
            require(oldNote.startTick==newNote.startTick
                && oldNote.lengthTicks==newNote.lengthTicks
                && oldNote.pitch==newNote.pitch
                && oldNote.channel==newNote.channel
                && oldNote.muted==newNote.muted
                && oldNote.probability==newNote.probability,
                "velocity ramp preserves all non-velocity MIDI properties");
        }
        undoArrangement(false);
        for(size_t k=0;k<rampBefore.size();++k)
            require(midiClips[(size_t)multiClip].notes[k].velocity
                ==rampBefore[k].velocity,
                "crescendo undo restores each velocity");
        undoArrangement(true);
        require(midiClips[(size_t)multiClip].notes[0].velocity==40
            && midiClips[(size_t)multiClip].notes[2].velocity==120,
            "crescendo redo");
        // A two-note selection may leave the third note untouched.
        inlineMidiSelectedClipId=midiClips[(size_t)multiClip].id;
        inlineMidiSelectedInstrument=false;
        inlineMidiSelectedNotes={0,2};
        const auto beforeDim=midiClips[(size_t)multiClip].notes;
        require(inlineMidiRampVelocitySelected(110,30),
            "diminuendo menu action");
        const auto diminuendo=midiClips[(size_t)multiClip].notes;
        require(diminuendo[0].velocity==110
            && diminuendo[2].velocity==30
            && diminuendo[1].velocity==beforeDim[1].velocity,
            "diminuendo changes only selected notes");
        undoArrangement(false);
        require(midiClips[(size_t)multiClip].notes[0].velocity==beforeDim[0].velocity
            && midiClips[(size_t)multiClip].notes[2].velocity==beforeDim[2].velocity,
            "diminuendo undo");
        // Validate all three velocity curves independently of the dialog UI.
        require(LibertyInlineVelocity::velocityAt(40,120,0.0,
            LibertyInlineVelocity::Curve::linear)==40
            &&LibertyInlineVelocity::velocityAt(40,120,1.0,
            LibertyInlineVelocity::Curve::linear)==120,
            "custom MIDI velocity curve endpoints");
        require(LibertyInlineVelocity::velocityAt(40,120,0.25,
            LibertyInlineVelocity::Curve::linear)==60
            &&LibertyInlineVelocity::velocityAt(40,120,0.25,
            LibertyInlineVelocity::Curve::exponential)==45
            &&LibertyInlineVelocity::velocityAt(40,120,0.25,
            LibertyInlineVelocity::Curve::sCurve)==53,
            "linear exponential and S velocity interpolation");
        // Create a four-note clip with a real midpoint, preserving the
        // engine's chronological order and never holding references across
        // Arrangement Undo/Redo (which restores clip vectors by value).
        const auto baseline=midiClips[(size_t)multiClip].notes;
        require(baseline.size()==3,"curve fixture has three notes");
        auto midpoint=baseline[2];
        midpoint.startTick=(baseline[0].startTick+baseline[2].startTick)/2;
        midpoint.pitch=72;
        auto fourNotes=baseline;
        fourNotes.push_back(midpoint);
        require(owner.midiEngine.replaceNotes(fourNotes),
            "custom curve intermediate MIDI note");
        midiClips[(size_t)multiClip].notes=owner.midiEngine.getNotesCopy();
        syncPlayback();
        clearArrangementHistory();
        inlineMidiSelectedClipId=midiClips[(size_t)multiClip].id;
        inlineMidiSelectedInstrument=false;inlineMidiSelectedNotes={0,2,3};
        const auto originalCurveNotes=midiClips[(size_t)multiClip].notes;
        require(originalCurveNotes.size()==4
            && originalCurveNotes[2].startTick>originalCurveNotes[0].startTick
            && originalCurveNotes[3].startTick>originalCurveNotes[2].startTick,
            "custom curve notes in musical time order");
        require(inlineMidiRampVelocitySelected(40,120,
            LibertyInlineVelocity::Curve::exponential),
            "apply custom exponential curve");
        const auto curved=midiClips[(size_t)multiClip].notes;
        require(curved.size()==4 && curved[0].velocity==40
            && curved[2].velocity==60
            && curved[3].velocity==120
            && curved[1].velocity==originalCurveNotes[1].velocity,
            "exponential ramp preserves unselected chord note");
        undoArrangement(false);
        require(midiClips[(size_t)multiClip].notes[2].velocity
            ==originalCurveNotes[2].velocity,
            "custom exponential curve undo");
        undoArrangement(true);
        require(midiClips[(size_t)multiClip].notes[2].velocity==curved[2].velocity,
            "custom exponential curve redo");
        inlineMidiSelectedClipId=midiClips[(size_t)multiClip].id;
        inlineMidiSelectedInstrument=false;inlineMidiSelectedNotes={0,2,3};
        require(inlineMidiRampVelocitySelected(40,120,
            LibertyInlineVelocity::Curve::sCurve),
            "apply custom S curve");
        const auto smooth=midiClips[(size_t)multiClip].notes;
        require(smooth[0].velocity==40 && smooth[2].velocity==80
            && smooth[3].velocity==120,
            "custom S curve musical midpoint");
        undoArrangement(false);
        require(midiClips[(size_t)multiClip].notes[2].velocity==curved[2].velocity,
            "custom S curve undo");
        // Construct the JUCE dialog with no modal loop or active changes.
        bool dialogApplied=false;
        LibertyInlineVelocity::DialogContent dialog(32,112,
            LibertyInlineVelocity::Curve::sCurve,
            [&](int,int,LibertyInlineVelocity::Curve,LibertyInlineVelocity::Mode,int,int,std::uint32_t,int,LibertyInlineVelocity::AccentPattern,int,int,int,bool,LibertyInlineVelocity::Curve){
                dialogApplied=true;
            });
        dialog.bindInputs();
        require(dialog.getWidth()==530 && dialog.getHeight()==846
            && !dialogApplied,"blue velocity window geometry and no side effects");
        require(dialog.hasVisibleNumberFields()
            && dialog.getNumChildComponents()>=5,
            "velocity window shows both editable numeric fields");
        LibertyInlineVelocity::DialogContent populated(40,120,
            LibertyInlineVelocity::Curve::exponential,
            [&](int,int,LibertyInlineVelocity::Curve,LibertyInlineVelocity::Mode,int,int,std::uint32_t,int,LibertyInlineVelocity::AccentPattern,int,int,int,bool,LibertyInlineVelocity::Curve){
                dialogApplied=true;
            },
            {{1920,77},{240,55},{1080,91}});
        populated.bindInputs();
        require(populated.getPreviewNoteCount()==3
            && populated.getPreviewOriginalVelocity(0)==55
            && populated.getPreviewOriginalVelocity(1)==91
            && populated.getPreviewOriginalVelocity(2)==77,
            "real velocity preview sorts notes by musical time");
        require(populated.getPreviewResultVelocity(0,40,120,
                LibertyInlineVelocity::Curve::exponential)==40
            &&populated.getPreviewResultVelocity(1,40,120,
                LibertyInlineVelocity::Curve::exponential)==60
            &&populated.getPreviewResultVelocity(2,40,120,
                LibertyInlineVelocity::Curve::exponential)==120
            && !dialogApplied,
            "real velocity preview computes correct note values without applying");
        require(populated.hasVisibleNumberFields(),
            "velocity preview retains numeric controls");
        // Real mouse-hit geometry, pointer drag and numeric fields must
        // produce an interactive PREVIEW without applying a MIDI edit.
        require(populated.getStartValue()==40&&populated.getEndValue()==120,
            "velocity endpoint initial numeric values");
        const auto leftHandle=populated.endpointPosition(0);
        const auto rightHandle=populated.endpointPosition(1);
        require(populated.endpointAt(leftHandle)==0
            &&populated.endpointAt(rightHandle)==1
            &&populated.endpointAt({300.0f,180.0f})==-1,
            "velocity graph endpoint hit testing");
        require(populated.beginEndpointDragAt(leftHandle)
            &&populated.dragEndpointTo(LibertyInlineVelocity::DialogContent::previewPlot().getY()),
            "dragging graph start endpoint updates velocity");
        require(populated.getStartValue()==127 && populated.getEndValue()==120
            && populated.getCurrentPreviewVelocity(0)==127
            && populated.getCurrentPreviewVelocity(2)==120,
            "start handle updates input and before-after curve preview");
        populated.dragEndpointTo(1500.0f);
        require(populated.getStartValue()==1,
            "velocity drag clamps to MIDI minimum");
        populated.endEndpointDrag();
        require(populated.beginEndpointDragAt(rightHandle)
            &&populated.dragEndpointTo(-500.0f)
            &&populated.getEndValue()==127,
            "end handle clamps to MIDI maximum");
        populated.endEndpointDrag();
        require(!populated.dragEndpointTo(340.0f)
            && !dialogApplied,
            "endpoint drag cannot apply MIDI after release");
        // Starting values follow the original phrase, averaging simultaneous
        // chord notes instead of relying on note insertion order.
        const auto fromPhrase=LibertyInlineVelocity::boundaryVelocities(
            {{1920,90},{240,55},{240,65},{1080,80},{1920,110}});
        require(fromPhrase.first==60&&fromPhrase.second==100,
            "curve dialog initialization averages starting and ending chords");
        require(LibertyInlineVelocity::boundaryVelocities({}).first==64
            &&LibertyInlineVelocity::boundaryVelocities({}).second==100,
            "empty MIDI selection retains safe default curve values");
        // Invert and reset update the graph/numeric controls, but never
        // fire the MIDI apply callback or mutate the active clip.
        require(populated.hasVisibleActionButtons()
            &&populated.getNumChildComponents()>=7,
            "velocity dialog has visible invert and reset actions");
        populated.restoreInitialSettings();
        require(populated.getStartValue()==40&&populated.getEndValue()==120
            &&populated.getCurrentPreviewVelocity(1)==60,
            "reset restores the initial exponential curve and endpoints");
        populated.invertEndpoints();
        require(populated.getStartValue()==120&&populated.getEndValue()==40
            &&populated.getCurrentPreviewVelocity(1)==100,
            "invert swaps velocities without changing the selected curve");
        populated.restoreInitialSettings();
        require(populated.getStartValue()==40&&populated.getEndValue()==120
            &&populated.getCurrentPreviewVelocity(1)==60
            && !dialogApplied,
            "reset returns to initial values without applying MIDI edits");
        // Relative curve mode preserves accents around a phrase envelope,
        // unlike absolute mode which overwrites each selected velocity.
        const std::vector<LibertyInlineVelocity::PreviewNote> expressive={
            {0,50},{0,80},{960,100},{1920,70}};
        const auto absolute=LibertyInlineVelocity::transformVelocities(
            expressive,80,110,LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::absolute);
        const auto relative=LibertyInlineVelocity::transformVelocities(
            expressive,80,110,LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::relative);
        require(absolute.size()==4 && absolute[0]==80 && absolute[1]==80
            && absolute[2]==95 && absolute[3]==110,
            "absolute velocity curve replaces expressive accents");
        require(relative.size()==4 && relative[0]==65 && relative[1]==95
            && relative[2]==127 && relative[3]==110,
            "relative velocity curve retains local offsets from phrase envelope");
        const auto chordClamp=LibertyInlineVelocity::transformVelocities(
            {{0,70},{0,120},{960,90}},
            127,90,LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::relative);
        require(chordClamp.size()==3 && chordClamp[0]==77
            && chordClamp[1]==127 && chordClamp[2]==90,
            "relative saturation preserves intervals between chord notes");
        populated.setMode(LibertyInlineVelocity::Mode::relative);
        require(populated.getMode()==LibertyInlineVelocity::Mode::relative
            &&populated.getCurrentPreviewVelocity(0)==40
            &&populated.getCurrentPreviewVelocity(2)==120
            &&!dialogApplied,
            "relative mode refreshes preview without applying MIDI notes");
        populated.restoreInitialSettings();
        require(populated.getMode()==LibertyInlineVelocity::Mode::absolute,
            "reset restores default absolute application mode");
        // Intensity blends the exact computed MIDI result, never the note
        // time/pitch. In relative mode a shared chord delta is scaled once.
        const auto zeroAbs=LibertyInlineVelocity::transformVelocities(
            expressive,80,110,LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::absolute,0);
        const auto zeroRel=LibertyInlineVelocity::transformVelocities(
            expressive,80,110,LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::relative,0);
        require(zeroAbs==std::vector<int>({50,80,100,70})
            &&zeroRel==zeroAbs,
            "zero intensity never edits selected velocities in either mode");
        const auto halfAbs=LibertyInlineVelocity::transformVelocities(
            expressive,80,110,LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::absolute,50);
        const auto halfRel=LibertyInlineVelocity::transformVelocities(
            expressive,80,110,LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::relative,50);
        require(halfAbs==std::vector<int>({65,80,97,90}),
            "half intensity blends absolute target with source velocity");
        require(halfRel==std::vector<int>({58,88,114,90})
            &&halfRel[1]-halfRel[0]==30,
            "half intensity relative preserves initial chord dynamics");
        require(LibertyInlineVelocity::transformVelocities(
            expressive,80,110,LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::absolute,100)==absolute
            &&LibertyInlineVelocity::transformVelocities(
            expressive,80,110,LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::relative,100)==relative,
            "full intensity matches previous absolute and relative behavior");
        populated.setIntensity(0);
        require(populated.getIntensity()==0
            &&populated.getCurrentPreviewVelocity(0)==55
            &&populated.getCurrentPreviewVelocity(1)==91
            &&populated.getCurrentPreviewVelocity(2)==77
            &&!dialogApplied,
            "zero intensity preview shows exactly original notes");
        populated.setIntensity(50);
        require(populated.getIntensity()==50
            &&populated.getCurrentPreviewVelocity(1)==75
            &&!dialogApplied,
            "half intensity updates preview without applying changes");
        populated.restoreInitialSettings();
        require(populated.getIntensity()==100
            &&populated.getCurrentPreviewVelocity(1)==60
            &&!dialogApplied,
            "reset restores 100 percent and initial velocity curve");
        // Humanisation uses the same deterministic seed in preview and
        // Apply. A chord receives ONE offset, with saturation clamped for
        // the whole chord rather than independently on each MIDI note.
        const std::vector<LibertyInlineVelocity::PreviewNote> humanNotes={
            {0,60},{0,88},{960,70},{1920,93},{2880,57},{3840,75}};
        const auto unchanged=LibertyInlineVelocity::transformVelocities(
            humanNotes,60,100,LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::absolute,0);
        const auto humanized=LibertyInlineVelocity::humanizeVelocities(
            humanNotes,unchanged,15,0x2345abcdU);
        require(humanized.size()==humanNotes.size()
            &&humanized[1]-humanized[0]==28,
            "humanisation preserves chord velocity differences");
        for(size_t i=0;i<humanNotes.size();++i){
            require(humanized[i]>=1&&humanized[i]<=127
                &&std::abs(humanized[i]-unchanged[i])<=15,
                "humanisation stays within specified range and MIDI limits");
        }
        require(LibertyInlineVelocity::humanizeVelocities(
            humanNotes,unchanged,15,0x2345abcdU)==humanized,
            "humanization with same seed is deterministic");
        require(LibertyInlineVelocity::humanizeVelocities(
            humanNotes,unchanged,0,0x2345abcdU)==unchanged,
            "zero humanization is exactly a no-op");
        const std::vector<LibertyInlineVelocity::PreviewNote> hotChord={
            {100,115},{100,125},{200,3}};
        const auto hotBase=std::vector<int>{115,125,3};
        const auto safeResult=LibertyInlineVelocity::humanizeVelocities(
            hotChord,hotBase,20,0x91b41d24U);
        require(safeResult[1]-safeResult[0]==10
            &&safeResult[0]>=1&&safeResult[1]<=127
            &&safeResult[2]>=1&&safeResult[2]<=127,
            "humanization avoids note-by-note chord saturation");
        populated.setIntensity(0);
        populated.setHumanization(12);
        require(populated.getHumanization()==12
            &&populated.getPreviewResultVelocity(1,40,120,
                LibertyInlineVelocity::Curve::exponential,
                LibertyInlineVelocity::Mode::absolute,0,12,
                populated.getVariationSeed())==populated.getCurrentPreviewVelocity(1),
            "live humanization preview uses exact shared computation");
        const auto seedBefore=populated.getVariationSeed();
        populated.rerollVariation();
        require(populated.getVariationSeed()!=seedBefore
            &&!dialogApplied,
            "reroll makes a new deterministic preview without MIDI commit");
        populated.restoreInitialSettings();
        require(populated.getHumanization()==0
            &&populated.getVariationSeed()==seedBefore
            &&populated.getIntensity()==100
            &&!dialogApplied,
            "reset returns humanization and random seed to original values");
        // Beat accents use the project meter and the clip's absolute
        // Arrangement position. Only strong beats are changed.
        const std::vector<LibertyInlineVelocity::PreviewNote> rhythm={
            {0,100},{0,120},{960,70},{1920,90},{2880,80},{3840,75}};
        const std::vector<int> rhythmBase={100,120,70,90,80,75};
        const auto accentFour=LibertyInlineVelocity::accentVelocities(
            rhythm,rhythmBase,20,4,4,960,0);
        require(accentFour==std::vector<int>({107,127,70,100,80,95}),
            "4/4 accents strengthen bar downbeat and secondary strong beat");
        require(accentFour[1]-accentFour[0]==20,
            "accent chord saturates as a group without flattening dynamics");
        require(LibertyInlineVelocity::accentVelocities(
            rhythm,rhythmBase,0,4,4,960,0)==rhythmBase,
            "zero rhythmic accents leave MIDI exactly unchanged");
        const auto accentThree=LibertyInlineVelocity::accentVelocities(
            rhythm,rhythmBase,20,3,4,960,0);
        require(accentThree==std::vector<int>({107,127,70,90,100,75}),
            "3/4 accents apply at each measure beginning only");
        const auto sixEight=LibertyInlineVelocity::accentVelocities(
            {{0,70},{480,70},{1440,70},{2880,70}},
            {70,70,70,70},16,6,8,960,0);
        require(sixEight==std::vector<int>({86,70,78,86}),
            "6/8 secondary strong beat follows compound meter");
        const auto offsetAccent=LibertyInlineVelocity::accentVelocities(
            {{0,60},{2880,70}},{60,70},18,4,4,960,960);
        require(offsetAccent==std::vector<int>({60,88}),
            "clip offset anchors accents to project timeline");
        const auto clippedAccent=LibertyInlineVelocity::accentVelocities(
            {{0,125},{0,127},{1920,2}}, {125,127,2},
            20,4,4,960,0);
        require(clippedAccent==std::vector<int>({125,127,12}),
            "rhythmic accents preserve chord intervals at MIDI limit 127");
        using AP=LibertyInlineVelocity::AccentPattern;
        require(LibertyInlineVelocity::accentVelocities(
            rhythm,rhythmBase,20,4,4,960,0,AP::everyBeat)
                ==std::vector<int>({107,127,90,110,100,95}),
            "each beat accents all four quarter-note pulses");
        require(LibertyInlineVelocity::accentVelocities(
            rhythm,rhythmBase,20,4,4,960,0,AP::backbeat)
                ==std::vector<int>({100,120,90,90,100,75}),
            "backbeat accents beats two and four");
        require(LibertyInlineVelocity::accentVelocities(
            {{0,60},{480,60},{1440,60},{2400,60},{3360,60}},
            {60,60,60,60,60},12,4,4,960,0,AP::offbeat)
                ==std::vector<int>({60,72,72,72,72}),
            "offbeat accents the middle of each quarter note");
        require(LibertyInlineVelocity::accentVelocities(
            {{0,70},{480,70},{1440,70},{1920,70},{2880,70}},
            {70,70,70,70,70},10,6,8,960,0,AP::everyBeat)
                ==std::vector<int>({80,70,80,70,80}),
            "6/8 uses compound dotted-quarter beat groups");
        require(LibertyInlineVelocity::accentVelocities(
            {{0,70},{480,70},{960,70},{1440,70},{1920,70},{2400,70}},
            {70,70,70,70,70,70},10,6,8,960,0,AP::offbeat)
                ==std::vector<int>({70,80,70,70,80,70}),
            "6/8 accents second eighth of each ternary group");
        require(LibertyInlineVelocity::accentVelocities(
            {{0,70},{1440,70},{2880,70},{4320,70}},
            {70,70,70,70},10,12,8,960,0,AP::backbeat)
                ==std::vector<int>({70,80,70,80}),
            "12/8 accents second and fourth dotted-quarter pulses");
        // Velocity reduction never applies on a chosen accent location.
        require(LibertyInlineVelocity::accentVelocities(
            rhythm,rhythmBase,20,4,4,960,0,AP::strongBeats,10)
                ==std::vector<int>({107,127,60,100,70,95}),
            "ghost notes soften weak beats and preserve chord dynamics");
        require(LibertyInlineVelocity::accentVelocities(
            rhythm,rhythmBase,0,4,4,960,0,AP::strongBeats,10)
                ==std::vector<int>({100,120,60,90,70,75}),
            "ghost effect works independently of accent strength");
        require(LibertyInlineVelocity::accentVelocities(
            {{0,70},{480,70},{1440,70},{2880,70}},
            {70,70,70,70},16,6,8,960,0,AP::strongBeats,10)
                ==std::vector<int>({86,60,78,86}),
            "ghost timing respects the compound 6/8 meter");
        require(LibertyInlineVelocity::accentVelocities(
            {{0,70},{960,5},{960,15}},
            {70,5,15},0,4,4,960,0,AP::strongBeats,20)
                ==std::vector<int>({70,1,11}),
            "ghost clamp preserves chord intervals at MIDI minimum");
        require(LibertyInlineVelocity::accentVelocities(
            rhythm,rhythmBase,20,4,4,960,0,AP::strongBeats,0)
                ==accentFour,
            "zero ghost level restores exact original accent behavior");
        // A human-performed phrase is not quantized: accent recognition
        // is measured against absolute musical ticks, wrapping across bars.
        const std::vector<LibertyInlineVelocity::PreviewNote> looseTiming={
            {3780,60},{60,60},{1830,70},{240,80}};
        const std::vector<int> looseVelocity={60,60,70,80};
        require(LibertyInlineVelocity::accentVelocities(
            looseTiming,looseVelocity,20,4,4,960,0,AP::strongBeats,12,10)
                ==std::vector<int>({80,80,80,68}),
            "10 percent tolerance recognizes early/late downbeats, and secondary accents");
        require(LibertyInlineVelocity::accentVelocities(
            looseTiming,looseVelocity,20,4,4,960,0,AP::strongBeats,12,0)
                ==std::vector<int>({48,48,58,68}),
            "zero tolerance retains the old strict note-to-grid behavior");
        require(LibertyInlineVelocity::accentVelocities(
            {{900,75},{1040,75},{1400,75}}, {75,75,75},
            16,4,4,960,0,AP::everyBeat,5,10)
                ==std::vector<int>({91,91,70}),
            "each-beat tolerance recognizes anticipation and delay");
        require(LibertyInlineVelocity::accentVelocities(
            {{420,60},{540,60},{690,60}}, {60,60,60},
            16,4,4,960,0,AP::offbeat,5,10)
                ==std::vector<int>({76,76,55}),
            "offbeat tolerance recognizes both sides of subdivision");
        require(LibertyInlineVelocity::accentVelocities(
            {{900,60},{1020,60},{3780,60}}, {60,60,60},
            16,4,4,960,0,AP::backbeat,5,10)
                ==std::vector<int>({76,76,55}),
            "backbeat tolerance never confuses next downbeat with last backbeat");
        require(LibertyInlineVelocity::accentVelocities(
            {{2740,60},{1320,60},{420,60}}, {60,60,60},
            20,6,8,960,0,AP::strongBeats,10,10)
                ==std::vector<int>({80,70,50}),
            "6/8 timing tolerance respects bar wrap and dotted-quarter pulse");
        // Musical swing changes the target grid for simple-meter offbeats only.
        require(LibertyInlineVelocity::accentVelocities(
            {{480,60},{720,70},{1680,80},{1680,100}},
            {60,70,80,100},12,4,4,960,0,AP::offbeat,8,0,75)
                ==std::vector<int>({52,82,92,112}),
            "75 percent swing accents late offbeats and leaves chord nuances");
        require(LibertyInlineVelocity::accentVelocities(
            {{480,60},{720,70},{1680,80},{1680,100}},
            {60,70,80,100},12,4,4,960,0,AP::offbeat,8,0,50)
                ==std::vector<int>({72,62,72,92}),
            "50 percent straight groove keeps existing offbeat behavior");
        require(LibertyInlineVelocity::accentVelocities(
            {{650,70},{800,70},{480,70}}, {70,70,70},
            12,4,4,960,0,AP::offbeat,8,10,75)
                ==std::vector<int>({82,82,62}),
            "swing and tolerance recognize humanized late offbeats");
        require(LibertyInlineVelocity::accentVelocities(
            {{0,70},{480,70},{720,70}}, {70,70,70},
            16,6,8,960,0,AP::offbeat,10,0,75)
                ==std::vector<int>({60,86,60}),
            "compound 6/8 offbeats remain unchanged by swing");
        require(LibertyInlineVelocity::accentVelocities(
            {{0,70},{720,70}}, {70,70},
            10,4,4,960,720,AP::offbeat,0,0,75)
                ==std::vector<int>({80,70}),
            "swing recognition uses absolute Arrangement clip offset");
        require(LibertyInlineVelocity::accentVelocities(
            {{480,70},{720,70}}, {70,70},
            12,4,4,960,0,AP::strongBeats,8,0,75)
                ==LibertyInlineVelocity::accentVelocities(
            {{480,70},{720,70}}, {70,70},
            12,4,4,960,0,AP::strongBeats,8,0,50),
            "strong-beat profile is independent of swing control");
        // Progressive accent falloff is a strictly opt-in interpolation
        // between an exact accent and the adjacent ghost-note level.
        const auto smoothNotes=std::vector<LibertyInlineVelocity::PreviewNote>{
            {0,80},{120,80},{240,80},{241,80},{960,80}};
        const auto smoothValues=std::vector<int>{80,80,80,80,80};
        require(LibertyInlineVelocity::accentVelocities(
            smoothNotes,smoothValues,20,4,4,960,0,AP::everyBeat,
            10,25,50,true)==std::vector<int>({100,85,70,70,100}),
            "progressive accents fade naturally into ghost dynamics");
        require(LibertyInlineVelocity::accentVelocities(
            smoothNotes,smoothValues,20,4,4,960,0,AP::everyBeat,
            10,25,50,false)==std::vector<int>({100,100,100,70,100}),
            "disabled progressive mode preserves strict accent levels");
        require(LibertyInlineVelocity::accentVelocities(
            {{120,60},{120,80},{240,126}},
            {60,80,126},20,4,4,960,0,AP::everyBeat,10,25,50,true)
                ==std::vector<int>({65,85,116}),
            "progressive mode preserves chord dynamics and MIDI bounds");
        require(LibertyInlineVelocity::accentVelocities(
            {{720,70},{768,70},{816,70}}, {70,70,70},
            20,4,4,960,0,AP::offbeat,10,10,75,true)
                ==std::vector<int>({90,75,60}),
            "swing and progressive accents share accurate proximity");
        require(LibertyInlineVelocity::accentVelocities(
            {{0,70},{120,70}}, {70,70},
            20,4,4,960,0,AP::everyBeat,10,0,50,true)
                ==std::vector<int>({90,60}),
            "zero tolerance preserves original exact-grid behavior");
        using FC=LibertyInlineVelocity::Curve;
        const std::vector<LibertyInlineVelocity::PreviewNote> falloffNotes={
            {60,80},{120,80},{180,80},{240,80}};
        const std::vector<int> falloffBase={80,80,80,80};
        require(LibertyInlineVelocity::accentVelocities(
            falloffNotes,falloffBase,20,4,4,960,0,AP::everyBeat,10,
            25,50,true,FC::linear)==std::vector<int>({93,85,77,70}),
            "linear falloff exactly preserves previously validated behavior");
        require(LibertyInlineVelocity::accentVelocities(
            falloffNotes,falloffBase,20,4,4,960,0,AP::everyBeat,10,
            25,50,true,FC::exponential)==std::vector<int>({87,77,72,70}),
            "exponential falloff uses squared rhythmic proximity");
        require(LibertyInlineVelocity::accentVelocities(
            falloffNotes,falloffBase,20,4,4,960,0,AP::everyBeat,10,
            25,50,true,FC::sCurve)==std::vector<int>({95,85,75,70}),
            "S falloff smoothly preserves boundary velocities");
        require(LibertyInlineVelocity::accentVelocities(
            falloffNotes,falloffBase,20,4,4,960,0,AP::everyBeat,10,
            25,50,false,FC::sCurve)==std::vector<int>({100,100,100,100}),
            "curve setting never alters MIDI when progressive mode is off");
        populated.setIntensity(0);
        populated.setHumanization(0);
        populated.setAccents(14);
        require(populated.getAccents()==14
            &&populated.getCurrentPreviewVelocity(0)==55
            &&!dialogApplied,
            "accent preview handles offbeat notes without MIDI commit");
        populated.restoreInitialSettings();
        require(populated.getAccents()==0
            &&populated.getHumanization()==0
            &&!dialogApplied,
            "reset removes preview rhythm accents");
        LibertyInlineVelocity::DialogContent beatPreview(60,90,
            LibertyInlineVelocity::Curve::linear,
            [&](int,int,LibertyInlineVelocity::Curve,
                LibertyInlineVelocity::Mode,int,int,std::uint32_t,int,LibertyInlineVelocity::AccentPattern,int,int,int,bool,LibertyInlineVelocity::Curve){
                dialogApplied=true;
            },
            {{0,50},{960,65},{1920,70},{3840,80}},4,4,960,0);
        beatPreview.bindInputs();
        beatPreview.setIntensity(0);
        beatPreview.setAccents(20);
        require(beatPreview.getCurrentPreviewVelocity(0)==70
            &&beatPreview.getCurrentPreviewVelocity(1)==65
            &&beatPreview.getCurrentPreviewVelocity(2)==80
            &&beatPreview.getCurrentPreviewVelocity(3)==100
            &&!dialogApplied,
            "strong-beat preview exactly reflects accent computation");
        beatPreview.setAccentPattern(AP::backbeat);
        require(beatPreview.getAccentPattern()==AP::backbeat
            &&beatPreview.getCurrentPreviewVelocity(0)==50
            &&beatPreview.getCurrentPreviewVelocity(1)==85
            &&beatPreview.getCurrentPreviewVelocity(2)==70
            &&!dialogApplied,
            "backbeat preview reflects chosen style without applying");
        beatPreview.restoreInitialSettings();
        require(beatPreview.getAccentPattern()==AP::strongBeats
            &&beatPreview.getAccents()==0,
            "reset restores strong-beat profile");
        beatPreview.setIntensity(0);
        beatPreview.setGhostNotes(12);
        require(beatPreview.getGhostNotes()==12
            &&beatPreview.getCurrentPreviewVelocity(0)==50
            &&beatPreview.getCurrentPreviewVelocity(1)==53
            &&beatPreview.getCurrentPreviewVelocity(2)==70
            &&!dialogApplied,
            "ghost preview updates weaker beats without applying MIDI");
        beatPreview.restoreInitialSettings();
        require(beatPreview.getGhostNotes()==0,
            "reset clears ghost strength");
        LibertyInlineVelocity::DialogContent freeTimingPreview(60,100,
            LibertyInlineVelocity::Curve::linear,
            [&](int,int,LibertyInlineVelocity::Curve,
                LibertyInlineVelocity::Mode,int,int,std::uint32_t,int,
                LibertyInlineVelocity::AccentPattern,int,int,int,bool,LibertyInlineVelocity::Curve){
                dialogApplied=true;
            },{{50,70},{1500,80}},4,4,960,0);
        freeTimingPreview.bindInputs();
        freeTimingPreview.setIntensity(0);
        freeTimingPreview.setAccents(20);
        freeTimingPreview.setGhostNotes(8);
        require(freeTimingPreview.getCurrentPreviewVelocity(0)==62,
            "without tolerance early downbeats remain ghosted");
        freeTimingPreview.setTolerance(10);
        require(freeTimingPreview.getTolerance()==10
            &&freeTimingPreview.getCurrentPreviewVelocity(0)==90
            &&freeTimingPreview.getCurrentPreviewVelocity(1)==72
            &&!dialogApplied,
            "tolerance preview recognizes early downbeats without MIDI commit");
        freeTimingPreview.restoreInitialSettings();
        require(freeTimingPreview.getTolerance()==0
            &&freeTimingPreview.getGhostNotes()==0,
            "reset restores strict musical grid");
        LibertyInlineVelocity::DialogContent swingPreview(60,100,
            LibertyInlineVelocity::Curve::linear,
            [&](int,int,LibertyInlineVelocity::Curve,
                LibertyInlineVelocity::Mode,int,int,std::uint32_t,int,
                LibertyInlineVelocity::AccentPattern,int,int,int,bool,LibertyInlineVelocity::Curve){
                dialogApplied=true;
            },{{480,60},{720,70}},4,4,960,0);
        swingPreview.bindInputs();
        swingPreview.setIntensity(0);
        swingPreview.setAccents(14);
        swingPreview.setGhostNotes(8);
        require(!swingPreview.isSwingEnabled(),"swing disabled for strong beats");
        swingPreview.setAccentPattern(AP::offbeat);
        require(swingPreview.isSwingEnabled()
            &&swingPreview.getCurrentPreviewVelocity(0)==74
            &&swingPreview.getCurrentPreviewVelocity(1)==62,
            "straight offbeats drive default preview");
        swingPreview.setSwing(75);
        require(swingPreview.getSwing()==75
            &&swingPreview.getCurrentPreviewVelocity(0)==52
            &&swingPreview.getCurrentPreviewVelocity(1)==84
            &&!dialogApplied,
            "swung accents update preview but do not edit MIDI");
        swingPreview.restoreInitialSettings();
        require(swingPreview.getSwing()==50
            &&swingPreview.getAccentPattern()==AP::strongBeats
            &&!swingPreview.isSwingEnabled(),
            "reset restores straight rhythm and disables inactive swing");
        LibertyInlineVelocity::DialogContent softPreview(40,110,
            LibertyInlineVelocity::Curve::linear,
            [&](int,int,LibertyInlineVelocity::Curve,
                LibertyInlineVelocity::Mode,int,int,std::uint32_t,int,
                LibertyInlineVelocity::AccentPattern,int,int,int,bool,LibertyInlineVelocity::Curve){
                dialogApplied=true;
            },{{0,70},{120,70},{240,70}},4,4,960,0);
        softPreview.bindInputs();
        softPreview.setIntensity(0);
        softPreview.setAccentPattern(AP::everyBeat);
        softPreview.setAccents(20);
        softPreview.setGhostNotes(10);
        require(!softPreview.areProgressiveAccentsAvailable(),
            "progressive mode remains disabled when no tolerance is set");
        softPreview.setTolerance(25);
        require(softPreview.areProgressiveAccentsAvailable()
            &&softPreview.getCurrentPreviewVelocity(1)==90,
            "default discrete accents preserved in the live preview");
        softPreview.setProgressiveAccents(true);
        require(softPreview.getProgressiveAccents()
            &&softPreview.getCurrentPreviewVelocity(0)==90
            &&softPreview.getCurrentPreviewVelocity(1)==75
            &&softPreview.getCurrentPreviewVelocity(2)==60
            &&!dialogApplied,
            "live preview shows graduated accents without editing MIDI");
        softPreview.restoreInitialSettings();
        require(!softPreview.getProgressiveAccents()
            &&!softPreview.areProgressiveAccentsAvailable()
            &&softPreview.getTolerance()==0
            &&!dialogApplied,
            "reset returns to the validated rhythm settings");
        softPreview.setTolerance(25);
        softPreview.setProgressiveAccents(true);
        require(softPreview.isAccentFalloffEnabled()
            &&softPreview.getAccentFalloff()==FC::linear,
            "validated linear falloff remains the initial setting");
        softPreview.setAccentFalloff(FC::exponential);
        require(softPreview.getCurrentPreviewVelocity(1)==67
            &&!dialogApplied,
            "exponential falloff updates preview without MIDI mutation");
        softPreview.setAccentFalloff(FC::sCurve);
        require(softPreview.getCurrentPreviewVelocity(1)==75
            &&!dialogApplied,
            "S falloff updates the non-destructive preview");
        softPreview.restoreInitialSettings();
        require(softPreview.getAccentFalloff()==FC::linear
            &&!softPreview.isAccentFalloffEnabled(),
            "reset restores linear curve and hides inactive accent control");




        // Restore the three-note fixture for subsequent same-chord checks.
        midiClips[(size_t)multiClip].notes=baseline;
        require(owner.midiEngine.replaceNotes(midiClips[(size_t)multiClip].notes),
            "restore chord fixture after custom curve");
        midiClips[(size_t)multiClip].notes=owner.midiEngine.getNotesCopy();
        syncPlayback();clearArrangementHistory();
        // Relative mode applied to actual notes, preserving the difference
        // between simultaneous notes and leaving unselected MIDI untouched.
        inlineMidiSelectedClipId=midiClips[(size_t)multiClip].id;
        inlineMidiSelectedInstrument=false;inlineMidiSelectedNotes={0,1,2};
        const auto beforeRelative=midiClips[(size_t)multiClip].notes;
        const int oldChordDifference=(int)beforeRelative[0].velocity
                                    -(int)beforeRelative[1].velocity;
        require(inlineMidiRampVelocitySelected(80,105,
            LibertyInlineVelocity::Curve::sCurve,
            LibertyInlineVelocity::Mode::relative),
            "relative MIDI velocity command applied");
        const auto afterRelative=midiClips[(size_t)multiClip].notes;
        require((int)afterRelative[0].velocity-(int)afterRelative[1].velocity
                    ==oldChordDifference,
            "relative MIDI mode preserves chord velocity differences");
        for(size_t i=0;i<beforeRelative.size();++i){
            const auto& a=beforeRelative[i];
            const auto& b=afterRelative[i];
            require(a.startTick==b.startTick && a.lengthTicks==b.lengthTicks
                &&a.pitch==b.pitch && a.channel==b.channel
                &&a.muted==b.muted && a.probability==b.probability,
                "relative curve preserves all other MIDI properties");
        }
        undoArrangement(false);
        for(size_t i=0;i<beforeRelative.size();++i)
            require(midiClips[(size_t)multiClip].notes[i].velocity
                ==beforeRelative[i].velocity,"relative MIDI curve Undo");
        undoArrangement(true);
        for(size_t i=0;i<afterRelative.size();++i)
            require(midiClips[(size_t)multiClip].notes[i].velocity
                ==afterRelative[i].velocity,"relative MIDI curve Redo");
        // Apply partial intensity to the actual clip. The preview and edit
        // share a pure transformation, with independent Undo and Redo.
        inlineMidiSelectedClipId=midiClips[(size_t)multiClip].id;
        inlineMidiSelectedInstrument=false;inlineMidiSelectedNotes={0,1,2};
        const auto intensityBefore=midiClips[(size_t)multiClip].notes;
        const auto intensityExpected=LibertyInlineVelocity::transformVelocities(
            {{intensityBefore[0].startTick,(int)intensityBefore[0].velocity},
             {intensityBefore[1].startTick,(int)intensityBefore[1].velocity},
             {intensityBefore[2].startTick,(int)intensityBefore[2].velocity}},
            40,120,LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::absolute,50);
        require(inlineMidiRampVelocitySelected(40,120,
            LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::absolute,50),
            "partial MIDI velocity intensity edit");
        for(size_t i=0;i<intensityExpected.size();++i)
            require(midiClips[(size_t)multiClip].notes[i].velocity
                ==intensityExpected[i],
                "partial velocity MIDI result matches live preview");
        undoArrangement(false);
        for(size_t i=0;i<intensityBefore.size();++i)
            require(midiClips[(size_t)multiClip].notes[i].velocity
                ==intensityBefore[i].velocity,"partial MIDI intensity Undo");
        undoArrangement(true);
        for(size_t i=0;i<intensityExpected.size();++i)
            require(midiClips[(size_t)multiClip].notes[i].velocity
                ==intensityExpected[i],"partial MIDI intensity Redo");
        // Test actual MIDI clip humanisation, matching the preview, and
        // verify undo/redo retains all non-velocity MIDI attributes.
        inlineMidiSelectedClipId=midiClips[(size_t)multiClip].id;
        inlineMidiSelectedInstrument=false;inlineMidiSelectedNotes={0,1,2};
        const auto humanBefore=midiClips[(size_t)multiClip].notes;
        const std::vector<LibertyInlineVelocity::PreviewNote> sourceHuman={
            {humanBefore[0].startTick,(int)humanBefore[0].velocity},
            {humanBefore[1].startTick,(int)humanBefore[1].velocity},
            {humanBefore[2].startTick,(int)humanBefore[2].velocity}};
        const auto baseHuman=LibertyInlineVelocity::transformVelocities(
            sourceHuman,50,105,LibertyInlineVelocity::Curve::sCurve,
            LibertyInlineVelocity::Mode::relative,60);
        const auto expectedHuman=LibertyInlineVelocity::humanizeVelocities(
            sourceHuman,baseHuman,13,0x2345abcdU);
        require(inlineMidiRampVelocitySelected(50,105,
            LibertyInlineVelocity::Curve::sCurve,
            LibertyInlineVelocity::Mode::relative,60,13,0x2345abcdU),
            "humanized MIDI velocities apply to selected clip");
        const auto humanAfter=midiClips[(size_t)multiClip].notes;
        for(size_t i=0;i<sourceHuman.size();++i){
            require(humanAfter[i].velocity==expectedHuman[i]
                &&humanAfter[i].startTick==humanBefore[i].startTick
                &&humanAfter[i].lengthTicks==humanBefore[i].lengthTicks
                &&humanAfter[i].pitch==humanBefore[i].pitch
                &&humanAfter[i].channel==humanBefore[i].channel
                &&humanAfter[i].muted==humanBefore[i].muted,
                "humanized clip matches preview and retains MIDI structure");
        }
        undoArrangement(false);
        for(size_t i=0;i<humanBefore.size();++i)
            require(midiClips[(size_t)multiClip].notes[i].velocity
                ==humanBefore[i].velocity,"humanized MIDI notes Undo");
        undoArrangement(true);
        for(size_t i=0;i<humanAfter.size();++i)
            require(midiClips[(size_t)multiClip].notes[i].velocity
                ==humanAfter[i].velocity,"humanized MIDI notes Redo");
        // Apply rhythmic accents with exactly the same musical meter,
        // velocity transform and clip offset as the dialog preview.
        inlineMidiSelectedClipId=midiClips[(size_t)multiClip].id;
        inlineMidiSelectedInstrument=false;inlineMidiSelectedNotes={0,1,2};
        const auto beatBefore=midiClips[(size_t)multiClip].notes;
        const std::vector<LibertyInlineVelocity::PreviewNote> beatNotes={
            {beatBefore[0].startTick,(int)beatBefore[0].velocity},
            {beatBefore[1].startTick,(int)beatBefore[1].velocity},
            {beatBefore[2].startTick,(int)beatBefore[2].velocity}};
        const auto beatCurve=LibertyInlineVelocity::transformVelocities(
            beatNotes,45,110,LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::relative,50);
        const auto beatHumanized=LibertyInlineVelocity::humanizeVelocities(
            beatNotes,beatCurve,0,0x61b3e729U);
        const auto beatExpected=LibertyInlineVelocity::accentVelocities(
            beatNotes,beatHumanized,18,owner.timeSignatureNumerator,
            owner.timeSignatureDenominator,MidiEngine::ticksPerQuarterNote,
            MidiEngine::secondsToTick(
                midiClips[(size_t)multiClip].startSeconds,owner.tempoBpm));
        require(inlineMidiRampVelocitySelected(45,110,
            LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::relative,50,0,0x61b3e729U,18),
            "rhythm accent edit applies within Arrangement");
        const auto beatAfter=midiClips[(size_t)multiClip].notes;
        for(size_t i=0;i<beatNotes.size();++i){
            require(beatAfter[i].velocity==beatExpected[i]
                &&beatAfter[i].startTick==beatBefore[i].startTick
                &&beatAfter[i].lengthTicks==beatBefore[i].lengthTicks
                &&beatAfter[i].pitch==beatBefore[i].pitch
                &&beatAfter[i].channel==beatBefore[i].channel
                &&beatAfter[i].muted==beatBefore[i].muted,
                "accent result matches exact preview without structural changes");
        }
        undoArrangement(false);
        for(size_t i=0;i<beatBefore.size();++i)
            require(midiClips[(size_t)multiClip].notes[i].velocity
                ==beatBefore[i].velocity,"accent MIDI velocity Undo");
        undoArrangement(true);
        for(size_t i=0;i<beatAfter.size();++i)
            require(midiClips[(size_t)multiClip].notes[i].velocity
                ==beatAfter[i].velocity,"accent MIDI velocity Redo");
        // New accent profiles share Arrangement Undo/Redo and note safety.
        inlineMidiSelectedClipId=midiClips[(size_t)multiClip].id;
        inlineMidiSelectedInstrument=false;inlineMidiSelectedNotes={0,1,2};
        const auto profileBefore=midiClips[(size_t)multiClip].notes;
        const std::vector<LibertyInlineVelocity::PreviewNote> profileNotes={
            {profileBefore[0].startTick,(int)profileBefore[0].velocity},
            {profileBefore[1].startTick,(int)profileBefore[1].velocity},
            {profileBefore[2].startTick,(int)profileBefore[2].velocity}};
        const auto profileCurve=LibertyInlineVelocity::transformVelocities(
            profileNotes,45,110,LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::relative,50);
        const auto profileExpected=LibertyInlineVelocity::accentVelocities(
            profileNotes,profileCurve,19,owner.timeSignatureNumerator,
            owner.timeSignatureDenominator,MidiEngine::ticksPerQuarterNote,
            MidiEngine::secondsToTick(
                midiClips[(size_t)multiClip].startSeconds,owner.tempoBpm),
            AP::everyBeat);
        require(inlineMidiRampVelocitySelected(45,110,
            LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::relative,50,0,0x61b3e729U,19,
            AP::everyBeat),"new accent profile applies in Arrangement");
        const auto profileAfter=midiClips[(size_t)multiClip].notes;
        for(size_t i=0;i<profileNotes.size();++i)
            require((int)profileAfter[i].velocity==profileExpected[i]
                &&profileAfter[i].startTick==profileBefore[i].startTick
                &&profileAfter[i].lengthTicks==profileBefore[i].lengthTicks
                &&profileAfter[i].pitch==profileBefore[i].pitch,
                "new accents match preview and preserve MIDI properties");
        undoArrangement(false);
        for(size_t i=0;i<profileNotes.size();++i)
            require(profileBefore[i].velocity==
                midiClips[(size_t)multiClip].notes[i].velocity,
                "new accents Undo");
        undoArrangement(true);
        for(size_t i=0;i<profileNotes.size();++i)
            require(profileAfter[i].velocity==
                midiClips[(size_t)multiClip].notes[i].velocity,
                "new accents Redo");
        // Apply ghost-only adjustment with the real Arrangement transaction.
        inlineMidiSelectedClipId=midiClips[(size_t)multiClip].id;
        inlineMidiSelectedInstrument=false;inlineMidiSelectedNotes={0,1,2};
        const auto ghostBefore=midiClips[(size_t)multiClip].notes;
        const std::vector<LibertyInlineVelocity::PreviewNote> ghostNotes={
            {ghostBefore[0].startTick,(int)ghostBefore[0].velocity},
            {ghostBefore[1].startTick,(int)ghostBefore[1].velocity},
            {ghostBefore[2].startTick,(int)ghostBefore[2].velocity}};
        const auto ghostBase=LibertyInlineVelocity::transformVelocities(
            ghostNotes,40,100,LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::relative,0);
        const auto ghostExpected=LibertyInlineVelocity::accentVelocities(
            ghostNotes,ghostBase,0,owner.timeSignatureNumerator,
            owner.timeSignatureDenominator,MidiEngine::ticksPerQuarterNote,
            MidiEngine::secondsToTick(
                midiClips[(size_t)multiClip].startSeconds,owner.tempoBpm),
            AP::backbeat,13);
        require(ghostExpected!=std::vector<int>(
                {ghostBefore[0].velocity,ghostBefore[1].velocity,
                 ghostBefore[2].velocity}),
            "ghost edit fixture has non-accented notes to change");
        require(inlineMidiRampVelocitySelected(40,100,
            LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::relative,0,0,0x61b3e729U,0,
            AP::backbeat,13),"ghost dynamics apply within Arrangement");
        const auto ghostAfter=midiClips[(size_t)multiClip].notes;
        for(size_t i=0;i<ghostNotes.size();++i)
            require((int)ghostAfter[i].velocity==ghostExpected[i]
                &&ghostAfter[i].startTick==ghostBefore[i].startTick
                &&ghostAfter[i].lengthTicks==ghostBefore[i].lengthTicks
                &&ghostAfter[i].pitch==ghostBefore[i].pitch,
                "ghost result matches preview and preserves MIDI fields");
        undoArrangement(false);
        for(size_t i=0;i<ghostNotes.size();++i)
            require(ghostBefore[i].velocity==
                midiClips[(size_t)multiClip].notes[i].velocity,
                "ghost edit Undo");
        undoArrangement(true);
        for(size_t i=0;i<ghostNotes.size();++i)
            require(ghostAfter[i].velocity==
                midiClips[(size_t)multiClip].notes[i].velocity,
                "ghost edit Redo");
        // Preview and Arrangement share the tolerance-aware calculation.
        inlineMidiSelectedClipId=midiClips[(size_t)multiClip].id;
        inlineMidiSelectedInstrument=false;inlineMidiSelectedNotes={0,1,2};
        const auto toleranceBefore=midiClips[(size_t)multiClip].notes;
        const std::vector<LibertyInlineVelocity::PreviewNote> toleranceNotes={
            {toleranceBefore[0].startTick,(int)toleranceBefore[0].velocity},
            {toleranceBefore[1].startTick,(int)toleranceBefore[1].velocity},
            {toleranceBefore[2].startTick,(int)toleranceBefore[2].velocity}};
        const auto toleranceCurve=LibertyInlineVelocity::transformVelocities(
            toleranceNotes,45,95,LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::relative,0);
        const auto toleranceExpected=LibertyInlineVelocity::accentVelocities(
            toleranceNotes,toleranceCurve,12,owner.timeSignatureNumerator,
            owner.timeSignatureDenominator,MidiEngine::ticksPerQuarterNote,
            MidiEngine::secondsToTick(
                midiClips[(size_t)multiClip].startSeconds,owner.tempoBpm),
            AP::strongBeats,8,15);
        require(inlineMidiRampVelocitySelected(45,95,
            LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::relative,0,0,0x61b3e729U,12,
            AP::strongBeats,8,15),"tolerance accents apply to the Arrangement");
        const auto toleranceAfter=midiClips[(size_t)multiClip].notes;
        for(size_t i=0;i<toleranceNotes.size();++i)
            require((int)toleranceAfter[i].velocity==toleranceExpected[i]
                &&toleranceAfter[i].startTick==toleranceBefore[i].startTick
                &&toleranceAfter[i].lengthTicks==toleranceBefore[i].lengthTicks
                &&toleranceAfter[i].pitch==toleranceBefore[i].pitch
                &&toleranceAfter[i].channel==toleranceBefore[i].channel,
                "tolerance changes velocities only, exactly as in preview");
        undoArrangement(false);
        for(size_t i=0;i<toleranceNotes.size();++i)
            require(toleranceBefore[i].velocity==
                midiClips[(size_t)multiClip].notes[i].velocity,
                "tolerance edit Undo");
        undoArrangement(true);
        for(size_t i=0;i<toleranceNotes.size();++i)
            require(toleranceAfter[i].velocity==
                midiClips[(size_t)multiClip].notes[i].velocity,
                "tolerance edit Redo");
        // End-to-end swing preview/Apply parity with complete MIDI Undo/Redo.
        inlineMidiSelectedClipId=midiClips[(size_t)multiClip].id;
        inlineMidiSelectedInstrument=false;inlineMidiSelectedNotes={0,1,2};
        const auto swingBefore=midiClips[(size_t)multiClip].notes;
        const std::vector<LibertyInlineVelocity::PreviewNote> swingNotes={
            {swingBefore[0].startTick,(int)swingBefore[0].velocity},
            {swingBefore[1].startTick,(int)swingBefore[1].velocity},
            {swingBefore[2].startTick,(int)swingBefore[2].velocity}};
        const auto swingBase=LibertyInlineVelocity::transformVelocities(
            swingNotes,40,100,LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::relative,0);
        const auto swingExpected=LibertyInlineVelocity::accentVelocities(
            swingNotes,swingBase,12,owner.timeSignatureNumerator,
            owner.timeSignatureDenominator,MidiEngine::ticksPerQuarterNote,
            MidiEngine::secondsToTick(
                midiClips[(size_t)multiClip].startSeconds,owner.tempoBpm),
            AP::offbeat,9,15,75);
        require(inlineMidiRampVelocitySelected(40,100,
            LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::relative,0,0,0x61b3e729U,12,
            AP::offbeat,9,15,75),"swung MIDI accents apply in Arrangement");
        const auto swingAfter=midiClips[(size_t)multiClip].notes;
        for(size_t i=0;i<swingNotes.size();++i)
            require((int)swingAfter[i].velocity==swingExpected[i]
                &&swingAfter[i].startTick==swingBefore[i].startTick
                &&swingAfter[i].lengthTicks==swingBefore[i].lengthTicks
                &&swingAfter[i].pitch==swingBefore[i].pitch
                &&swingAfter[i].channel==swingBefore[i].channel,
                "swing edits velocity only and matches full preview");
        undoArrangement(false);
        for(size_t i=0;i<swingNotes.size();++i)
            require(swingBefore[i].velocity==
                midiClips[(size_t)multiClip].notes[i].velocity,
                "swing edit Undo");
        undoArrangement(true);
        for(size_t i=0;i<swingNotes.size();++i)
            require(swingAfter[i].velocity==
                midiClips[(size_t)multiClip].notes[i].velocity,
                "swing edit Redo");
        // Real MIDI edit with progressive accents and complete Undo/Redo.
        inlineMidiSelectedClipId=midiClips[(size_t)multiClip].id;
        inlineMidiSelectedInstrument=false;inlineMidiSelectedNotes={0,1,2};
        const auto softBefore=midiClips[(size_t)multiClip].notes;
        const std::vector<LibertyInlineVelocity::PreviewNote> softNotes={
            {softBefore[0].startTick,(int)softBefore[0].velocity},
            {softBefore[1].startTick,(int)softBefore[1].velocity},
            {softBefore[2].startTick,(int)softBefore[2].velocity}};
        const auto softBase=LibertyInlineVelocity::transformVelocities(
            softNotes,40,100,LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::relative,0);
        const auto softExpected=LibertyInlineVelocity::accentVelocities(
            softNotes,softBase,17,owner.timeSignatureNumerator,
            owner.timeSignatureDenominator,MidiEngine::ticksPerQuarterNote,
            MidiEngine::secondsToTick(
                midiClips[(size_t)multiClip].startSeconds,owner.tempoBpm),
            AP::everyBeat,9,25,50,true);
        require(inlineMidiRampVelocitySelected(40,100,
            LibertyInlineVelocity::Curve::linear,
            LibertyInlineVelocity::Mode::relative,0,0,0x61b3e729U,17,
            AP::everyBeat,9,25,50,true),
            "progressive rhythmic accents apply inside Arrangement");
        const auto softAfter=midiClips[(size_t)multiClip].notes;
        for(size_t i=0;i<softNotes.size();++i)
            require((int)softAfter[i].velocity==softExpected[i]
                &&softAfter[i].startTick==softBefore[i].startTick
                &&softAfter[i].lengthTicks==softBefore[i].lengthTicks
                &&softAfter[i].pitch==softBefore[i].pitch
                &&softAfter[i].channel==softBefore[i].channel,
                "progressive edit matches preview and preserves MIDI fields");
        undoArrangement(false);
        for(size_t i=0;i<softNotes.size();++i)
            require(softBefore[i].velocity==
                midiClips[(size_t)multiClip].notes[i].velocity,
                "progressive groove Undo");
        undoArrangement(true);
        for(size_t i=0;i<softNotes.size();++i)
            require(softAfter[i].velocity==
                midiClips[(size_t)multiClip].notes[i].velocity,
                "progressive groove Redo");
        // Real Arrangement transaction, with a non-default S-shaped accent.
        inlineMidiSelectedClipId=midiClips[(size_t)multiClip].id;
        inlineMidiSelectedInstrument=false;inlineMidiSelectedNotes={0,1,2};
        const auto shapedBefore=midiClips[(size_t)multiClip].notes;
        const std::vector<LibertyInlineVelocity::PreviewNote> shapedNotes={
            {shapedBefore[0].startTick,(int)shapedBefore[0].velocity},
            {shapedBefore[1].startTick,(int)shapedBefore[1].velocity},
            {shapedBefore[2].startTick,(int)shapedBefore[2].velocity}};
        const auto shapedBase=LibertyInlineVelocity::transformVelocities(
            shapedNotes,40,100,FC::linear,LibertyInlineVelocity::Mode::relative,0);
        const auto shapedExpected=LibertyInlineVelocity::accentVelocities(
            shapedNotes,shapedBase,17,owner.timeSignatureNumerator,
            owner.timeSignatureDenominator,MidiEngine::ticksPerQuarterNote,
            MidiEngine::secondsToTick(
                midiClips[(size_t)multiClip].startSeconds,owner.tempoBpm),
            AP::everyBeat,9,25,50,true,FC::sCurve);
        require(inlineMidiRampVelocitySelected(40,100,FC::linear,
            LibertyInlineVelocity::Mode::relative,0,0,0x61b3e729U,17,
            AP::everyBeat,9,25,50,true,FC::sCurve),
            "S-shaped MIDI accent edits apply in Arrangement");
        const auto shapedAfter=midiClips[(size_t)multiClip].notes;
        for(size_t i=0;i<shapedNotes.size();++i)
            require((int)shapedAfter[i].velocity==shapedExpected[i]
                &&shapedAfter[i].startTick==shapedBefore[i].startTick
                &&shapedAfter[i].lengthTicks==shapedBefore[i].lengthTicks
                &&shapedAfter[i].pitch==shapedBefore[i].pitch
                &&shapedAfter[i].channel==shapedBefore[i].channel,
                "S falloff preserves MIDI structure and preview parity");
        undoArrangement(false);
        for(size_t i=0;i<shapedNotes.size();++i)
            require(shapedBefore[i].velocity==
                midiClips[(size_t)multiClip].notes[i].velocity,
                "S-shaped accent Undo");
        undoArrangement(true);
        for(size_t i=0;i<shapedNotes.size();++i)
            require(shapedAfter[i].velocity==
                midiClips[(size_t)multiClip].notes[i].velocity,
                "S-shaped accent Redo");
        // A selection confined to one chord cannot define a time ramp.
        inlineMidiSelectedClipId=midiClips[(size_t)multiClip].id;
        inlineMidiSelectedInstrument=false;
        inlineMidiSelectedNotes={0,1};
        const auto sameTickBefore=midiClips[(size_t)multiClip].notes;
        require(!inlineMidiRampVelocitySelected(40,120),
            "same-time chord rejects undefined velocity ramp");
        const auto sameTickAfter=midiClips[(size_t)multiClip].notes;
        require(sameTickBefore[0].velocity==sameTickAfter[0].velocity
            && sameTickBefore[1].velocity==sameTickAfter[1].velocity,
            "rejected chord ramp does not change MIDI notes");
        inlineMidiVelocityMode=false;inlineMidiMode=false;inlineMidiClearSelection();
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
