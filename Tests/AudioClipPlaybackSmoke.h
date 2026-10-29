#pragma once
#include "AudioEngine.h"
#include <cmath>
#include <stdexcept>
inline bool runLibertyAudioClipPlaybackSmoke()
{
    try {
        AudioEngine engine;
        constexpr int count=4800;
        auto source=std::make_shared<juce::AudioBuffer<float>>(1,count);
        for(int i=0;i<count;++i)source->setSample(0,i,(float)i/count);
        AudioEngine::AudioClipMetadata clip;
        clip.id="test";clip.lengthSeconds=.1;clip.sourceSampleRate=48000;clip.sourceBuffer=source;
        auto render=[&](const AudioEngine::AudioClipMetadata& c,int block=128){
            if(!engine.replaceAudioClips(0,{c}))throw std::runtime_error("publication");
            juce::AudioBuffer<float> output(2,count);
            if(!engine.renderOfflineForTesting(output,48000,block))throw std::runtime_error("render");
            return output;
        };
        auto near=[](float actual,float expected){if(std::abs(actual-expected)>.0002f)throw std::runtime_error("sample");};
        auto normal=render(clip);near(normal.getSample(0,1200),.25f);
        clip.clipGain=.5f;auto quiet=render(clip);near(quiet.getSample(0,1200),.125f);
        clip.clipGain=1;clip.reverse=true;auto reversed=render(clip);near(reversed.getSample(0,1200),3599.f/count);
        clip.reverse=false;clip.fadeInSeconds=.05;auto fade=render(clip);near(fade.getSample(0,1200),.125f);
        clip.fadeInSeconds=0;clip.fadeOutSeconds=.05;fade=render(clip);near(fade.getSample(0,3600),.375f);
        clip.fadeOutSeconds=0;clip.pitchSemitones=12;auto pitched=render(clip);near(pitched.getSample(0,1200),.5f);
        clip.pitchSemitones=0;clip.muted=true;auto muted=render(clip);near(muted.getMagnitude(0,count),0);
        clip.muted=false;clip.trimStartFraction=.25;clip.trimEndFraction=.75;
        auto trim=render(clip);near(trim.getSample(0,0),.25f);near(trim.getSample(0,2400),0);
        auto otherBlock=render(clip,511);
        for(int i=0;i<count;++i)near(trim.getSample(0,i),otherBlock.getSample(0,i));
        auto invalid=clip;invalid.lengthSeconds=-1;
        if(engine.replaceAudioClips(0,{invalid})||engine.getAudioClips(0).size()!=1)throw std::runtime_error("transaction");
        clip.clipGain=.37f;clip.pitchSemitones=-7;clip.reverse=true;clip.muted=true;
        clip.fadeInSeconds=.013;clip.fadeOutSeconds=.021;clip.sourceName="Échantillon";
        juce::XmlElement xml("Clip");AudioEngine::writeAudioClipProperties(xml,clip);
        auto parsed=juce::parseXML(xml.toString());if(!parsed)throw std::runtime_error("xml");
        auto restored=AudioEngine::readAudioClipProperties(*parsed);
        juce::XmlElement again("Clip");AudioEngine::writeAudioClipProperties(again,restored);
        if(xml.toString()!=again.toString())throw std::runtime_error("properties roundtrip");
        restored.sourceBuffer=source;restored.sourceSampleRate=48000;
        if(!engine.replaceAudioClips(0,{restored}))throw std::runtime_error("restore");
        // Replacing an invalid selection must leave the published state intact.
        auto duplicate=restored;
        if(engine.replaceAudioClips(0,{restored,duplicate}))throw std::runtime_error("duplicate IDs");
        if(engine.getAudioClips(0).front().clipGain!=.37f)throw std::runtime_error("rollback");
        AudioEngine::AudioClipMetadata whole;whole.id="whole";whole.sourceName="Original";
        whole.lengthSeconds=.1;whole.sourceSampleRate=48000;whole.sourceBuffer=source;
        whole.reverse=true;whole.fadeInSeconds=.02;whole.fadeOutSeconds=.03;whole.clipGain=.8f;
        if(!engine.replaceArrangementAudioClips(0,{whole}))throw std::runtime_error("primary");
        juce::AudioBuffer<float> before(2,count),after(2,count);
        engine.renderOfflineForTesting(before,48000,128);
        if(!engine.splitArrangementAudioClip(0,"whole",.0375))throw std::runtime_error("split");
        auto split=engine.getArrangementAudioClips(0);
        if(split.size()!=2 || split[0].sourceBuffer!=source || split[1].sourceBuffer!=source)throw std::runtime_error("source preservation");
        engine.renderOfflineForTesting(after,48000,511);
        for(int i=0;i<count;++i)near(before.getSample(0,i),after.getSample(0,i));
        if(!engine.promoteAudioClip(0,split[1].id))throw std::runtime_error("dock promotion");
        engine.renderOfflineForTesting(after,48000,256);
        for(int i=0;i<count;++i)near(before.getSample(0,i),after.getSample(0,i));
        auto roundtrip=engine.getArrangementAudioClips(0);
        for(auto& c:roundtrip){juce::XmlElement e("Clip");AudioEngine::writeAudioClipProperties(e,c);
            auto restoredClip=AudioEngine::readAudioClipProperties(e);restoredClip.sourceBuffer=c.sourceBuffer;restoredClip.sourceSampleRate=c.sourceSampleRate;c=restoredClip;}
        if(!engine.replaceArrangementAudioClips(0,roundtrip))throw std::runtime_error("region restore");
        engine.renderOfflineForTesting(after,48000,128);
        for(int i=0;i<count;++i)near(before.getSample(0,i),after.getSample(0,i));
        auto invalidPrimary=whole;invalidPrimary.offsetSeconds=std::numeric_limits<double>::quiet_NaN();
        if(engine.replaceArrangementAudioClips(0,{invalidPrimary}))throw std::runtime_error("invalid primary accepted");
        // Restoring snapshots must restore both the sound and all region properties.
        if(!engine.replaceArrangementAudioClips(0,{whole}))throw std::runtime_error("undo snapshot");
        engine.renderOfflineForTesting(after,48000,128);
        for(int i=0;i<count;++i)near(before.getSample(0,i),after.getSample(0,i));
        if(!engine.replaceArrangementAudioClips(0,split))throw std::runtime_error("redo snapshot");
        engine.renderOfflineForTesting(after,48000,128);
        for(int i=0;i<count;++i)near(before.getSample(0,i),after.getSample(0,i));
        // Signalsmith must change duration while preserving the frequency.
        auto tone=std::make_shared<juce::AudioBuffer<float>>(1,48000);
        for(int i=0;i<48000;++i)tone->setSample(0,i,.25f*std::sin(2*juce::MathConstants<double>::pi*440*i/48000));
        auto stretched=whole;stretched.id="tone";stretched.sourceBuffer=tone;stretched.lengthSeconds=1;
        stretched.reverse=false;stretched.clipGain=1;stretched.fadeInSeconds=stretched.fadeOutSeconds=0;
        if(!engine.replaceArrangementAudioClips(0,{stretched}) || !engine.stretchArrangementAudioClip(0,"tone",1.5))throw std::runtime_error("stretch");
        const auto result=engine.getArrangementAudioClips(0).front();
        if(result.sourceBuffer->getNumSamples()!=72000 || tone->getNumSamples()!=48000)throw std::runtime_error("stretch length");
        int crossings=0;const auto* samples=result.sourceBuffer->getReadPointer(0);
        for(int i=24001;i<48000;++i)if(samples[i-1]<=0 && samples[i]>0)++crossings;
        if(std::abs(crossings*2-440)>4)throw std::runtime_error("stretch pitch");
        // Stretch must not silently discard the clip's editable controls.
        auto configured=stretched;
        configured.id="stretch-properties";
        configured.clipGain=.42f;
        configured.pitchSemitones=3;
        configured.reverse=true;
        configured.fadeInSeconds=.04;
        configured.fadeOutSeconds=.06;
        if(!engine.replaceArrangementAudioClips(0,{configured})
           || !engine.stretchArrangementAudioClip(0,configured.id,1.5))
            throw std::runtime_error("stretch properties operation");
        const auto preserved=engine.getArrangementAudioClips(0).front();
        if(std::abs(preserved.clipGain-configured.clipGain)>1.e-6f
           || preserved.pitchSemitones!=configured.pitchSemitones
           || preserved.reverse!=configured.reverse
           || preserved.fadeInSeconds!=configured.fadeInSeconds
           || preserved.fadeOutSeconds!=configured.fadeOutSeconds)
            throw std::runtime_error("stretch properties lost");
        // Two adjacent overlapping clips must crossfade without summing to +6 dB.
        // Moving the right clip must remove the derived transition automatically.
        auto constant=std::make_shared<juce::AudioBuffer<float>>(1,48000);
        constant->clear();
        for(int i=0;i<constant->getNumSamples();++i)constant->setSample(0,i,.25f);
        AudioEngine::AudioClipMetadata first,second;
        first.id="crossfade-left";first.sourceBuffer=constant;first.sourceSampleRate=48000;
        first.startSeconds=0;first.lengthSeconds=1;
        second=first;second.id="crossfade-right";second.startSeconds=.5;
        if(!engine.replaceArrangementAudioClips(0,{first,second}))throw std::runtime_error("crossfade setup");
        auto xfade=engine.getArrangementAudioClips(0);
        if(xfade.size()!=2 || xfade[0].fadeOutSeconds!=0 || xfade[1].fadeInSeconds!=0
            || xfade[0].autoCrossfadeOutStart!=.5 || xfade[0].autoCrossfadeOutEnd!=1
            || xfade[1].autoCrossfadeInStart!=.5 || xfade[1].autoCrossfadeInEnd!=1)
            throw std::runtime_error("crossfade metadata");
        juce::AudioBuffer<float> crossRender(2,72000);
        if(!engine.renderOfflineForTesting(crossRender,48000,511))throw std::runtime_error("crossfade render");
        near(crossRender.getSample(0,12000),.25f); // left region alone
        near(crossRender.getSample(0,36000),.25f); // overlap midpoint
        near(crossRender.getSample(0,60000),.25f); // right region alone
        second.startSeconds=1;
        if(!engine.replaceArrangementAudioClips(0,{first,second}))throw std::runtime_error("crossfade moved");
        xfade=engine.getArrangementAudioClips(0);
        if(xfade[0].autoCrossfadeOutStart>=0 || xfade[1].autoCrossfadeInStart>=0)
            throw std::runtime_error("stale automatic fade after move");
        // Muting one region must not duck the remaining audible region.
        second.startSeconds=.5;second.muted=true;
        if(!engine.replaceArrangementAudioClips(0,{first,second}))throw std::runtime_error("crossfade mute");
        if(!engine.renderOfflineForTesting(crossRender,48000,128))throw std::runtime_error("crossfade mute render");
        near(crossRender.getSample(0,36000),.25f);
        // Confirm all three curves affect the rendered overlap and the
        // curve preference survives the existing project XML serialization.
        auto quieter=std::make_shared<juce::AudioBuffer<float>>(1,48000);
        for(int i=0;i<48000;++i)quieter->setSample(0,i,.125f);
        second.muted=false;second.sourceBuffer=quieter;
        for(int curve=0;curve<3;++curve){
            second.crossfadeCurve=curve;
            if(!engine.replaceArrangementAudioClips(0,{first,second}))
                throw std::runtime_error("crossfade curve setup");
            const auto regions=engine.getArrangementAudioClips(0);
            if(regions.size()!=2 || regions[0].autoCrossfadeCurve!=curve
               || regions[1].autoCrossfadeCurve!=curve || regions[1].crossfadeCurve!=curve)
                throw std::runtime_error("crossfade curve publication");
            if(!engine.renderOfflineForTesting(crossRender,48000,257))
                throw std::runtime_error("crossfade curve render");
            const double p=.25;
            const double shaped=curve==1?std::sqrt(p):curve==2?p*p*(3-2*p):p;
            const double shapedOut=curve==1?std::sqrt(1-p):1-shaped;
            near(crossRender.getSample(0,30000),(float)(.25*shapedOut+.125*shaped));
            juce::XmlElement projectClip("Clip");
            AudioEngine::writeAudioClipProperties(projectClip,regions[1]);
            const auto loadedClip=AudioEngine::readAudioClipProperties(projectClip);
            if(loadedClip.crossfadeCurve!=curve)
                throw std::runtime_error("crossfade curve XML persistence");
        }
        auto badCurve=second;badCurve.crossfadeCurve=4;
        if(engine.replaceArrangementAudioClips(0,{first,badCurve}))
            throw std::runtime_error("invalid crossfade curve accepted");
        // The primary and additional paths must use the same Warp renderer.
        for(int mode=0;mode<=5;++mode){
            auto warped=stretched;warped.warp.enabled=true;warped.warp.mode=mode;warped.warp.count=2;
            warped.warp.source[1]=1;warped.warp.target[1]=1.25;warped.lengthSeconds=1.25;
            warped.clipGain=.6f;warped.fadeInSeconds=.02;warped.pitchSemitones=3;
            if(!engine.replaceArrangementAudioClips(0,{warped}))throw std::runtime_error("primary warp");
            juce::AudioBuffer<float> primary(2,60000),extra(2,60000);
            engine.renderOfflineForTesting(primary,48000,128);
            if(!engine.replaceArrangementAudioClips(0,{})||!engine.replaceAudioClips(0,{warped}))throw std::runtime_error("extra warp");
            engine.renderOfflineForTesting(extra,48000,128);
            for(int i=0;i<60000;++i)near(primary.getSample(0,i),extra.getSample(0,i));
        }
        return true;
    }catch(const std::exception& e){std::fprintf(stderr,"AudioClipPlaybackSmoke: %s\n",e.what());return false;}
}
