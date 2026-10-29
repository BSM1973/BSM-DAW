#include <set>
#include <algorithm>
#include <numeric>
#include "AudioEngine.h"
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <cmath>
#include <stdexcept>

bool AudioEngine::validAudioClip(const AudioClipMetadata& c)
{
    if(c.id.isEmpty() || !std::isfinite(c.startSeconds) || c.startSeconds<0
       || !std::isfinite(c.lengthSeconds) || c.lengthSeconds<=0
       || !std::isfinite(c.startSeconds+c.lengthSeconds)
       || !std::isfinite(c.trimStartFraction) || !std::isfinite(c.trimEndFraction)
       || c.trimStartFraction<0 || c.trimEndFraction>1 || c.trimStartFraction>=c.trimEndFraction
       || !std::isfinite(c.clipGain) || c.clipGain<0 || c.clipGain>4
       || !std::isfinite(c.pitchSemitones) || std::abs(c.pitchSemitones)>48
       || !std::isfinite(c.fadeInSeconds) || c.fadeInSeconds<0
       || !std::isfinite(c.fadeOutSeconds) || c.fadeOutSeconds<0
       || !std::isfinite(c.offsetSeconds) || c.offsetSeconds<0
       || !std::isfinite(c.envelopeLengthSeconds) || c.envelopeLengthSeconds<0
       || !std::isfinite(c.formant) || c.warp.count<0 || c.warp.count>maxWarpMarkers
       || (c.sourceBuffer && (!std::isfinite(c.sourceSampleRate) || c.sourceSampleRate<=0
           || c.sourceBuffer->getNumChannels()<1 || c.sourceBuffer->getNumSamples()<1)))return false;
    for(int i=0;i<c.warp.count;++i)
        if(!std::isfinite(c.warp.source[(size_t)i]) || !std::isfinite(c.warp.target[(size_t)i])
           || c.warp.source[(size_t)i]<0 || c.warp.target[(size_t)i]<0
           || (i>0 && (c.warp.source[(size_t)i]<=c.warp.source[(size_t)i-1]
                    || c.warp.target[(size_t)i]<=c.warp.target[(size_t)i-1])))return false;
    return true;
}
std::shared_ptr<AudioEngine::AudioTrackState> AudioEngine::prepareAudioVoice(const AudioClipMetadata& c)
{
    auto v=std::make_shared<AudioTrackState>();
    v->buffer=std::const_pointer_cast<juce::AudioBuffer<float>>(c.sourceBuffer);
    v->bufferSampleRate.store(c.sourceSampleRate);v->startSeconds.store(c.startSeconds);v->lengthSeconds.store(c.lengthSeconds);
    v->clipId=c.id;v->fileName=c.sourceName;v->clipGain.store(c.clipGain);v->clipPitch.store(c.pitchSemitones);
    v->clipReverse.store(c.reverse);v->clipMuted.store(c.muted);v->clipOffset.store(c.offsetSeconds);
    v->clipTrimStart.store(c.trimStartFraction);v->clipTrimEnd.store(c.trimEndFraction);
    v->clipFadeIn.store(c.fadeInSeconds);v->clipFadeOut.store(c.fadeOutSeconds);
    v->autoCrossfadeInStart.store(c.autoCrossfadeInStart);
    v->autoCrossfadeInEnd.store(c.autoCrossfadeInEnd);
    v->autoCrossfadeOutStart.store(c.autoCrossfadeOutStart);
    v->autoCrossfadeOutEnd.store(c.autoCrossfadeOutEnd);
    v->clipEnvelopeLength.store(c.envelopeLengthSeconds);v->warpFormant.store(c.formant);
    auto warp=std::make_shared<WarpMarkerSnapshot>();
    warp->enabled=c.warp.enabled;warp->mode=c.warp.mode;warp->count=c.warp.count;
    warp->source=c.warp.source;warp->target=c.warp.target;v->warpMarkerSnapshot=warp;
    v->warpEnabled.store(warp->enabled);v->warpMode.store(warp->mode);v->warpMarkerCount.store(warp->count);
    for(int i=0;i<maxWarpMarkers;++i){v->warpSourceSeconds[(size_t)i].store(warp->source[(size_t)i]);v->warpTargetSeconds[(size_t)i].store(warp->target[(size_t)i]);}
    v->spectralCache=c.spectralCache;
    v->loaded.store(c.sourceBuffer!=nullptr && c.sourceBuffer->getNumSamples()>0);
    return v;
}
std::vector<AudioEngine::AudioClipMetadata> AudioEngine::getArrangementAudioClips(int index) const
{
    if(!isValidTrackIndex(index))return {};
    auto clips=getAudioClips(index);
    const auto& t=*tracks[(size_t)index];
    if(t.loaded.load()){
        AudioClipMetadata c;c.id=t.clipId;c.sourceName=t.fileName;
        c.sourceBuffer=std::atomic_load(&t.buffer);c.sourceSampleRate=t.bufferSampleRate.load();
        c.startSeconds=t.startSeconds.load();c.lengthSeconds=t.lengthSeconds.load();
        c.clipGain=t.clipGain.load();c.pitchSemitones=t.clipPitch.load();c.reverse=t.clipReverse.load();
        c.muted=t.clipMuted.load();c.offsetSeconds=t.clipOffset.load();c.envelopeLengthSeconds=t.clipEnvelopeLength.load();
        c.trimStartFraction=t.clipTrimStart.load();c.trimEndFraction=t.clipTrimEnd.load();
        c.fadeInSeconds=t.clipFadeIn.load();c.fadeOutSeconds=t.clipFadeOut.load();
        c.autoCrossfadeInStart=t.autoCrossfadeInStart.load();
        c.autoCrossfadeInEnd=t.autoCrossfadeInEnd.load();
        c.autoCrossfadeOutStart=t.autoCrossfadeOutStart.load();
        c.autoCrossfadeOutEnd=t.autoCrossfadeOutEnd.load();
        c.warp=getTrackWarpSnapshot(index);c.formant=t.warpFormant.load();c.spectralCache=std::atomic_load(&t.spectralCache);
        clips.insert(clips.begin(),std::move(c));
    }
    return clips;
}
bool AudioEngine::replaceArrangementAudioClips(int index,const std::vector<AudioClipMetadata>& clips)
{
    if(!isValidTrackIndex(index))return false;
    try {
        auto extras=clips;
        // Calculate only simple adjacent two-clip overlaps. More complex
        // layering remains untouched rather than altering audio unexpectedly.
        for(auto& c:extras){
            c.autoCrossfadeInStart=c.autoCrossfadeInEnd=-1.0;
            c.autoCrossfadeOutStart=c.autoCrossfadeOutEnd=-1.0;
        }
        std::vector<size_t> chronological(extras.size());
        std::iota(chronological.begin(),chronological.end(),size_t{0});
        std::stable_sort(chronological.begin(),chronological.end(),[&](size_t a,size_t b){
            return extras[a].startSeconds<extras[b].startSeconds;
        });
        for(size_t i=1;i<chronological.size();++i){
            auto& earlier=extras[chronological[i-1]];
            auto& later=extras[chronological[i]];
            if(!earlier.sourceBuffer || !later.sourceBuffer || earlier.muted || later.muted)
                continue;
            const double earlierEnd=earlier.startSeconds+earlier.lengthSeconds;
            const double laterEnd=later.startSeconds+later.lengthSeconds;
            // Ignore equal-start layers, containment and non-overlapping clips.
            if(!(earlier.startSeconds<later.startSeconds && later.startSeconds<earlierEnd
                 && earlierEnd<=laterEnd))continue;
            const double start=later.startSeconds,end=earlierEnd;
            bool tripleOverlap=false;
            for(size_t third=0;third<extras.size();++third){
                if(third==chronological[i-1]||third==chronological[i])continue;
                const auto& other=extras[third];
                if(other.sourceBuffer && !other.muted && other.startSeconds<end
                   && other.startSeconds+other.lengthSeconds>start){tripleOverlap=true;break;}
            }
            if(tripleOverlap)continue;
            earlier.autoCrossfadeOutStart=start;earlier.autoCrossfadeOutEnd=end;
            later.autoCrossfadeInStart=start;later.autoCrossfadeInEnd=end;
        }
        std::shared_ptr<AudioTrackState> primary;
        for(auto it=extras.begin();it!=extras.end();++it)
            if(it->sourceBuffer && it->sourceBuffer->getNumSamples()>0){primary=prepareAudioVoice(*it);extras.erase(it);break;}
        if(!primary)primary=std::make_shared<AudioTrackState>();
        // Validate and prepare every allocation before stopping the callback.
        auto validation=clips;std::set<juce::String> ids;
        for(const auto& c:validation)
            if(!validAudioClip(c)||!ids.insert(c.id).second)return false;
        const bool running=initialised.load();
        if(running)deviceManager.removeAudioCallback(this);
        if(!replaceAudioClips(index,extras)){if(running)deviceManager.addAudioCallback(this);return false;}
        auto& t=*tracks[(size_t)index];t.loaded.store(false);
        std::atomic_store(&t.buffer,primary->buffer);t.bufferSampleRate.store(primary->bufferSampleRate.load());
        t.startSeconds.store(primary->startSeconds.load());t.lengthSeconds.store(primary->lengthSeconds.load());
        t.fileName.swapWith(primary->fileName);t.clipId.swapWith(primary->clipId);
        t.clipGain.store(primary->clipGain.load());t.clipPitch.store(primary->clipPitch.load());
        t.clipReverse.store(primary->clipReverse.load());t.clipMuted.store(primary->clipMuted.load());
        t.clipOffset.store(primary->clipOffset.load());t.clipEnvelopeLength.store(primary->clipEnvelopeLength.load());
        t.clipTrimStart.store(primary->clipTrimStart.load());t.clipTrimEnd.store(primary->clipTrimEnd.load());
        t.clipFadeIn.store(primary->clipFadeIn.load());t.clipFadeOut.store(primary->clipFadeOut.load());
        t.autoCrossfadeInStart.store(primary->autoCrossfadeInStart.load());
        t.autoCrossfadeInEnd.store(primary->autoCrossfadeInEnd.load());
        t.autoCrossfadeOutStart.store(primary->autoCrossfadeOutStart.load());
        t.autoCrossfadeOutEnd.store(primary->autoCrossfadeOutEnd.load());
        t.warpEnabled.store(primary->warpEnabled.load());t.warpMode.store(primary->warpMode.load());
        t.warpFormant.store(primary->warpFormant.load());t.warpMarkerCount.store(primary->warpMarkerCount.load());
        for(int i=0;i<maxWarpMarkers;++i){t.warpSourceSeconds[(size_t)i].store(primary->warpSourceSeconds[(size_t)i].load());t.warpTargetSeconds[(size_t)i].store(primary->warpTargetSeconds[(size_t)i].load());}
        std::atomic_store(&t.warpMarkerSnapshot,primary->warpMarkerSnapshot);
        std::atomic_store(&t.spectralCache,primary->spectralCache);
        const auto generation=t.spectralGeneration.fetch_add(1)+1;t.spectralReadyGeneration.store(primary->spectralCache?generation:generation-1);t.spectralBlend=0;t.richeTransientPrimed=false;
        t.contentRevision.fetch_add(1);t.loaded.store(primary->loaded.load());
        if(running)deviceManager.addAudioCallback(this);
        return true;
    }catch(...){return false;}
}
bool AudioEngine::promoteAudioClip(int track,const juce::String& id)
{
    auto clips=getArrangementAudioClips(track);
    const auto it=std::find_if(clips.begin(),clips.end(),[&](const auto& c){return c.id==id;});
    if(it==clips.end()||!it->sourceBuffer)return false;
    if(it==clips.begin())return true;
    std::rotate(clips.begin(),it,it+1);
    return replaceArrangementAudioClips(track,clips);
}
bool AudioEngine::splitArrangementAudioClip(int track,const juce::String& id,double seconds)
{
    auto clips=getArrangementAudioClips(track);
    for(size_t i=0;i<clips.size();++i){
        auto& left=clips[i];if(left.id!=id)continue;
        const double cut=seconds-left.startSeconds;
        if(!std::isfinite(cut)||cut<=0||cut>=left.lengthSeconds)return false;
        auto right=left;right.id=juce::Uuid().toString();right.startSeconds=seconds;
        if(left.envelopeLengthSeconds<=0)left.envelopeLengthSeconds=left.offsetSeconds+left.lengthSeconds;
        right.envelopeLengthSeconds=left.envelopeLengthSeconds;
        right.offsetSeconds+=cut;right.lengthSeconds-=cut;left.lengthSeconds=cut;
        clips.insert(clips.begin()+(int)i+1,std::move(right));
        return replaceArrangementAudioClips(track,clips);
    }
    return false;
}
std::shared_ptr<juce::AudioBuffer<float>> AudioEngine::renderArrangementAudioClip(const AudioClipMetadata& clip)
{
    const double rate=clip.sourceSampleRate;
    if(!clip.sourceBuffer || rate<=0 || !std::isfinite(clip.lengthSeconds) || clip.lengthSeconds<=0
       || clip.lengthSeconds*rate>32000000)return {};
    try{
        auto c=clip;c.startSeconds=0;c.muted=false;
        auto voice=prepareAudioVoice(c);
        auto output=std::make_shared<juce::AudioBuffer<float>>(2,(int)std::llround(c.lengthSeconds*rate));output->clear();
        for(int offset=0;offset<output->getNumSamples();offset+=256){
            float* channels[]={output->getWritePointer(0,offset),output->getWritePointer(1,offset)};
            mixAudioVoice(*voice,1,0,rate,offset,channels,2,juce::jmin(256,output->getNumSamples()-offset));
        }
        return output;
    }catch(...){return {};}
}
bool AudioEngine::stretchArrangementAudioClip(int track,const juce::String& id,double seconds)
{
    if(!std::isfinite(seconds)||seconds<=0)return false;
    auto clips=getArrangementAudioClips(track);
    for(auto& c:clips){
        if(c.id!=id)continue;
        if(!c.sourceBuffer){c.lengthSeconds=seconds;return replaceArrangementAudioClips(track,clips);}
        // Stretch the neutral signal so gain, pitch, reverse and fades remain
        // editable clip properties instead of being baked into the samples.
        auto neutral=c;
        neutral.clipGain=1.0f;
        neutral.pitchSemitones=0.0;
        neutral.reverse=false;
        neutral.fadeInSeconds=0.0;
        neutral.fadeOutSeconds=0.0;
        neutral.autoCrossfadeInStart=neutral.autoCrossfadeInEnd=-1.0;
        neutral.autoCrossfadeOutStart=neutral.autoCrossfadeOutEnd=-1.0;
        auto source=renderArrangementAudioClip(neutral);
        if(!source||seconds*c.sourceSampleRate>32000000)return false;
        try {
            auto output=std::make_shared<juce::AudioBuffer<float>>(2,(int)std::llround(seconds*c.sourceSampleRate));
            if(output->getNumSamples()<1)return false;
            signalsmith::stretch::SignalsmithStretch<float> stretch;stretch.presetDefault(2,c.sourceSampleRate);
            stretch.setTransposeFactor(1.0);
            stretch.process(source->getArrayOfReadPointers(),source->getNumSamples(),output->getArrayOfWritePointers(),output->getNumSamples());
            c.sourceBuffer=output;c.sourcePath.clear();c.lengthSeconds=seconds;c.offsetSeconds=0;
            // The source region and Warp mapping are baked into the new buffer.
            // Keep independent, user-editable clip controls unchanged.
            c.envelopeLengthSeconds=0;c.trimStartFraction=0;c.trimEndFraction=1;c.warp={};c.spectralCache.reset();
            return replaceArrangementAudioClips(track,clips);
        }catch(...){return false;}
    }
    return false;
}
