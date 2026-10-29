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
        // MIDI time grid stays visible over a coloured clip and aligns with
        // the global musical ruler, not the beginning of the MIDI region.
        {
            juce::Image gridCheck(juce::Image::ARGB,600,120,true);
            juce::Graphics gridGraphics(gridCheck);
            gridGraphics.setColour(juce::Colour(0xff244d72));
            gridGraphics.fillAll();
            const double pxNow=getLibertyTimelinePixelsPerSecond();
            const double barSeconds=secondsPerMeasure();
            const int globalBar=(int)std::lround(owner.trackHeaderWidth+barSeconds*pxNow);
            const int regionX=juce::jmax(0,globalBar-45);
            // Render into an image with independent local coordinates while
            // keeping absolute-timeline mapping via a translated Graphics context.
            gridGraphics.addTransform(juce::AffineTransform::translation((float)-regionX,0.0f));
            drawMidiClipTimeGrid(gridGraphics,{regionX,10,90,80},true);
            const int x=globalBar-regionX;
            if(x>=1&&x<599){
                const auto on=gridCheck.getPixelAt(x,47);
                const auto off=gridCheck.getPixelAt(x-1,47);
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
