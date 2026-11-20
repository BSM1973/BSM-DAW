LibertyClipNames clipNames;
bool renameDialogOpen=false;
juce::uint32 lastNameDoubleClick=0;
juce::String lastNameKey;
struct NamedClip {juce::String key,lane;int logical=0;juce::Rectangle<int> bounds;};
std::vector<NamedClip> namedClips()const{
    std::vector<NamedClip> result;
    for(int t=0;t<owner.getAudioTrackCount();++t)for(const auto& c:owner.audioEngine.getArrangementAudioClips(t))
        result.push_back({"audio:"+c.id,"audio:"+juce::String(t),t,audioBounds(t,c).withWidth(juce::jmax(1,(int)std::round(c.lengthSeconds*getLibertyTimelinePixelsPerSecond())))});
    auto add=[&](const auto& clips,bool instrument){for(const auto& c:clips){int x=0,w=0;if(!clipPixelBounds(c,x,w))continue;w=juce::jmin(w,juce::jmax(1,(int)std::round(c.lengthSeconds*getLibertyTimelinePixelsPerSecond())));
        const int row=owner.getAudioTrackCount()+(instrument?owner.getMidiTrackCount():0)+c.trackLane;
        result.push_back({"midi:"+juce::String(c.id),(instrument?"instrument:":"midi:")+juce::String(c.trackLane),row,{x,owner.getArrangeTrackTop(row)+4,w,owner.getArrangeTrackHeight(row)-8}});
    }};add(midiClips,false);add(instrumentClips,true);
    for(int t=0;t<(int)owner.instrumentStepSequencers.size();++t)for(const auto& c:owner.instrumentStepSequencers[(size_t)t].timelineClips){
        Clip proxy;proxy.startSeconds=c.startSeconds;proxy.lengthSeconds=c.lengthSeconds;int x=0,w=0;if(!clipPixelBounds(proxy,x,w))continue;w=juce::jmin(w,juce::jmax(1,(int)std::round(c.lengthSeconds*getLibertyTimelinePixelsPerSecond())));
        const int row=owner.getAudioTrackCount()+owner.getMidiTrackCount()+t;
        result.push_back({"pattern:"+c.namingId,"instrument:"+juce::String(t),row,{x,owner.getArrangeTrackTop(row)+5,w,owner.getArrangeTrackHeight(row)-10}});
    }
    return result;
}
void syncClipNames(){std::set<juce::String> seen;for(auto& bank:owner.instrumentStepSequencers)for(auto& c:bank.timelineClips){if(c.namingId.isEmpty()||!seen.insert(c.namingId).second){c.namingId=juce::Uuid().toString();seen.insert(c.namingId);}}for(const auto& c:namedClips())clipNames.ensure(c.key,c.lane);}
juce::String namingSignature(){syncClipNames();juce::String result;for(const auto& c:namedClips()){
    const auto& entry=clipNames.entries[c.key];result<<c.key<<":"<<entry.manual.length()<<":"<<entry.manual<<";";
}return result;}
juce::String trackClipName(int row)const {auto name=getLibertyTrackName(row);return name.isNotEmpty()?name:owner.getTrackDisplayName(row);}
juce::Rectangle<int> nameBounds(const NamedClip& c)const{
    if(c.bounds.getWidth()<=4||c.bounds.getHeight()<=4)return {};
    return c.bounds.reduced(2).withHeight(juce::jmin(20,juce::jmax(0,c.bounds.getHeight()-4)));
}
juce::String nameKeyAt(juce::Point<int> p)const{
    if(p.x<owner.trackHeaderWidth||p.y<owner.getArrangeTop()||p.y>=owner.getMixerTop())return {};
    const auto clips=namedClips();for(auto it=clips.rbegin();it!=clips.rend();++it)if(nameBounds(*it).contains(p))return it->key;
    return {};
}
void paintClipNames(juce::Graphics& g){
    syncClipNames();juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(owner.trackHeaderWidth,owner.getArrangeTop(),owner.getWidth()-owner.trackHeaderWidth,owner.getMixerTop()-owner.getArrangeTop());
    for(const auto& c:namedClips()){
        const auto box=nameBounds(c);g.setColour(juce::Colour(0xff101f30).withAlpha(.96f));g.fillRoundedRectangle(box.toFloat(),3);
        g.setColour(juce::Colour(0xffedf7ff));g.setFont(juce::Font(11.5f,juce::Font::bold));
        g.drawText(clipNames.display(c.key,trackClipName(c.logical)),box.reduced(4,0),juce::Justification::centredLeft,true);
    }
}
bool renameClipAt(juce::Point<int> p){
    const auto key=nameKeyAt(p);if(key.isEmpty())return false;syncClipNames();
    const auto now=juce::Time::getMillisecondCounter();
    if(renameDialogOpen||(key==lastNameKey&&now-lastNameDoubleClick<500))return true;
    lastNameDoubleClick=now;lastNameKey=key;renameDialogOpen=true;
    finishArrangementEdit();
    juce::String current;for(const auto& c:namedClips())if(c.key==key)current=clipNames.display(key,trackClipName(c.logical));
    auto* dialog=new juce::AlertWindow(juce::String::fromUTF8("Renommer le clip"),juce::String::fromUTF8("Ce nom restera indépendant du nom de la piste."),juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("name",current,juce::String::fromUTF8("Nom du clip :"));
    dialog->addButton(juce::String::fromUTF8("Annuler"),0,juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->addButton(juce::String::fromUTF8("Renommer"),1,juce::KeyPress(juce::KeyPress::returnKey));
    juce::Component::SafePointer<MultiMidiClipController> safe(this);
    dialog->enterModalState(true,juce::ModalCallbackFunction::create([safe,key,dialog](int result){
        if(!safe)return;safe->renameDialogOpen=false;if(result!=1)return;
        const auto clips=safe->namedClips();if(std::none_of(clips.begin(),clips.end(),[&](const auto& c){return c.key==key;}))return;
        safe->beginArrangementEdit();safe->clipNames.rename(key,dialog->getTextEditorContents("name"));safe->finishArrangementEdit();safe->repaint();safe->owner.repaint();
    }),true);return true;
}
