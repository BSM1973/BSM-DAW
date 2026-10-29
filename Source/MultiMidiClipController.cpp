#define private public
#include "MainComponent.h"
#undef private
#include "MidiEditor.h"
#include "MidiClipEditing.h"
#include "AudioCrossfadeCurve.h"
#include "ClipNames.h"
#include "LibertyDesignSystem.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <map>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>
void setLibertyTrackName(int,const juce::String&);
int getLibertyTrackColourId(int);juce::String getLibertyTrackName(int);bool isLibertySnapEnabled()noexcept;double getLibertySnapSeconds(double)noexcept;double getLibertyTimelinePixelsPerSecond()noexcept;int getLibertyTrackRowHeight()noexcept;
namespace{constexpr int rulerHeight=32;constexpr float resizeZone=12;struct Clip{int id=0;int trackLane=0;double startSeconds=0,lengthSeconds=2;bool userLength=false,muted=false;std::vector<MidiEngine::NoteEvent>notes;};juce::Colour trackColour(int id){static constexpr std::array<juce::uint32,9>p{0xff31506a,0xff3b82f6,0xff22c55e,0xffeab308,0xfff97316,0xffef4444,0xffa855f7,0xffec4899,0xff14b8a6};return juce::Colour(p[(size_t)juce::jlimit(0,8,id)]);}class MultiMidiClipController;MultiMidiClipController*activeController=nullptr;
class MultiMidiClipController final:public juce::Component,private juce::Timer{public:explicit MultiMidiClipController(MainComponent&o):owner(o){setInterceptsMouseClicks(true,false);setWantsKeyboardFocus(true);owner.addAndMakeVisible(this);setBounds(owner.getLocalBounds());owner.midiClipOverlay.setVisible(false);juce::Desktop::getInstance().addGlobalMouseListener(this);toFront(false);migrateLegacyClip();activeController=this;startTimerHz(30);}~MultiMidiClipController()override{shutdown();}void shutdown(){if(stopped.exchange(true))return;juce::Desktop::getInstance().removeGlobalMouseListener(this);stopTimer();if(activeController==this)activeController=nullptr;setVisible(false);}bool owns(const MainComponent&c)const noexcept{return &owner==&c;}void prepareExternalGeneration(){syncActiveClip();}
juce::String stateSignature() {
    juce::String state;
    auto append=[&](const std::vector<Clip>& clips,const char* kind) {
        state << kind;
        for(const auto& clip:clips) {
            state << "|" << clip.id << "," << clip.trackLane << ","
                  << juce::String(clip.startSeconds,9) << "," << juce::String(clip.lengthSeconds,9)
                  << "," << (int)clip.userLength << "," << (int)clip.muted;
            for(const auto& n:clip.notes)state<<";"<<n.startTick<<","<<n.lengthTicks<<","<<(int)n.pitch<<","<<(int)n.velocity<<","<<(int)n.channel<<","<<(int)n.muted<<","<<(int)n.probability;
        }
    };
    append(midiClips,"midi");append(instrumentClips,"instrument");state<<namingSignature();
    return state;
}
void saveProjectClips(juce::XmlElement&root){syncActiveClip();auto*all=root.createNewChildElement("DynamicMidiClips");auto save=[&](const auto&v,const char*kind){for(const auto&c:v){auto*e=all->createNewChildElement("Clip");e->setAttribute("kind",kind);e->setAttribute("id",c.id);e->setAttribute("lane",c.trackLane);e->setAttribute("start",c.startSeconds);e->setAttribute("length",c.lengthSeconds);e->setAttribute("userLength",c.userLength);e->setAttribute("muted",c.muted);for(const auto&n:c.notes){auto*x=e->createNewChildElement("Note");x->setAttribute("startTick",(double)n.startTick);x->setAttribute("lengthTicks",(double)n.lengthTicks);x->setAttribute("pitch",(int)n.pitch);x->setAttribute("velocity",(int)n.velocity);x->setAttribute("channel",(int)n.channel);x->setAttribute("muted",n.muted);x->setAttribute("probability",(int)n.probability);}}};save(midiClips,"midi");save(instrumentClips,"instrument");syncClipNames();clipNames.save(root);}
void loadProjectClips(const juce::XmlElement&root){inlineMidiFinishGesture();inlineMidiClearSelection();clearArrangementHistory();clipNames.load(root);auto*all=root.getChildByName("DynamicMidiClips");if(!all)return;selectedClipIds.clear();groupOrigins.clear();selectingClips=movingGroup=false;clipboardValid=false;operationMessage.clear();midiClips.clear();instrumentClips.clear();activeIndex=-1;nextClipId=1;std::set<int>usedClipIds;constexpr size_t maxRestoredClips=16384,maxRestoredNotesPerClip=16384,maxVisitedClipElements=32768,maxVisitedNoteElementsPerClip=32768;size_t restoredClips=0,visitedClipElements=0;for(auto*e=all->getFirstChildElement();e;e=e->getNextElement()){if(++visitedClipElements>maxVisitedClipElements)break;if(e->getTagName()!="Clip")continue;if(restoredClips>=maxRestoredClips)break;Clip c;int savedId=e->getIntAttribute("id",0);if(savedId>0&&savedId<std::numeric_limits<int>::max()&&!usedClipIds.count(savedId)){c.id=savedId;usedClipIds.insert(c.id);nextClipId=juce::jmax(nextClipId,c.id+1);}else{while(nextClipId<std::numeric_limits<int>::max()&&usedClipIds.count(nextClipId))++nextClipId;if(nextClipId>=std::numeric_limits<int>::max())continue;c.id=nextClipId++;usedClipIds.insert(c.id);}c.trackLane=e->getIntAttribute("lane",-1);const bool instrument=e->getStringAttribute("kind")=="instrument";const int laneCount=instrument?owner.getInstrumentTrackCount():owner.getMidiTrackCount();if(c.trackLane<0||c.trackLane>=laneCount)continue;const double savedStart=e->getDoubleAttribute("start",0),savedLength=e->getDoubleAttribute("length",2);if(!std::isfinite(savedStart)||!std::isfinite(savedLength)||savedStart<0.0||savedLength<=0.0)continue;c.startSeconds=savedStart;c.lengthSeconds=savedLength;c.userLength=e->getBoolAttribute("userLength",false);c.muted=e->getBoolAttribute("muted",false);size_t visitedNoteElements=0;for(auto*x=e->getFirstChildElement();x;x=x->getNextElement()){if(++visitedNoteElements>maxVisitedNoteElementsPerClip)break;if(x->getTagName()!="Note")continue;if(c.notes.size()>=maxRestoredNotesPerClip)break;const double savedStartTick=x->getDoubleAttribute("startTick",0),savedLengthTicks=x->getDoubleAttribute("lengthTicks",MidiEngine::ticksPerQuarterNote);const double maxTick=(double)std::numeric_limits<std::int64_t>::max();if(!std::isfinite(savedStartTick)||!std::isfinite(savedLengthTicks)||savedStartTick<0.0||savedLengthTicks<=0.0||savedStartTick>maxTick||savedLengthTicks>maxTick)continue;MidiEngine::NoteEvent n;n.startTick=(std::int64_t)std::llround(savedStartTick);n.lengthTicks=(std::int64_t)std::llround(savedLengthTicks);std::int64_t restoredEnd=0;if(!addTicks(n.startTick,n.lengthTicks,restoredEnd))continue;n.pitch=(std::uint8_t)juce::jlimit(0,127,x->getIntAttribute("pitch",60));n.velocity=(std::uint8_t)juce::jlimit(1,127,x->getIntAttribute("velocity",100));n.channel=(std::uint8_t)juce::jlimit(1,16,x->getIntAttribute("channel",1));n.muted=x->getBoolAttribute("muted",false);n.probability=(std::uint8_t)juce::jlimit(0,100,x->getIntAttribute("probability",100));c.notes.push_back(n);}if(instrument)instrumentClips.push_back(std::move(c));else midiClips.push_back(std::move(c));++restoredClips;}syncPlayback();repaint();owner.repaint();}bool commitGeneratedClip(bool ins){auto n=owner.midiEngine.getNotesCopy();return!n.empty()&&commitGeneratedNotes(n,ins,owner.midiClipStartSeconds,owner.midiClipLengthSeconds);}bool commitRecordedInstrumentNotes(const std::vector<MidiEngine::NoteEvent>&n,int lane,double start,double length){if(lane<0||lane>=owner.getInstrumentTrackCount())return false;activeInstrumentLane=lane;return commitGeneratedNotes(n,true,start,length);}bool commitGeneratedMidiNotesToLane(const std::vector<MidiEngine::NoteEvent>&n,int lane,double start,double length){if(lane<0||lane>=owner.getMidiTrackCount())return false;activeMidiLane=lane;return commitGeneratedNotes(n,false,start,length);}bool commitGeneratedNotes(const std::vector<MidiEngine::NoteEvent>&n,bool ins,double start,double length){if(n.empty()||!std::isfinite(start)||!std::isfinite(length)||start<0.0||length<=0.0)return false;Clip c;c.id=allocateClipId();if(c.id==0)return false;c.trackLane=ins?activeInstrumentLane:activeMidiLane;c.startSeconds=juce::jmax(0.0,start);c.lengthSeconds=juce::jmax(secondsPerMeasure(),length);c.userLength=true;c.notes=n;auto&v=ins?instrumentClips:midiClips;v.push_back(std::move(c));activeInstrument=ins;activeIndex=(int)v.size()-1;auto&a=v[(size_t)activeIndex];owner.midiEngine.clear();for(auto&x:a.notes)owner.midiEngine.addNote(x.startTick,x.lengthTicks,x.pitch,x.velocity,x.channel);owner.midiEngine.clearNoteSelection();owner.midiClipStartSeconds=a.startSeconds;owner.midiClipLengthSeconds=a.lengthSeconds;owner.midiClipLengthUserDefined=true;owner.selectedTrack=ins?(owner.getAudioTrackCount()+owner.getMidiTrackCount()+activeInstrumentLane):(owner.getAudioTrackCount()+activeMidiLane);syncPlayback();setBounds(owner.getLocalBounds());toFront(false);repaint();owner.repaint();return true;}void setInstrumentGain(float v)noexcept{instrumentGain.store(juce::jlimit(0.f,2.f,v));}float getInstrumentGain()const noexcept{return instrumentGain.load();}void setInstrumentPan(float v)noexcept{instrumentPan.store(juce::jlimit(-1.f,1.f,v));}float getInstrumentPan()const noexcept{return instrumentPan.load();}void resetProjectClips(){inlineMidiMode=false;inlineMidiFinishGesture();inlineMidiClearSelection();clearArrangementHistory();clipNames={};selectedClipIds.clear();groupOrigins.clear();selectingClips=movingGroup=false;clipboardValid=false;operationMessage.clear();midiClips.clear();instrumentClips.clear();activeIndex=-1;activeMidiLane=0;activeInstrumentLane=0;activeInstrument=false;dragIndex=-1;dragMode=0;nextClipId=1;owner.midiEngine.clear();owner.audioEngine.setProjectExtraLengthSeconds(0.0);repaint();}void synchronizePianoRollPlayback()
{
    int selectedLane = owner.getSelectedPianoInstrumentLane();
    // The dock can edit an instrument while the legacy MIDI clip retains focus.
    // Resolve the actual instrument row instead of silently skipping conversion.
    if (selectedLane < 0 || selectedLane >= owner.getInstrumentTrackCount())
    {
        const int selectedInstrumentRow = owner.selectedTrack
            - owner.getAudioTrackCount() - owner.getMidiTrackCount();
        if (selectedInstrumentRow >= 0 && selectedInstrumentRow < owner.getInstrumentTrackCount())
            selectedLane = selectedInstrumentRow;
        else if (activeInstrument && activeInstrumentLane >= 0
                 && activeInstrumentLane < owner.getInstrumentTrackCount())
            selectedLane = activeInstrumentLane;
        else if (owner.getInstrumentTrackCount() == 1)
            selectedLane = 0;
        else
            return;
    }
    const auto notes = owner.midiEngine.getNotesCopy();
    if (activeIndex >= 0)
    {
        // MIDI-only arrangement clips do not reach the instrument host.
        // Convert an edited clip when the Piano Roll targets an instrument lane.
        if (!activeInstrument && activeIndex < (int) midiClips.size() && !notes.empty())
        {
            syncActiveClip();
            Clip converted = std::move(midiClips[(size_t) activeIndex]);
            midiClips.erase(midiClips.begin() + activeIndex);
            converted.trackLane = selectedLane;
            converted.notes = notes;
            instrumentClips.push_back(std::move(converted));
            activeIndex = (int) instrumentClips.size() - 1;
            activeInstrument = true;
            activeInstrumentLane = selectedLane;
            syncPlayback();
            repaint();
            owner.repaint();
            return;
        }
        if (activeInstrument && activeIndex < (int) instrumentClips.size())
        {
            // The dock's selected instrument lane is authoritative for Piano Roll edits.
            // Keep the clip on that lane so playback uses the same plugin as recording.
            auto& clip = instrumentClips[(size_t) activeIndex];
            const int previousLane = clip.trackLane;
            clip.trackLane = selectedLane;
            activeInstrumentLane = selectedLane;
            syncActivePlayback();
            if (previousLane != selectedLane)
                syncInstrumentLane(previousLane, projectClipEnd());
        }
        return;
    }
    if (notes.empty()) return;
    // Notes entered in the dock with no existing arrangement clip need a
    // real instrument clip so the timeline can send them to the plugin.
    activeInstrumentLane = selectedLane;
    commitGeneratedNotes(notes, true, owner.midiClipStartSeconds,
                         juce::jmax(secondsPerMeasure(), owner.midiClipLengthSeconds));
}
bool activeClipIsInstrument() const noexcept{return activeIndex>=0&&activeInstrument;}int activeInstrumentClipLane() const noexcept{return activeIndex>=0&&activeInstrument&&activeIndex<(int)instrumentClips.size()?instrumentClips[(size_t)activeIndex].trackLane:-1;}void scaleClipsForTempoChange(double ratio){if(!std::isfinite(ratio)||ratio<=0.0)return;if(activeIndex>=0){auto&v=activeInstrument?instrumentClips:midiClips;if(activeIndex<(int)v.size()){auto&c=v[(size_t)activeIndex];c.notes=owner.midiEngine.getNotesCopy();owner.midiClipStartSeconds=c.startSeconds;owner.midiClipLengthSeconds=c.lengthSeconds;owner.midiClipLengthUserDefined=c.userLength;}}auto scale=[ratio](auto& clips){for(auto& c:clips){const double start=c.startSeconds*ratio,length=c.lengthSeconds*ratio;if(std::isfinite(start)&&std::isfinite(length)&&start>=0.0&&length>0.0){c.startSeconds=start;c.lengthSeconds=length;}}};scale(midiClips);scale(instrumentClips);if(activeIndex>=0){auto&v=activeInstrument?instrumentClips:midiClips;if(activeIndex<(int)v.size()){owner.midiClipStartSeconds=v[(size_t)activeIndex].startSeconds;owner.midiClipLengthSeconds=v[(size_t)activeIndex].lengthSeconds;}}syncPlayback();repaint();owner.repaint();}void resyncPlayback(){syncPlayback();}
#include "ClipNamingController.inc"
#include "ArrangementTools.inc"
#include "InlineMidiEditor.inc"
std::set<int> selectedClipIds;
std::map<int,double> groupOrigins;
bool selectingClips=false, movingGroup=false, arrangementFocus=false;
juce::Point<int> rectangleAnchor, rectangleEnd;
float groupMouseX=0;
double groupAnchorSeconds=0;
juce::String operationMessage;
juce::Rectangle<int> selectionRectangle() const {
    return juce::Rectangle<int>(rectangleAnchor,rectangleEnd).getIntersection(
        {owner.trackHeaderWidth,owner.getArrangeTop(),juce::jmax(0,owner.getWidth()-owner.trackHeaderWidth),juce::jmax(0,owner.getMixerTop()-owner.getArrangeTop())});
}
void updateRectangleSelection() {
    selectedClipIds.clear();
    const auto area=selectionRectangle();
    auto visit=[&](const auto& clips,bool instrument){
        for(const auto& clip:clips){
            int x=0,w=0;if(!clipPixelBounds(clip,x,w))continue;
            const int row=owner.getAudioTrackCount()+(instrument?owner.getMidiTrackCount():0)+clip.trackLane;
            const auto bounds=juce::Rectangle<int>(x,owner.getArrangeTrackTop(row)+4,w,owner.getArrangeTrackHeight(row)-8);
            if(area.intersects(bounds))selectedClipIds.insert(clip.id);
        }
    };
    visit(midiClips,false);visit(instrumentClips,true);
    selectedAudio.clear();
    for(int t=0;t<owner.getAudioTrackCount();++t)for(const auto& c:owner.audioEngine.getArrangementAudioClips(t))
        if(area.intersects(audioBounds(t,c)))selectedAudio.insert({t,c.id});
}
void paintSelection(juce::Graphics& g) {
    paintAudioSelection(g);paintClipNames(g);paintAudioFades(g);
    if(selectingClips){
        g.setColour(juce::Colour(0xff4f82ff).withAlpha(.20f));g.fillRect(selectionRectangle());
        g.setColour(juce::Colour(0xff63c7e8));g.drawRect(selectionRectangle(),1);
    }
    if(operationMessage.isNotEmpty()) {
        const int width=juce::jmin(430,juce::jmax(0,owner.getWidth()-owner.trackHeaderWidth-16));
        const juce::Rectangle<int> box(owner.trackHeaderWidth+8,owner.getMixerTop()-28,width,24);
        g.setColour(juce::Colour(0xff142738).withAlpha(.96f));g.fillRoundedRectangle(box.toFloat(),4);
        g.setColour(juce::Colour(0xffbdefff));g.setFont(juce::Font(11));
        g.drawFittedText(operationMessage,box.reduced(6,2),juce::Justification::centredLeft,1);
    }
}
bool handleArrangementKey(const juce::KeyPress& key) {
    if(!arrangementFocus || !hasKeyboardFocus(false))return false;
    if(key.getModifiers().isCommandDown()&&(key.getKeyCode()=='z'||key.getKeyCode()=='Z'))return undoArrangement(key.getModifiers().isShiftDown());
    if(key.getModifiers().isCommandDown()&&(key.getKeyCode()=='y'||key.getKeyCode()=='Y'))return undoArrangement(true);
    if(inlineMidiMode && key.getModifiers().isCommandDown()
       && (key.getKeyCode()=='a'||key.getKeyCode()=='A'))
        return inlineMidiSelectAll();
    if(inlineMidiMode&&key.getKeyCode()==juce::KeyPress::escapeKey
       && !inlineMidiSelectedNotes.empty()){
        inlineMidiClearSelection();repaint();return true;
    }
    if(inlineMidiMode && (key.getKeyCode()==juce::KeyPress::deleteKey
                        ||key.getKeyCode()==juce::KeyPress::backspaceKey)
       && !inlineMidiSelectedNotes.empty())
        return inlineMidiDeleteSelected();
    if((key.getKeyCode()=='e'||key.getKeyCode()=='E') && !key.getModifiers().isCommandDown()
       && !key.getModifiers().isAltDown() && !key.getModifiers().isShiftDown()
       && (activeIndex>=0 || !selectedClipIds.empty())){
        toggleInlineMidiMode();return true;
    }
    if(key.getKeyCode()!=juce::KeyPress::deleteKey && key.getKeyCode()!=juce::KeyPress::backspaceKey)return false;
    if(selectedClipIds.empty()&&selectedAudio.empty())return false;
    beginArrangementEdit();eraseArrangementSelection();finishArrangementEdit();
    operationMessage=juce::String::fromUTF8("Clips sélectionnés supprimés");repaint();return true;
}
bool keyPressed(const juce::KeyPress& key) override {return handleArrangementKey(key);}
static constexpr int toolFrameX=1248,toolGap=3;
int activeArrangeTool=0;Clip clipboard;bool clipboardValid=false;
juce::Rectangle<int> toolFrameBounds() const {
    // Transport deck ends at x=1240. Match its y=31 and height=42,
    // leaving an eight-pixel gap and an eight-pixel window margin.
    return {toolFrameX,31,juce::jmin(345,juce::jmax(0,owner.getWidth()-toolFrameX-8)),42};
}
juce::Rectangle<int> toolBounds(int index)const {
    const auto frame=toolFrameBounds();
    const int width=juce::jmax(0,(frame.getWidth()-20-7*toolGap)/8);
    const int total=8*width+7*toolGap;
    const int left=frame.getX()+(frame.getWidth()-total)/2;
    return {left+index*(width+toolGap),38,width,28};
}
int toolAt(juce::Point<int> p)const{
    for(int i=0;i<8;++i)if(toolBounds(i).contains(p))return i;
    return -1;
}
void drawArrangeTools(juce::Graphics& g){
    // Identical to MainComponent::drawTransport's Obsidian command deck.
    const auto frame=toolFrameBounds().toFloat();
    g.setColour(LibertyUI::Colour::canvas);
    g.fillRoundedRectangle(frame,11.0f);
    g.setColour(LibertyUI::Colour::divider);
    g.drawRoundedRectangle(frame.reduced(0.5f),11.0f,1.0f);
    g.setColour(juce::Colour(0xff526978).withAlpha(0.65f));
    g.drawHorizontalLine(32,frame.getX()+13.0f,frame.getRight()-13.0f);

    static const char* names[]={"Sél","Rect","Cray","Gom","Cis","Col","Muet","Étir"};
    for(int i=0;i<8;++i){
        const auto box=toolBounds(i).toFloat();
        g.setColour(i==activeArrangeTool?juce::Colour(0xff225a70):juce::Colour(0xff192b3b));
        g.fillRoundedRectangle(box,5.0f);
        g.setColour(i==activeArrangeTool?juce::Colour(0xff56dce4):juce::Colour(0xff435b70));
        g.drawRoundedRectangle(box.reduced(0.5f),5.0f,1.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(9.0f,juce::Font::bold));
        g.drawFittedText(juce::String::fromUTF8(names[i]),box.toNearestInt(),juce::Justification::centred,1);
    }
}
bool hitTest(int x,int y)override{
    if(toolAt({x,y})>=0)return true;
    if(nameKeyAt({x,y}).isNotEmpty())return true;
    if (x < owner.trackHeaderWidth) return false;
    const juce::Point<int> p{x,y};
    if (isMixerPoint(p)) return true;
    if(audioLaneAt(p)>=0)return true;
    // Instrument Pattern clips belong to MainComponent, not this MIDI overlay.
    // Let mouse-down, dragging and double-click reach their actual owner.
    const int instrumentLane = laneAtPoint(p,true);
    if (instrumentLane >= 0)
    {
        if (instrumentLane < (int) owner.instrumentStepSequencers.size())
        {
            const double pixelsPerSecond = getLibertyTimelinePixelsPerSecond();
            for (const auto& pattern : owner.instrumentStepSequencers[(size_t) instrumentLane].timelineClips)
            {
                const int left = owner.trackHeaderWidth + (int) std::round(pattern.startSeconds * pixelsPerSecond);
                const int width = juce::jmax(1, (int) std::round(pattern.lengthSeconds * pixelsPerSecond));
                if (pattern.lengthSeconds > 0.0 && x >= left && x < left + width)
                    return false;
            }
        }
        // The MIDI overlay handles double-click creation on empty instrument rows.
        // Pattern clips above remain owned by their existing controller.
        return true;
    }
    return laneAtPoint(p,false) >= 0;
}void paint(juce::Graphics&g)override{drawArrangeTools(g);drawClips(g,false,midiClips,owner.getAudioTrackCount()-owner.getTrackScrollRows(),owner.getAudioTrackCount());drawClips(g,true,instrumentClips,owner.getAudioTrackCount()+owner.getMidiTrackCount()-owner.getTrackScrollRows(),owner.getAudioTrackCount()+owner.getMidiTrackCount());drawInstrumentMixer(g);paintSelection(g);}void mouseMove(const juce::MouseEvent&e)override{if(isMixerPoint(e.getPosition())){setMouseCursor(juce::MouseCursor::PointingHandCursor);return;}
    if(inlineMidiMode&&activeArrangeTool==0){bool ins=false;int ci=-1,ni=-1;
        if(inlineMidiAt(e.getPosition(),ins,ci,ni)&&ni>=0){
            const auto& noteClip=(ins?instrumentClips:midiClips)[(size_t)ci];
            const bool atRightEdge=inlineMidiResizeGripAt(noteClip,ins,
                noteClip.notes[(size_t)ni],e.getPosition());
            setMouseCursor(e.mods.isAltDown()?juce::MouseCursor::CopyingCursor
                :(atRightEdge?juce::MouseCursor::LeftRightResizeCursor
                             :juce::MouseCursor::DraggingHandCursor));
            return;
        }
    }
    if(audioLaneAt(e.getPosition())>=0){
        const auto cursor=(audioFadeHandleAt(e.getPosition())!=0 || crossfadeGripAt(e.getPosition()).mode!=0)
            ?juce::MouseCursor::LeftRightResizeCursor:
            (audioAt(e.getPosition()).isNotEmpty()?juce::MouseCursor::DraggingHandCursor:juce::MouseCursor::CrosshairCursor);
        setMouseCursor(cursor);return;
    }
    bool ins=false;int i=findClipAt(e.getPosition(),ins);if(i<0){setMouseCursor(juce::MouseCursor::CrosshairCursor);return;}auto&c=(ins?instrumentClips:midiClips)[(size_t)i];int l=0,w=0;if(!clipPixelBounds(c,l,w)){setMouseCursor(juce::MouseCursor::NormalCursor);return;}const int r=l+w;setMouseCursor(((float)e.x<=(float)l+resizeZone||(float)e.x>=(float)r-resizeZone)?juce::MouseCursor::LeftRightResizeCursor:juce::MouseCursor::DraggingHandCursor);}void mouseExit(const juce::MouseEvent&)override{setMouseCursor(juce::MouseCursor::NormalCursor);}bool clipOverlaps(const std::vector<Clip>& clips,const Clip& candidate,int except=-1)const{
    for(int j=0;j<(int)clips.size();++j){
        if(j==except)continue;
        const auto& other=clips[(size_t)j];
        if(other.trackLane==candidate.trackLane
           && candidate.startSeconds<other.startSeconds+other.lengthSeconds
           && other.startSeconds<candidate.startSeconds+candidate.lengthSeconds)return true;
    }
    return false;
}
bool applyClipTool(juce::Point<int> p){
    if(p.x<owner.trackHeaderWidth || p.y<owner.getArrangeTop() || p.y>=owner.getMixerTop())return false;
    const bool ins=laneAtPoint(p,true)>=0;
    if(!ins && laneAtPoint(p,false)<0)return false;
    bool hitInstrument=false;
    const int index=findClipAt(p,hitInstrument);
    auto& clips=ins?instrumentClips:midiClips;
    const double px=getLibertyTimelinePixelsPerSecond();
    if(!std::isfinite(px)||px<=0.0)return true;
    const double seconds=snapPosition(juce::jmax(0.0,((double)p.x-owner.trackHeaderWidth)/px));
    if(activeArrangeTool==5&&index<0&&!clipboardValid){operationMessage=juce::String::fromUTF8("Copiez d’abord un clip MIDI pour cette piste");repaint();return true;}
    if(activeArrangeTool==2 && index<0){
        Clip clip;clip.id=allocateClipId();if(clip.id==0)return true;
        clip.trackLane=laneAtPoint(p,ins);clip.startSeconds=seconds;
        clip.lengthSeconds=4.0*secondsPerMeasure();clip.userLength=true;
        if(!clipOverlaps(clips,clip)){clips.push_back(std::move(clip));selectClip(ins,(int)clips.size()-1);syncPlayback();}
    }else if(activeArrangeTool==5 && clipboardValid && index<0){
        const int id=allocateClipId();if(id==0)return true;
        Clip clip=LibertyMidiClipEditing::copyClipAt(clipboard,id,laneAtPoint(p,ins),seconds);
        if(!clipOverlaps(clips,clip)){
            clips.push_back(std::move(clip));selectClip(ins,(int)clips.size()-1);
            selectedClipIds.clear();selectedClipIds.insert(clips.back().id);syncPlayback();
            operationMessage=juce::String::fromUTF8("Clip collé — le presse-papiers reste disponible");
        }else operationMessage=juce::String::fromUTF8("Collage impossible : un clip occupe cette position");
    }else if(index>=0 && hitInstrument==ins){
        if(activeArrangeTool==3 && selectedClipIds.count(clips[(size_t)index].id)){eraseArrangementSelection();return true;}
        selectedAudio.clear();selectClip(ins,index);selectedClipIds.clear();selectedClipIds.insert(clips[(size_t)index].id);
        if(activeArrangeTool==3){
            clips.erase(clips.begin()+index);activeIndex=-1;selectedClipIds.clear();
            owner.midiEngine.clear();syncPlayback();
        }else if(activeArrangeTool==4){
            syncActiveClip();
            const Clip original=clips[(size_t)index];
            const double split=seconds;
            if(split>original.startSeconds+0.001 && split<original.startSeconds+original.lengthSeconds-0.001){
                Clip right=original;right.id=allocateClipId();if(right.id==0)return true;
                const auto cutTick=MidiEngine::secondsToTick(split-original.startSeconds,owner.tempoBpm);
                if(cutTick<=0)return true;
                std::vector<MidiEngine::NoteEvent> leftNotes,rightNotes;
                LibertyMidiClipEditing::splitNotes(original.notes,cutTick,leftNotes,rightNotes);
                right.notes=std::move(rightNotes);
                clips[(size_t)index].notes=std::move(leftNotes);
                // selectClip synchronizes the current editor before switching clips.
                owner.midiEngine.replaceNotes(clips[(size_t)index].notes);
                right.startSeconds=split;right.lengthSeconds=original.startSeconds+original.lengthSeconds-split;
                clips[(size_t)index].lengthSeconds=split-original.startSeconds;
                clips[(size_t)index].userLength=true;right.userLength=true;
                clips.push_back(std::move(right));selectClip(ins,(int)clips.size()-1);syncPlayback();
            }
        }else if(activeArrangeTool==6){
            // Clip mute is a separate playback feature; preserve clip contents.
            clips[(size_t)index].muted=!clips[(size_t)index].muted;
            syncPlayback();
        }else if(activeArrangeTool==5){
            clipboard=clips[(size_t)index];clipboardValid=true;audioClipboardValid=false;operationMessage=juce::String::fromUTF8("Clip copié — cliquez dans un espace libre pour coller");
        }
    }
    repaint();owner.repaint();return true;
}
void mouseDown(const juce::MouseEvent&e)override{
    if(e.eventComponent!=&owner && !owner.isParentOf(e.eventComponent))return;
    if(e.eventComponent!=this){arrangementFocus=false;}

    // JUCE delivers this overlay's click both to its global listener and to
    // the component. A destructive operation or mute toggle must run once.
    if(e.eventComponent==this){
        const auto time=e.eventTime.toMilliseconds();
        if(time==lastLocalMouseDownTime && e.getPosition()==lastLocalMouseDownPoint)return;
        lastLocalMouseDownTime=time;lastLocalMouseDownPoint=e.getPosition();
    }

    const auto ownerPoint = e.getEventRelativeTo(&owner).getPosition();
    // The overlap menu is available on right-click (or Ctrl-click on macOS).
    if(e.eventComponent==this && e.mods.isPopupMenu()){
        if(showAudioCrossfadeMenu(ownerPoint)||inlineMidiContextMenu(ownerPoint))return;
    }
    if(e.mods.isLeftButtonDown()){
        const int tool=toolAt(ownerPoint);
        if(tool>=0){activeArrangeTool=tool;operationMessage=tool==5?juce::String::fromUTF8("Cliquez sur un clip pour copier, puis dans le vide pour coller"):juce::String{};repaint();return;}
    }
    if(e.mods.isLeftButtonDown()&&nameKeyAt(ownerPoint).isNotEmpty()
       && !(e.eventComponent==this &&
            (audioFadeHandleAt(ownerPoint)!=0 || crossfadeGripAt(ownerPoint).mode!=0))){
        if(e.getNumberOfClicks()>=2)renameClipAt(ownerPoint);
        else if(e.eventComponent==this){
            arrangementFocus=true;grabKeyboardFocus();owner.selectedTrack=owner.getArrangeTrackAtY(ownerPoint.y);
            selectedAudio.clear();selectedClipIds.clear();
            const int track=audioLaneAt(ownerPoint);if(track>=0){const auto id=audioAt(ownerPoint);if(id.isNotEmpty())selectedAudio.insert({track,id});}
            else {bool instrument=false;const int index=findClipAt(ownerPoint,instrument);if(index>=0){selectClip(instrument,index);selectedClipIds.insert((instrument?instrumentClips:midiClips)[(size_t)index].id);}}
            repaint();
        }
        return;
    }
    if(e.eventComponent==this&&e.mods.isLeftButtonDown()&&ownerPoint.x>=owner.trackHeaderWidth
       && ownerPoint.y>=owner.getArrangeTop()&&ownerPoint.y<owner.getMixerTop())beginArrangementEdit();
    if(e.eventComponent==this&&e.mods.isLeftButtonDown()&&e.getNumberOfClicks()>=2&&audioLaneAt(ownerPoint)>=0){
        owner.openAudioClipInDockAt(ownerPoint);finishArrangementEdit();return;
    }
    if (e.getNumberOfClicks() >= 2 && e.mods.isLeftButtonDown()
        && (laneAtPoint(ownerPoint, true) >= 0 || laneAtPoint(ownerPoint, false) >= 0)
        && ownerPoint.x >= owner.trackHeaderWidth)
    {
        DBG("Liberty MIDI: instrument double-click at " + juce::String(ownerPoint.x)
            + ", " + juce::String(ownerPoint.y));
        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastInstrumentDoubleClickMs > 350 || ownerPoint != lastInstrumentDoubleClickPoint)
        {
            lastInstrumentDoubleClickMs = now;
            lastInstrumentDoubleClickPoint = ownerPoint;
            handleGridDoubleClick(ownerPoint);
        }
        return;
    }
    if(e.eventComponent==this && e.mods.isLeftButtonDown() && toolAt(ownerPoint)<0){
        arrangementFocus=true;grabKeyboardFocus();
        if(inlineMidiDown(ownerPoint,e.mods.isAltDown(),e.mods.isShiftDown()))return;
        if(handleAudioDown(ownerPoint))return;
        if(activeArrangeTool==0 || activeArrangeTool==1){
            bool instrument=false;const int hit=findClipAt(ownerPoint,instrument);
            const auto& clips=instrument?instrumentClips:midiClips;
            const bool selected=hit>=0 && selectedClipIds.count(clips[(size_t)hit].id);
            if(selected && (activeArrangeTool==1 || selectedClipIds.size()+selectedAudio.size()>1)){
                startGroup(clips[(size_t)hit].startSeconds,(float)e.x);return;
            }
            if(activeArrangeTool==1){
                syncActiveClip();activeIndex=-1;selectedClipIds.clear();selectedAudio.clear();
                rectangleAnchor=rectangleEnd=ownerPoint;selectingClips=true;
                operationMessage=juce::String::fromUTF8("Glissez pour sélectionner les clips");repaint();return;
            }
            selectedClipIds.clear();selectedAudio.clear();
            if(hit>=0)selectedClipIds.insert(clips[(size_t)hit].id);
        }
    }
    if(e.eventComponent == this && e.mods.isLeftButtonDown() && activeArrangeTool>=2 && activeArrangeTool<=6
       && applyClipTool(ownerPoint))return;
    // Listener events from other children must not be interpreted as local overlay coordinates.
    if (e.eventComponent != this) return;
    if(handleMixerMouse(e.getPosition()))return;bool ins=false;int i=findClipAt(e.getPosition(),ins);if(i<0){const int midiLane=laneAtPoint(e.getPosition(),false),instrumentLane=laneAtPoint(e.getPosition(),true);if(midiLane>=0||instrumentLane>=0){syncActiveClip();activeIndex=-1;owner.midiEngine.clear();owner.midiEngine.clearUndoHistory();owner.midiEngine.clearNoteSelection();}if(midiLane>=0){activeMidiLane=midiLane;activeInstrument=false;owner.selectedTrack=owner.getAudioTrackCount()+midiLane;}else if(instrumentLane>=0){activeInstrumentLane=instrumentLane;activeInstrument=true;owner.selectedTrack=owner.getAudioTrackCount()+owner.getMidiTrackCount()+instrumentLane;}owner.repaint();return;}selectClip(ins,i);auto&c=(ins?instrumentClips:midiClips)[(size_t)i];dragInstrument=ins;dragIndex=i;dragStartX=(float)e.x;dragStartSeconds=c.startSeconds;dragStartLength=c.lengthSeconds;stretchOriginalNotes=c.notes;int l=0,w=0;if(!clipPixelBounds(c,l,w)){dragIndex=-1;dragMode=0;return;}const int r=l+w;dragMode=activeArrangeTool==7?4:(float)e.x<=(float)l+resizeZone?2:((float)e.x>=(float)r-resizeZone?3:1);}void mouseDoubleClick(const juce::MouseEvent&e)override{
    if(e.eventComponent!=&owner && !owner.isParentOf(e.eventComponent))return;
    // Receive double-clicks from any child covering the arrangement grid.
    // JUCE reports coordinates relative to the original event component.
    const auto point=e.getEventRelativeTo(&owner).getPosition();
    if(renameClipAt(point))return;
    if(point.y<owner.getArrangeTop()||point.y>=owner.getMixerTop())return;
    if(e.eventComponent==this&&audioLaneAt(point)>=0){owner.openAudioClipInDockAt(point);return;}
    if(laneAtPoint(point,true)<0 && laneAtPoint(point,false)<0)return;
    if (!e.mods.isLeftButtonDown() || point.x < owner.trackHeaderWidth) return;
    // Some JUCE child components dispatch mouseDoubleClick without a second
    // mouseDown reaching the arrangement overlay. Handle both paths safely.
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastInstrumentDoubleClickMs <= 350
        && (point - lastInstrumentDoubleClickPoint).getDistanceFromOrigin() <= 5) return;
    lastInstrumentDoubleClickMs = now;
    lastInstrumentDoubleClickPoint = point;
    handleGridDoubleClick(point);
}
void handleGridDoubleClick(juce::Point<int> point){if(renameClipAt(point))return;if(point.x<owner.trackHeaderWidth)return;bool ins=isPointInTrackRow(point,true);if(!ins&&!isPointInTrackRow(point,false))return;bool hi=false;int i=findClipAt(point,hi);if(i<0){auto&v=ins?instrumentClips:midiClips;Clip c;c.id=allocateClipId();if(c.id==0)return;c.trackLane=laneAtPoint(point,ins);double m=secondsPerMeasure(),raw=juce::jmax(0.0,((double)point.x-owner.trackHeaderWidth)/getLibertyTimelinePixelsPerSecond());c.startSeconds=snapPosition(raw);c.lengthSeconds=ins?4.0*m:m;c.userLength=true;
        // Never create a second clip on top of an existing clip in the same lane.
        bool overlaps=false;
        for(const auto& existing:v)
            if(existing.trackLane==c.trackLane && c.startSeconds<existing.startSeconds+existing.lengthSeconds
               && existing.startSeconds<c.startSeconds+c.lengthSeconds){overlaps=true;break;}
        if(overlaps)return;
        v.push_back(std::move(c));i=(int)v.size()-1;selectClip(ins,i);syncPlayback();repaint();owner.repaint();return;}selectClip(ins,i);openLibertyMidiEditorInDock(owner);syncPlayback();repaint();owner.repaint();}void mouseDrag(const juce::MouseEvent&e)override{
    if(e.eventComponent!=this)return;
    if(audioDragTrack>=0){dragAudio(e.getPosition());return;}
    if(inlineMidiMarquee){inlineMidiMarqueeDrag(e.getPosition());return;}
    if(inlineMidiDragging){inlineMidiDrag(e.getPosition());return;}
    if(selectingClips){rectangleEnd=e.getPosition();updateRectangleSelection();
        operationMessage=juce::String((int)(selectedClipIds.size()+selectedAudio.size()))+juce::String::fromUTF8(" clips sélectionnés — glisser pour déplacer, Suppr pour supprimer");repaint();return;}
    if(movingGroup){
        const double px=getLibertyTimelinePixelsPerSecond();if(!std::isfinite(px)||px<=0)return;
        const double delta=snapPosition(groupAnchorSeconds+((double)e.x-groupMouseX)/px)-groupAnchorSeconds;
        moveSelectedGroup(delta);return;
    }
if(mixerDragMode){dragMixer(e.getPosition());return;}if(dragIndex<0||!dragMode)return;auto&v=dragInstrument?instrumentClips:midiClips;if(dragIndex>=(int)v.size())return;auto&c=v[(size_t)dragIndex];auto step=getLibertySnapSeconds(owner.tempoBpm),minLen=step>0?step:.01,d=((double)e.x-dragStartX)/getLibertyTimelinePixelsPerSecond();if(!std::isfinite(d)||!std::isfinite(minLen)||minLen<=0.0)return;if(dragMode==1){const double moved=snapPosition(dragStartSeconds+d);if(!std::isfinite(moved))return;c.startSeconds=moved;}else if(dragMode==2){double right=dragStartSeconds+dragStartLength;if(!std::isfinite(right))return;double raw=juce::jlimit(0.0,right-minLen,dragStartSeconds+d),ns=snapPosition(raw);if(!std::isfinite(raw)||!std::isfinite(ns))return;ns=juce::jlimit(0.0,right-minLen,ns);const double newLength=juce::jmax(minLen,right-ns);if(!std::isfinite(newLength))return;c.startSeconds=ns;c.lengthSeconds=newLength;c.userLength=true;}else{double raw=juce::jmax(minLen,dragStartLength+d);if(step>0)raw=std::round(raw/step)*step;if(!std::isfinite(raw))return;c.lengthSeconds=juce::jmax(minLen,raw);c.userLength=true;}if(dragMode==4){auto scaled=stretchOriginalNotes;if(LibertyMidiClipEditing::stretchNotes(scaled,c.lengthSeconds/dragStartLength)){c.notes=std::move(scaled);if(isActiveClip(dragInstrument,dragIndex))owner.midiEngine.replaceNotes(c.notes);}}if(isActiveClip(dragInstrument,dragIndex)){owner.midiClipStartSeconds=c.startSeconds;owner.midiClipLengthSeconds=c.lengthSeconds;owner.midiClipLengthUserDefined=c.userLength;}syncPlayback();repaint();owner.repaint();}void mouseUp(const juce::MouseEvent& e)override{
    if(e.eventComponent!=this)return;
    if(audioDragTrack>=0){
        if(audioDragMode==4){operationMessage=owner.audioEngine.stretchArrangementAudioClip(audioDragTrack,audioDragOriginal.id,audioDragLength)
            ?juce::String::fromUTF8("Étirement audio terminé — hauteur conservée"):juce::String::fromUTF8("Étirement impossible — clip conservé");refreshAudio(audioDragTrack);}
        audioDragTrack=-1;audioDragMode=0;
    }
    if(inlineMidiDragging||inlineMidiMarquee)inlineMidiCompleteGesture();
    finishArrangementEdit();
    if(selectingClips){
        selectingClips=false;
        bool chosen=false;
        for(int i=0;i<(int)midiClips.size()&&!chosen;++i)if(selectedClipIds.count(midiClips[(size_t)i].id)){selectClip(false,i);chosen=true;}
        for(int i=0;i<(int)instrumentClips.size()&&!chosen;++i)if(selectedClipIds.count(instrumentClips[(size_t)i].id)){selectClip(true,i);chosen=true;}
        if(!chosen)owner.midiEngine.clear();
        repaint();
    }
    movingGroup=false;groupOrigins.clear();audioOrigins.clear();
dragIndex=-1;dragMode=0;mixerDragMode=0;}
private:bool addTicks(std::int64_t a,std::int64_t b,std::int64_t&result)const noexcept{if((b>0&&a>std::numeric_limits<std::int64_t>::max()-b)||(b<0&&a<std::numeric_limits<std::int64_t>::min()-b))return false;result=a+b;return true;}bool clipPixelBounds(const Clip&c,int&x,int&w)const noexcept{const double px=getLibertyTimelinePixelsPerSecond();const double xd=(double)owner.trackHeaderWidth+c.startSeconds*px,wd=c.lengthSeconds*px;const double lo=(double)std::numeric_limits<int>::min(),hi=(double)std::numeric_limits<int>::max();if(!std::isfinite(xd)||!std::isfinite(wd)||xd<lo||xd>hi||wd<0.0||wd>hi)return false;x=(int)std::round(xd);w=juce::jmax(24,(int)std::round(wd));return true;}int allocateClipId()noexcept{if(nextClipId<=0||nextClipId>=std::numeric_limits<int>::max())return 0;return nextClipId++;}double secondsPerMeasure()const{double beat=60.0/juce::jmax(1.0,owner.tempoBpm)*(4.0/(double)juce::jmax(1,owner.timeSignatureDenominator));return beat*(double)juce::jmax(1,owner.timeSignatureNumerator);}double snapPosition(double s)const{s=juce::jmax(0.0,s);auto st=getLibertySnapSeconds(owner.tempoBpm);return st>0?std::round(s/st)*st:s;}int laneAtPoint(juce::Point<int>p,bool ins)const{int rh=getLibertyTrackRowHeight();int logical=owner.getArrangeTrackAtY(p.y);int first=ins?owner.getAudioTrackCount()+owner.getMidiTrackCount():owner.getAudioTrackCount();int count=ins?owner.getInstrumentTrackCount():owner.getMidiTrackCount();int lane=logical-first;return lane>=0&&lane<count?lane:-1;}bool isPointInTrackRow(juce::Point<int>p,bool ins)const{return laneAtPoint(p,ins)>=0;}int findClipAt(juce::Point<int>p,bool&ins)const{double px=getLibertyTimelinePixelsPerSecond();for(int pass=0;pass<2;++pass){ins=pass==1;if(!isPointInTrackRow(p,ins))continue;auto&v=ins?instrumentClips:midiClips;int lane=laneAtPoint(p,ins);for(int i=(int)v.size()-1;i>=0;--i){auto&c=v[(size_t)i];if(c.trackLane!=lane)continue;int x=0,w=0;if(!clipPixelBounds(c,x,w))continue;int logical=(ins?owner.getAudioTrackCount()+owner.getMidiTrackCount():owner.getAudioTrackCount())+c.trackLane,y=owner.getArrangeTrackTop(logical)+4;// Match the visible clip, including a tall MIDI editing lane.
            if(juce::Rectangle<int>(x,y,w,owner.getArrangeTrackHeight(logical)-8).contains(p))return i;}}return-1;}bool isActiveClip(bool ins,int i)const noexcept{return activeIndex==i&&activeInstrument==ins;}void syncActiveClip(){if(activeIndex<0)return;auto&v=activeInstrument?instrumentClips:midiClips;if(activeIndex>=(int)v.size())return;auto&c=v[(size_t)activeIndex];c.notes=owner.midiEngine.getNotesCopy();if(!c.userLength&&!c.notes.empty()){double end=0;for(auto&n:c.notes){std::int64_t noteEnd=0;if(!addTicks(n.startTick,n.lengthTicks,noteEnd))noteEnd=std::numeric_limits<std::int64_t>::max();end=juce::jmax(end,MidiEngine::tickToSeconds(noteEnd,owner.tempoBpm));}double m=secondsPerMeasure();const double autoLength=std::ceil(end/m)*m;if(std::isfinite(m)&&m>0.0&&std::isfinite(autoLength)&&autoLength>0.0)c.lengthSeconds=juce::jmax(m,autoLength);}owner.midiClipStartSeconds=c.startSeconds;owner.midiClipLengthSeconds=c.lengthSeconds;owner.midiClipLengthUserDefined=c.userLength;}void selectClip(bool ins,int i){syncActiveClip();auto&v=ins?instrumentClips:midiClips;if(i<0||i>=(int)v.size())return;activeInstrument=ins;activeIndex=i;auto&c=v[(size_t)i];if(ins)activeInstrumentLane=c.trackLane;else activeMidiLane=c.trackLane;owner.midiEngine.clear();owner.midiEngine.clearUndoHistory();owner.midiEngine.replaceNotes(c.notes);owner.midiEngine.clearNoteSelection();owner.midiEngine.clearUndoHistory();owner.midiClipStartSeconds=c.startSeconds;owner.midiClipLengthSeconds=c.lengthSeconds;owner.midiClipLengthUserDefined=c.userLength;owner.selectedTrack=ins?(owner.getAudioTrackCount()+owner.getMidiTrackCount()+c.trackLane):(owner.getAudioTrackCount()+c.trackLane);owner.repaint();}void migrateLegacyClip(){auto n=owner.midiEngine.getNotesCopy();if(n.empty())return;Clip c;c.id=allocateClipId();if(c.id==0)return;c.startSeconds=std::isfinite(owner.midiClipStartSeconds)&&owner.midiClipStartSeconds>=0.0?owner.midiClipStartSeconds:0.0;const double measure=secondsPerMeasure();const double legacyLength=owner.midiClipLengthSeconds;c.lengthSeconds=std::isfinite(legacyLength)&&legacyLength>0.0?juce::jmax(measure,legacyLength):measure;c.userLength=owner.midiClipLengthUserDefined;c.notes=n;const int instrumentLane=owner.getSelectedPianoInstrumentLane();if(instrumentLane>=0&&instrumentLane<owner.getInstrumentTrackCount()){c.trackLane=instrumentLane;instrumentClips.push_back(std::move(c));activeInstrumentLane=instrumentLane;activeInstrument=true;activeIndex=(int)instrumentClips.size()-1;syncPlayback();}else{midiClips.push_back(std::move(c));activeIndex=(int)midiClips.size()-1;activeInstrument=false;}}void flattenClips(const std::vector<Clip>&v,std::vector<MidiEngine::NoteEvent>&out)const{out.clear();for(auto&c:v){auto off=MidiEngine::secondsToTick(c.startSeconds,owner.tempoBpm),len=MidiEngine::secondsToTick(c.lengthSeconds,owner.tempoBpm);for(auto&n:c.notes){if(n.startTick>=len)continue;auto a=n;const auto remaining=len-n.startTick;if(remaining<=0)continue;a.lengthTicks=juce::jmin(a.lengthTicks,remaining);if(a.lengthTicks<=0)continue;std::int64_t shifted=0;if(!addTicks(a.startTick,off,shifted))continue;a.startTick=shifted;out.push_back(a);}}}double projectClipEnd()const{double end=0;auto include=[&end](const Clip&c){const double clipEnd=c.startSeconds+c.lengthSeconds;if(std::isfinite(clipEnd))end=juce::jmax(end,clipEnd);};for(const auto&c:midiClips)include(c);for(const auto&c:instrumentClips)include(c);return end;}void syncInstrumentLane(int lane,double end)
{
    if(lane<0||lane>=owner.getInstrumentTrackCount())return;
    // The audio callback prioritizes arrangementSnapshot over noteSnapshot.
    // Publishing only setInstrumentTrackNotes can therefore leave playback
    // silent whenever an (even empty) arrangement snapshot exists.
    std::vector<AudioEngine::InstrumentArrangementClip> clips;
    for(const auto& c:instrumentClips)
        if(c.trackLane==lane && !c.muted)
        {
            AudioEngine::InstrumentArrangementClip clip;
            clip.startSeconds=c.startSeconds;
            clip.lengthSeconds=c.lengthSeconds;
            clip.notes=c.notes;
            clips.push_back(std::move(clip));
        }
    owner.audioEngine.setInstrumentArrangementClips(lane,clips,owner.tempoBpm);
}
// Fast Diagnostic rebuild: verify MIDI timeline playback on Instrument tracks.
void syncPlayback(){syncActiveClip();const double end=projectClipEnd();for(int lane=0;lane<owner.getInstrumentTrackCount();++lane)syncInstrumentLane(lane,end);owner.audioEngine.setProjectExtraLengthSeconds(end);}void syncActivePlayback(){syncActiveClip();const double end=projectClipEnd();if(activeInstrument&&activeIndex>=0&&activeIndex<(int)instrumentClips.size()){const auto& clip=instrumentClips[(size_t)activeIndex];syncInstrumentLane(clip.trackLane,end);owner.audioEngine.setInstrumentTrackNotes(clip.trackLane,clip.muted?std::vector<MidiEngine::NoteEvent>{}:clip.notes,clip.startSeconds,clip.lengthSeconds,owner.tempoBpm);}owner.audioEngine.setProjectExtraLengthSeconds(end);}void drawClips(juce::Graphics&g,bool ins,const std::vector<Clip>&v,int row,int track)const{int rh=getLibertyTrackRowHeight();double px=getLibertyTimelinePixelsPerSecond();for(int i=0;i<(int)v.size();++i){auto&c=v[(size_t)i];int logical=(ins?owner.getAudioTrackCount()+owner.getMidiTrackCount():owner.getAudioTrackCount())+c.trackLane;int y=owner.getArrangeTrackTop(logical);rh=owner.getArrangeTrackHeight(logical);if(y+rh<=owner.getArrangeTop()||y>=owner.getMixerTop())continue;auto col=trackColour(getLibertyTrackColourId(track+c.trackLane));if(c.muted)col=col.withMultipliedSaturation(.15f).darker(.45f);int x=0,w=0;if(!clipPixelBounds(c,x,w))continue;juce::Rectangle<int>r(x,y+4,w,rh-8);g.setColour(col.withAlpha((selectedClipIds.count(c.id)||isActiveClip(ins,i))?.95f:.72f));g.fillRoundedRectangle(r.toFloat(),5);g.setColour(selectedClipIds.count(c.id)?juce::Colour(0xff63c7e8):col.brighter(.35f));g.drawRoundedRectangle(r.toFloat(),5,(selectedClipIds.count(c.id)||isActiveClip(ins,i))?2:1);drawMidiClipTimeGrid(g,r.reduced(2,2),false);g.setColour(juce::Colours::white);g.setFont(juce::Font(10));if(r.getHeight()>44&&!inlineMidiMode&&c.notes.empty())g.drawText(c.muted?"MUET":(ins?"INSTRUMENT":"MIDI"),r.reduced(8).withTrimmedTop(18),juce::Justification::centredLeft,true);drawInlineMidiNotes(g,ins,c);}}bool isMixerPoint(juce::Point<int>)const{return false;}bool handleMixerMouse(juce::Point<int>){return false;}void dragMixer(juce::Point<int>){mixerDragMode=0;}void drawInstrumentMixer(juce::Graphics&g){juce::ignoreUnused(g);}bool activeClipNeedsSync()const{if(activeIndex<0)return false;const auto&v=activeInstrument?instrumentClips:midiClips;if(activeIndex>=(int)v.size())return false;const auto&c=v[(size_t)activeIndex];if(owner.midiEngine.getNumNotes()!=c.notes.size())return true;const auto notes=owner.midiEngine.getNotesCopy();for(size_t i=0;i<notes.size();++i){const auto&a=notes[i];const auto&b=c.notes[i];if(a.startTick!=b.startTick||a.lengthTicks!=b.lengthTicks||a.pitch!=b.pitch||a.velocity!=b.velocity||a.channel!=b.channel||a.muted!=b.muted||a.probability!=b.probability)return true;}return owner.midiClipStartSeconds!=c.startSeconds||owner.midiClipLengthSeconds!=c.lengthSeconds||owner.midiClipLengthUserDefined!=c.userLength;}void timerCallback()override{if(stopped.load())return;if(activeClipNeedsSync()){syncActivePlayback();repaint();}}juce::int64 lastLocalMouseDownTime=-1;juce::Point<int> lastLocalMouseDownPoint{-1,-1};juce::uint32 lastInstrumentDoubleClickMs=0;juce::Point<int> lastInstrumentDoubleClickPoint{-1,-1};MainComponent&owner;std::vector<Clip>midiClips,instrumentClips;int nextClipId=1,activeIndex=-1,activeMidiLane=0,activeInstrumentLane=0;bool activeInstrument=false,dragInstrument=false;int dragIndex=-1,dragMode=0,mixerDragMode=0;float dragStartX=0;double dragStartSeconds=0,dragStartLength=0;std::atomic<float>instrumentGain{1},instrumentPan{0};std::atomic<bool>stopped{false};};std::map<MainComponent*,std::unique_ptr<MultiMidiClipController>>controllers;class Bootstrap:private juce::Timer{
public:
    Bootstrap(){startTimerHz(5);}
    ~Bootstrap(){shutdown();}
    void shutdown(){stopTimer();for(auto&p:controllers)if(p.second)p.second->shutdown();controllers.clear();}
private:
    static MainComponent* findMainComponent(juce::Component* root){
        if(root==nullptr)return nullptr;
        if(auto* main=dynamic_cast<MainComponent*>(root))return main;
        for(int i=0;i<root->getNumChildComponents();++i)
            if(auto* main=findMainComponent(root->getChildComponent(i)))return main;
        return nullptr;
    }
    void timerCallback()override{
        auto& desktop=juce::Desktop::getInstance();
        for(int i=0;i<desktop.getNumComponents();++i){
            if(auto* main=findMainComponent(desktop.getComponent(i))){
                if(!controllers.count(main))
                    controllers.emplace(main,std::make_unique<MultiMidiClipController>(*main));
                stopTimer();
                return;
            }
        }
    }
};Bootstrap bootstrap;}
void synchronizeLibertyPianoRollPlayback(MainComponent& o)
{
    auto it = controllers.find(&o);
    // Dock edits can arrive before the bootstrap timer has installed
    // the arrangement controller. Recorded MIDI already creates it eagerly.
    if (it == controllers.end())
        it = controllers.emplace(&o, std::make_unique<MultiMidiClipController>(o)).first;
    if (it->second)
        it->second->synchronizePianoRollPlayback();
}
void prepareLibertyAIGeneration(MainComponent&o){if(activeController&&activeController->owns(o))activeController->prepareExternalGeneration();}bool commitLibertyAIGeneratedClip(MainComponent&o,bool i){return activeController&&activeController->owns(o)?activeController->commitGeneratedClip(i):false;}bool commitLibertyAIGeneratedNotes(MainComponent&o,const std::vector<MidiEngine::NoteEvent>&n,bool i,double s,double l){return activeController&&activeController->owns(o)?activeController->commitGeneratedNotes(n,i,s,l):false;}bool commitLibertyRecordedInstrumentMidiClip(MainComponent&o,const std::vector<MidiEngine::NoteEvent>&n,int lane,double start,double length){auto it=controllers.find(&o);if(it==controllers.end())it=controllers.emplace(&o,std::make_unique<MultiMidiClipController>(o)).first;auto*controller=it->second.get();return controller&&controller->commitRecordedInstrumentNotes(n,lane,juce::jmax(0.0,start),length);}
bool commitLibertySequencerMidiClip(MainComponent&o,const std::vector<MidiEngine::NoteEvent>&n,int lane,double s,double l){return activeController&&activeController->owns(o)?activeController->commitGeneratedMidiNotesToLane(n,lane,s,l):false;}void setLibertyInstrumentGain(float v){if(activeController)activeController->setInstrumentGain(v);}float getLibertyInstrumentGain(){return activeController?activeController->getInstrumentGain():1;}void setLibertyInstrumentPan(float v){if(activeController)activeController->setInstrumentPan(v);}float getLibertyInstrumentPan(){return activeController?activeController->getInstrumentPan():0;}void saveLibertyMultiMidiClips(MainComponent&o,juce::XmlElement&root){if(activeController&&activeController->owns(o))activeController->saveProjectClips(root);}
void loadLibertyMultiMidiClips(MainComponent&o,const juce::XmlElement&root){if(activeController&&activeController->owns(o))activeController->loadProjectClips(root);}bool handleLibertyInstrumentGridDoubleClick(MainComponent& o, juce::Point<int> point)
{
    const int row = o.getArrangeTrackAtY(point.y);
    const int first = o.getAudioTrackCount() + o.getMidiTrackCount();
    if (point.x < o.trackHeaderWidth || point.y < o.getArrangeTop()
        || point.y >= o.getMixerTop() || row < first
        || row >= first + o.getInstrumentTrackCount()) return false;
    auto it = controllers.find(&o);
    if (it == controllers.end())
        it = controllers.emplace(&o, std::make_unique<MultiMidiClipController>(o)).first;
    if (!it->second) return false;
    it->second->handleGridDoubleClick(point);
    return true;
}
void resizeLibertyMultiMidiClipController(MainComponent*o){if(o&&activeController&&activeController->owns(*o)){activeController->setBounds(o->getLocalBounds());activeController->toFront(false);}}void resetLibertyMultiMidiProject(MainComponent&o){if(activeController&&activeController->owns(o))activeController->resetProjectClips();}void syncLibertyMultiMidiPlayback(MainComponent&o){if(activeController&&activeController->owns(o))activeController->resyncPlayback();}void scaleLibertyMultiMidiClipsForTempoChange(MainComponent&o,double ratio){if(activeController&&activeController->owns(o))activeController->scaleClipsForTempoChange(ratio);}bool isLibertyActiveMidiClipInstrument(MainComponent&o){return activeController&&activeController->owns(o)&&activeController->activeClipIsInstrument();}int getLibertyActiveInstrumentClipLane(MainComponent&o){return activeController&&activeController->owns(o)?activeController->activeInstrumentClipLane():-1;}
void shutdownLibertyMultiMidiClipController(){bootstrap.shutdown();}

juce::String getLibertyMultiMidiStateSignature(const MainComponent& owner)
{
    return activeController && activeController->owns(owner)
        ? activeController->stateSignature() : juce::String{};
}

bool handleLibertyArrangementClipKeyPress(const juce::KeyPress& key)
{
    return activeController && activeController->handleArrangementKey(key);
}

bool hasLibertyArrangementTools(const MainComponent& owner){return activeController && activeController->owns(owner);}

void shutdownLibertyTestControllers();
bool runLibertyArrangementToolsSmoke(){
    MainComponent owner;owner.audioEngine.shutdown();
    MultiMidiClipController controller(owner);
    const bool result=controller.runArrangementTests();
    shutdownLibertyTestControllers();return result;
}

bool handleLibertyClipNameDoubleClick(MainComponent& owner,juce::Point<int> p){return activeController&&activeController->owns(owner)&&activeController->renameClipAt(p);}
bool isLibertyClipNameAt(MainComponent& owner,juce::Point<int> p){return activeController&&activeController->owns(owner)&&activeController->nameKeyAt(p).isNotEmpty();}
void setLibertyClipManualName(MainComponent& owner,const juce::String& key,const juce::String& name){if(activeController&&activeController->owns(owner)){activeController->syncClipNames();activeController->beginArrangementEdit();activeController->clipNames.rename(key,name);activeController->finishArrangementEdit();activeController->repaint();}}
