#pragma once
#include "AudioEngine.h"
#include "AudioCrossfadeCurve.h"
#include <cmath>
#include <stdexcept>
#include <string>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <fstream>
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
        std::size_t comparisonIndex=0;
        auto near=[&](float actual,float expected){
            ++comparisonIndex;
            if(!std::isfinite(actual) || std::abs(actual-expected)>.0002f)
                throw std::runtime_error("sample comparison "
                    + std::to_string(comparisonIndex) + ": actual="
                    + std::to_string(actual) + ", expected=" + std::to_string(expected));
        };
        auto normal=render(clip);near(normal.getSample(0,1200),.25f);
        // Real BUS signal path: exactly one source contribution reaches Master.
        if(engine.addMixerBus()!=0)throw std::runtime_error("bus creation");
        engine.setTrackMixerBus(0,0);
        if(engine.getTrackMixerBus(0)!=0)throw std::runtime_error("bus assign");
        auto grouped=render(clip);
        near(grouped.getSample(0,1200),normal.getSample(0,1200));
        near(grouped.getSample(1,1200),normal.getSample(1,1200));
        engine.setMixerBusGain(0,.5f);
        grouped=render(clip);
        near(grouped.getSample(0,1200),.125f);
        engine.setMixerBusGain(0,1.f);
        engine.setMixerBusMuted(0,true);
        grouped=render(clip);
        near(grouped.getSample(0,1200),0.f);
        engine.setMixerBusMuted(0,false);
        engine.setMixerBusSolo(0,true);
        grouped=render(clip);
        near(grouped.getSample(0,1200),.25f);
        engine.setTrackMixerBus(0,-1);
        grouped=render(clip);
        near(grouped.getSample(0,1200),0.f); // solo BUS filters direct sources
        engine.setMixerBusSolo(0,false);
        grouped=render(clip);
        near(grouped.getSample(0,1200),.25f);
        engine.resetMixerBuses();
        if(engine.getMixerBusCount()!=0||engine.getTrackMixerBus(0)!=-1)
            throw std::runtime_error("bus project reset");

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
        // Rhythmic analysis: regular synthetic pulses, silence and source preservation.
        auto beatSource=std::make_shared<juce::AudioBuffer<float>>(1,48000*4);
        beatSource->clear();
        for(int onset=0;onset<beatSource->getNumSamples();onset+=24000)
            for(int i=0;i<480;++i)beatSource->setSample(0,onset+i,.5f);
        AudioEngine::AudioClipMetadata beat;beat.id="beat-analysis";beat.sourceBuffer=beatSource;
        beat.sourceSampleRate=48000;beat.lengthSeconds=4;
        const auto rhythm=engine.analyseArrangementClipRhythm(beat);
        if(rhythm.transients.size()!=8||std::abs(rhythm.bpm-120)>1||rhythm.confidence<.8)
            throw std::runtime_error("rhythmic analysis regular pulse");
        auto silent=std::make_shared<juce::AudioBuffer<float>>(1,48000*4);silent->clear();
        auto silentClip=beat;silentClip.sourceBuffer=silent;
        if(engine.analyseArrangementClipRhythm(silentClip).bpm!=0)throw std::runtime_error("rhythmic analysis silence");
        AudioEngine::AudioClipMetadata prepared;
        if(!engine.prepareStretchedArrangementClip(beat,2,prepared)||prepared.id!=beat.id||
            prepared.sourceBuffer==beatSource||prepared.sourceBuffer->getNumSamples()!=96000||
            beatSource->getNumSamples()!=192000||engine.prepareStretchedArrangementClip(beat,-1,prepared))
            throw std::runtime_error("staged stretch");
        // Fixed endpoints and preserved source, quantization strength 0/50/100.
        auto offbeat=std::make_shared<juce::AudioBuffer<float>>(1,48000*4);offbeat->clear();
        for(int onset=14400;onset<offbeat->getNumSamples();onset+=24000)
            for(int i=0;i<480;++i)offbeat->setSample(0,onset+i,.5f);
        auto groove=beat;groove.sourceBuffer=offbeat;
        for(double strength:{0.0,.5,1.0}){
            AudioEngine::AudioClipMetadata mapped;
            if(!engine.prepareQuantizedArrangementClip(groove,.25,strength,mapped)||
                mapped.sourceBuffer!=offbeat||mapped.lengthSeconds!=4||mapped.warp.count!=10||
                mapped.warp.source[0]!=0||mapped.warp.target[9]!=4||
                std::abs(mapped.warp.target[1]-(.30-.05*strength))>.006)
                throw std::runtime_error("warp quantize strength");
        }
        AudioEngine::AudioClipMetadata rejected;
        if(engine.prepareQuantizedArrangementClip(groove,0,1,rejected)||
            engine.prepareQuantizedArrangementClip(groove,.25,1.5,rejected)||
            engine.prepareQuantizedArrangementClip(groove,4,1,rejected))
            throw std::runtime_error("warp invalid or collapsing grid");
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
        AudioEngine::AudioClipMetadata shifted;
        if(!engine.prepareStretchedArrangementClip(stretched,1.0,shifted,12)||
            shifted.sourceBuffer->getNumSamples()!=48000||stretched.sourceBuffer!=tone)
            throw std::runtime_error("constant length pitch shift");
        int shiftedCrossings=0;
        for(int i=12001;i<36001;++i)
            if(shifted.sourceBuffer->getSample(0,i-1)<=0&&shifted.sourceBuffer->getSample(0,i)>0)++shiftedCrossings;
        if(std::abs(shiftedCrossings*2-880)>12)throw std::runtime_error("constant length octave shift");
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
        // Phase 2.4.2: all added shapes are bounded, monotonic, complementary
        // and identical in the rendered callback and the shared display law.
        for(int curve=3;curve<=LibertyAudioCrossfade::maxCurve;++curve){
            double last=-1;
            for(int i=0;i<=1000;++i){
                const double p=i/1000.;
                const double in=LibertyAudioCrossfade::level(curve,p,true);
                const double out=LibertyAudioCrossfade::level(curve,p,false);
                if(!std::isfinite(in)||!std::isfinite(out)||in< -1.e-12||in>1.+1.e-12||
                   in+1.e-12<last||std::abs(in+out-1.)>1.e-12||
                   std::abs(in-LibertyAudioCrossfade::manualFadeLevel(curve,p,true))>1.e-12)
                    throw std::runtime_error("advanced curve mathematical bounds");
                last=in;
            }
            if(std::abs(LibertyAudioCrossfade::level(curve,0,true))>1.e-12||
               std::abs(LibertyAudioCrossfade::level(curve,1,true)-1.)>1.e-12)
                throw std::runtime_error("advanced curve endpoints");
            second.crossfadeCurve=curve;
            if(!engine.replaceArrangementAudioClips(0,{first,second})||
               !engine.renderOfflineForTesting(crossRender,48000,257,false))
                throw std::runtime_error("advanced crossfade render");
            for(int i:{27000,30000,36000,42000,45000}){
                const double p=(i-24000)/24000.;
                const double g=LibertyAudioCrossfade::level(curve,p,true);
                near(crossRender.getSample(0,i),float(.25*(1-g)+.125*g));
            }
            auto independent=second;independent.fadeInCurve=curve;
            independent.fadeOutCurve=LibertyAudioCrossfade::maxCurve+3-curve;
            juce::XmlElement xml("Clip");AudioEngine::writeAudioClipProperties(xml,independent);
            const auto restored=AudioEngine::readAudioClipProperties(xml);
            if(restored.fadeInCurve!=independent.fadeInCurve||
               restored.fadeOutCurve!=independent.fadeOutCurve||restored.crossfadeCurve!=curve)
                throw std::runtime_error("advanced curves XML roundtrip");
        }
        // A shortened fade uses the entire overlap for routing, but not for its ramp.
        second.crossfadeCurve=0;second.crossfadeDurationSeconds=.2;
        if(!engine.replaceArrangementAudioClips(0,{first,second}))throw std::runtime_error("short crossfade setup");
        const auto narrowed=engine.getArrangementAudioClips(0);
        if(narrowed.size()!=2 || std::abs(narrowed[1].autoCrossfadeInStart-.65)>1.e-9
           || std::abs(narrowed[1].autoCrossfadeInEnd-.85)>1.e-9
           || narrowed[1].startSeconds!=.5 || narrowed[0].startSeconds!=0
           || narrowed[1].autoCrossfadeOverlapStart!=.5 || narrowed[1].autoCrossfadeOverlapEnd!=1)
            throw std::runtime_error("short crossfade window");
        if(!engine.renderOfflineForTesting(crossRender,48000,257))throw std::runtime_error("short crossfade render");
        near(crossRender.getSample(0,26400),.25f);
        near(crossRender.getSample(0,36000),.1875f);
        near(crossRender.getSample(0,45600),.125f);
        juce::XmlElement durationXml("Clip");
        AudioEngine::writeAudioClipProperties(durationXml,narrowed[1]);
        if(std::abs(AudioEngine::readAudioClipProperties(durationXml).crossfadeDurationSeconds-.2)>1.e-9)
            throw std::runtime_error("crossfade duration persistence");
        auto badDuration=second;badDuration.crossfadeDurationSeconds=-1;
        if(engine.replaceArrangementAudioClips(0,{first,badDuration}))throw std::runtime_error("negative duration accepted");
        badDuration.crossfadeDurationSeconds=std::numeric_limits<double>::quiet_NaN();
        if(engine.replaceArrangementAudioClips(0,{first,badDuration}))throw std::runtime_error("NaN duration accepted");
        if(std::abs(engine.getArrangementAudioClips(0)[1].crossfadeDurationSeconds-.2)>1.e-9)
            throw std::runtime_error("invalid crossfade modified state");
        auto badCurve=second;badCurve.crossfadeCurve=LibertyAudioCrossfade::maxCurve+1;
        if(engine.replaceArrangementAudioClips(0,{first,badCurve}))
            throw std::runtime_error("invalid crossfade curve accepted");
        // Audio perfection: independent measurement fixtures.
        {const float a[]={-1.f,-.5f,.5f,1.f},b[]={1.f,.5f,-.5f,-1.f};
         const auto m=LibertyAudioCrossfade::measureSignal(a,4);
         if(std::abs(m.peak-1.)>1.e-12||std::abs(m.maxStep-1.)>1.e-12||m.zeroCrossings!=1||
            std::abs(LibertyAudioCrossfade::signalCorrelation(a,a,4)-1.)>1.e-12||
            std::abs(LibertyAudioCrossfade::signalCorrelation(a,b,4)+1.)>1.e-12)
             throw std::runtime_error("crossfade audio perfection metrics");}
        // Studio PRO: energy invariant for different curve pairs, and bounded
        // theoretical gain-step diagnostic (not a signal-based click detector).
        for(int in:{0,1,3,5,7,31,131,231})for(int out:{0,1,4,9,31,131,231}){
            for(int k=0;k<=1000;++k){
                const double p=k/1000.;
                const double x=LibertyAudioCrossfade::studioLevel(in,out,p,true);
                const double y=LibertyAudioCrossfade::studioLevel(out,in,p,false);
                if(!std::isfinite(x)||!std::isfinite(y)||std::abs(x*x+y*y-1.)>1.e-10)
                    throw std::runtime_error("studio pro energy invariant");
            }
        }
        if(LibertyAudioCrossfade::studioGainStep(0,0,false,480)>.003||
           LibertyAudioCrossfade::studioGainStep(0,0,false,2)<.49)
            throw std::runtime_error("studio pro gain step diagnostic");
        // Studio: independent gain laws, moved shortened transition and optional
        // sum-of-squares compensation, with legacy defaults preserved.
        auto studio=second;studio.crossfadeCenter=.25;studio.crossfadeDurationSeconds=.2;
        studio.crossfadeCurve=5;studio.crossfadeOutCurve=181;studio.crossfadeEnergy=true;
        if(!engine.replaceArrangementAudioClips(0,{first,studio})||
           !engine.renderOfflineForTesting(crossRender,48000,257,false))
            throw std::runtime_error("studio crossfade render");
        const auto studioClips=engine.getArrangementAudioClips(0);
        if(std::abs(studioClips[1].autoCrossfadeInStart-.575)>1.e-9||
           studioClips[0].autoCrossfadeOutCurve!=181||studioClips[1].autoCrossfadeInCurve!=5)
            throw std::runtime_error("studio placement or independent curves");
        for(int i:{27600,30000,32400,34800,37200}){
            const double p=(i-27600)/9600.;
            const double in=LibertyAudioCrossfade::studioLevel(5,181,p,true);
            const double out=LibertyAudioCrossfade::studioLevel(181,5,p,false);
            if(std::abs(in*in+out*out-1.)>1.e-10)throw std::runtime_error("studio energy");
            near(crossRender.getSample(0,i),float(.25*out+.125*in));
        }
        juce::XmlElement studioXml("Clip");AudioEngine::writeAudioClipProperties(studioXml,studio);
        const auto studioRestored=AudioEngine::readAudioClipProperties(studioXml);
        if(studioRestored.crossfadeCenter!=.25||studioRestored.crossfadeOutCurve!=181||!studioRestored.crossfadeEnergy)
            throw std::runtime_error("studio XML persistence");
        juce::XmlElement oldStudioXml("Clip");
        const auto legacyStudio=AudioEngine::readAudioClipProperties(oldStudioXml);
        if(legacyStudio.crossfadeCenter!=.5||legacyStudio.crossfadeOutCurve!=-1||legacyStudio.crossfadeEnergy)
            throw std::runtime_error("studio legacy defaults");
        // REPITCH 2:1 duration must halve frequency, not preserve pitch.
        auto repitched=stretched;repitched.warp.enabled=true;repitched.warp.mode=6;repitched.warp.count=2;
        repitched.warp.source[1]=1;repitched.warp.target[1]=2;repitched.lengthSeconds=2;
        if(!engine.replaceArrangementAudioClips(0,{repitched})||engine.getTrackWarpMode(0)!=6||
           engine.getTrackWarpSnapshot(0).mode!=6)throw std::runtime_error("repitch mode publication");
        juce::XmlElement repitchXml("Clip");AudioEngine::writeAudioClipProperties(repitchXml,repitched);
        if(AudioEngine::readAudioClipProperties(repitchXml).warp.mode!=6)throw std::runtime_error("repitch mode XML");
        juce::AudioBuffer<float> repitchOut(2,96000);
        if(!engine.renderOfflineForTesting(repitchOut,48000,257,false))throw std::runtime_error("repitch render");
        int repitchCrossings=0;
        for(int i=24001;i<72001;++i){
            const float sample=repitchOut.getSample(0,i);
            if(!std::isfinite(sample))throw std::runtime_error("repitch nonfinite");
            if(repitchOut.getSample(0,i-1)<=0&&sample>0)++repitchCrossings;
        }
        if(std::abs(repitchCrossings-220)>3)throw std::runtime_error("repitch frequency");
        AudioEngine::AudioClipMetadata vocalShift;
        if(!engine.prepareStretchedArrangementClip(stretched,1.0,vocalShift,3,true)||
            vocalShift.sourceBuffer->getNumSamples()!=48000)throw std::runtime_error("formant shift render");
        for(int i=0;i<48000;++i)if(!std::isfinite(vocalShift.sourceBuffer->getSample(0,i)))
            throw std::runtime_error("formant shift nonfinite");
        // Decimation of an 18 kHz tone at 2x speed must suppress its 12 kHz alias.
        auto high=std::make_shared<juce::AudioBuffer<float>>(1,48000);
        for(int i=0;i<48000;++i)high->setSample(0,i,.25f*std::sin(2*juce::MathConstants<double>::pi*18000*i/48000));
        auto fast=repitched;fast.sourceBuffer=high;fast.lengthSeconds=.5;fast.warp.target[1]=.5;
        if(!engine.replaceArrangementAudioClips(0,{fast}))throw std::runtime_error("repitch antialias setup");
        juce::AudioBuffer<float> fastOut(2,24000);
        if(!engine.renderOfflineForTesting(fastOut,48000,128,false))throw std::runtime_error("repitch antialias render");
        double aliasEnergy=0;
        for(int i=1000;i<23000;++i){const double x=fastOut.getSample(0,i);aliasEnergy+=x*x;}
        if(!std::isfinite(aliasEnergy)||std::sqrt(aliasEnergy/22000)>.0125)
            throw std::runtime_error("repitch alias rejection");
        // Preserve an in-band tone at the same decimation ratio.
        fast.sourceBuffer=tone;
        if(!engine.replaceArrangementAudioClips(0,{fast})||!engine.renderOfflineForTesting(fastOut,48000,128,false))
            throw std::runtime_error("repitch passband render");
        if(fastOut.getMagnitude(0,1000,22000)<.05f)throw std::runtime_error("repitch passband lost");
        // Worker-safe stretch path: no AudioEngine instance or live state.
        AudioEngine::AudioClipMetadata workerResult;
        if(!AudioEngine::prepareRawStretchedArrangementClip(stretched,1.5,workerResult)||
           workerResult.sourceBuffer->getNumSamples()!=72000||stretched.sourceBuffer!=tone)
            throw std::runtime_error("worker raw stretch");
        int workerCrossings=0;
        for(int i=24001;i<48001;++i)
            if(workerResult.sourceBuffer->getSample(0,i-1)<=0&&workerResult.sourceBuffer->getSample(0,i)>0)++workerCrossings;
        if(std::abs(workerCrossings*2-440)>6)throw std::runtime_error("worker raw stretch frequency");
        auto editedRaw=stretched;editedRaw.reverse=true;
        if(AudioEngine::prepareRawStretchedArrangementClip(editedRaw,1.5,workerResult)||
           AudioEngine::prepareRawStretchedArrangementClip(stretched,10,workerResult))
            throw std::runtime_error("worker raw stretch geometry");
        // Stable harmonic signal, cancellation and pitch-synchronous render.
        auto harmonic=std::make_shared<juce::AudioBuffer<float>>(1,48000);
        for(int i=0;i<48000;++i){
            const double phase=2*juce::MathConstants<double>::pi*220*i/48000;
            harmonic->setSample(0,i,float(.18*std::sin(phase)+.07*std::sin(2*phase)+.03*std::sin(3*phase)));
        }
        auto tonalClip=stretched;tonalClip.sourceBuffer=harmonic;
        AudioEngine::AudioClipMetadata tonalOut;
        double f0=0;
        const auto tonalStart=std::chrono::steady_clock::now();
        if(!AudioEngine::prepareTonalArrangementClip(tonalClip,1.5,tonalOut,nullptr,&f0)||
           std::abs(f0-220)>2||tonalOut.sourceBuffer->getNumSamples()!=72000||tonalClip.sourceBuffer!=harmonic)
            throw std::runtime_error("tonal render");
        int tonalCrossings=0;double aEnergy=0,bEnergy=0;
        for(int i=24001;i<48001;++i){
            const double x=tonalOut.sourceBuffer->getSample(0,i);
            if(!std::isfinite(x))throw std::runtime_error("tonal nonfinite");
            if(tonalOut.sourceBuffer->getSample(0,i-1)<=0&&x>0)++tonalCrossings;
            bEnergy+=x*x;
            const double original=harmonic->getSample(0,i-12000);aEnergy+=original*original;
        }
        if(std::abs(tonalCrossings*2-220)>4||bEnergy<.001||
           std::abs(10*std::log10(bEnergy/aEnergy))>1)throw std::runtime_error("tonal pitch or level");
        std::fprintf(stderr,"tonal_quality,%.4f,%.4f,%.4f\n",f0,10*std::log10(bEnergy/aEnergy),
            std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-tonalStart).count());
        std::atomic<bool> cancelled{true};const auto savedBuffer=tonalOut.sourceBuffer;
        if(AudioEngine::prepareTonalArrangementClip(tonalClip,1.5,tonalOut,&cancelled)||
           tonalOut.sourceBuffer!=savedBuffer)throw std::runtime_error("tonal cancelled transaction");
        auto silenceTonal=tonalClip;silenceTonal.sourceBuffer=silent;silenceTonal.lengthSeconds=4;
        if(AudioEngine::prepareTonalArrangementClip(silenceTonal,6,tonalOut))
            throw std::runtime_error("tonal silence rejected");
        // Dynamic tracking and variable-period reconstruction on synthetic vibrato.
        auto vibrato=std::make_shared<juce::AudioBuffer<float>>(1,48000*2);
        double phaseV=0;
        for(int i=0;i<vibrato->getNumSamples();++i){
            const double f=220*std::pow(2.0,45*std::sin(2*juce::MathConstants<double>::pi*5*i/48000)/1200);
            phaseV+=2*juce::MathConstants<double>::pi*f/48000;
            vibrato->setSample(0,i,float(.2*std::sin(phaseV)+.04*std::sin(2*phaseV)));
        }
        auto melody=tonalClip;melody.sourceBuffer=vibrato;melody.lengthSeconds=2;
        AudioEngine::AudioClipMetadata melodicOut;
        double vibratoRate=0,vibratoDepth=0;
        const auto melodicStart=std::chrono::steady_clock::now();
        if(!AudioEngine::prepareMelodicArrangementClip(melody,3,melodicOut,nullptr,&vibratoRate,&vibratoDepth)||
           melodicOut.sourceBuffer->getNumSamples()!=144000||melody.sourceBuffer!=vibrato||
           std::abs(vibratoRate-5)>.6||vibratoDepth<15||vibratoDepth>65)
            throw std::runtime_error("melodic vibrato tracking");
        int melodyCrossings=0;double melodyEnergy=0;
        for(int i=48001;i<96001;++i){
            const float x=melodicOut.sourceBuffer->getSample(0,i);
            if(!std::isfinite(x))throw std::runtime_error("melody nonfinite");
            if(melodicOut.sourceBuffer->getSample(0,i-1)<=0&&x>0)++melodyCrossings;
            melodyEnergy+=double(x)*x;
        }
        if(std::abs(melodyCrossings-220)>8||melodyEnergy/48000<.005)
            throw std::runtime_error("melodic pitch or energy");
        std::fprintf(stderr,"melodic_quality,%.4f,%.4f,%d,%.6f,%.4f\n",vibratoRate,vibratoDepth,melodyCrossings,
            std::sqrt(melodyEnergy/48000),std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-melodicStart).count());
        if(AudioEngine::prepareMelodicArrangementClip(melody,3,melodicOut,&cancelled))
            throw std::runtime_error("melodic cancellation");
        // Compare spectral offline timings separately from callback CPU.
        const auto spectralStart=std::chrono::steady_clock::now();
        auto spectral=AudioEngine::renderSpectralWarpOffline(*harmonic,1.5,0,0);
        if(spectral.getNumSamples()!=72000)throw std::runtime_error("spectral length");
        double spectralEnergy=0,spectralPeak=0;
        for(int i=0;i<spectral.getNumSamples();++i){
            const double x=spectral.getSample(0,i);
            if(!std::isfinite(x))throw std::runtime_error("spectral nonfinite");
            spectralEnergy+=x*x;spectralPeak=std::max(spectralPeak,std::abs(x));
        }
        const double spectralRms=std::sqrt(spectralEnergy/spectral.getNumSamples());
        if(spectralRms<.07||spectralRms>.23||spectralPeak>.8)
            throw std::runtime_error("spectral gain or peak regression");
        std::fprintf(stderr,"spectral_quality,%.4f,%.6f\n",
            std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-spectralStart).count(),
            std::sqrt(spectralEnergy/spectral.getNumSamples()));
        std::fprintf(stderr,"spectral_peak_rms,%.6f,%.6f\n",spectralPeak,std::sqrt(spectralEnergy/spectral.getNumSamples()));
        // Unity parity and bounds at extreme ratios: no edge spikes, NaNs
        // or silent results accepted. Log the RMS comparison, not a listening score.
        auto spectralUnity=AudioEngine::renderSpectralWarpOffline(*harmonic,1,0,0);
        if(spectralUnity.getNumSamples()!=48000)throw std::runtime_error("spectral unity duration");
        double dryEnergy=0;
        for(int i=0;i<48000;++i){
            near(spectralUnity.getSample(0,i),harmonic->getSample(0,i));
            const double x=harmonic->getSample(0,i);dryEnergy+=x*x;
        }
        const double dryRms=std::sqrt(dryEnergy/48000);
        for(double ratio:{.25,.5,1.,1.5,2.,4.}){
            auto wet=AudioEngine::renderSpectralWarpOffline(*harmonic,ratio,0,0);
            if(wet.getNumSamples()!=(int)std::llround(48000*ratio))throw std::runtime_error("spectral ratio length");
            double sum=0,peak=0,jump=0;
            for(int i=0;i<wet.getNumSamples();++i){
                const double x=wet.getSample(0,i);
                if(!std::isfinite(x))throw std::runtime_error("spectral ratio nonfinite");
                sum+=x*x;peak=std::max(peak,std::abs(x));
                if(i>0)jump=std::max(jump,std::abs(x-wet.getSample(0,i-1)));
            }
            const double rms=std::sqrt(sum/wet.getNumSamples());
            if(rms<.01||rms>.3||peak>.8)throw std::runtime_error("spectral extreme ratio safety");
            std::fprintf(stderr,"spectral_final,%.2f,%.6f,%.6f,%.6f,%.4f\n",ratio,rms,peak,jump,20*std::log10(dryRms/rms));
        }
        // Phase 2.3.8: objective level-matched A/B and stereo repeatability.
        // Keep production DSP unchanged: compensation is diagnostic only.
        for(double ratio:{.25,.5,1.,1.5,2.,4.}){
            auto wet=AudioEngine::renderSpectralWarpOffline(*harmonic,ratio,0,0);
            if(wet.getNumSamples()!=(int)std::llround(48000*ratio))
                throw std::runtime_error("ultimate AB duration");
            double e=0;
            for(int i=0;i<wet.getNumSamples();++i){
                const double x=wet.getSample(0,i);e+=x*x;
            }
            if(!std::isfinite(e)||e<=0)throw std::runtime_error("ultimate AB energy");
            const double gain=dryRms/std::sqrt(e/wet.getNumSamples());
            double compensatedEnergy=0,compensatedPeak=0;int overFullScale=0;
            for(int i=0;i<wet.getNumSamples();++i){
                const double x=wet.getSample(0,i)*gain;
                if(!std::isfinite(x))throw std::runtime_error("ultimate AB nonfinite");
                compensatedEnergy+=x*x;compensatedPeak=std::max(compensatedPeak,std::abs(x));
                if(std::abs(x)>1.0)++overFullScale;
            }
            const double matchedRms=std::sqrt(compensatedEnergy/wet.getNumSamples());
            if(std::abs(matchedRms-dryRms)>1.e-6)
                throw std::runtime_error("ultimate AB RMS mismatch");
            // Clipping is a quality finding, not a reason to silently limit
            // the comparison or claim that the raw algorithm passed listening.
            std::fprintf(stderr,"warp_ultimate_ab,%.2f,%.6f,%.6f,%.6f,%d\n",
                ratio,gain,matchedRms,compensatedPeak,overFullScale);
        }
        juce::AudioBuffer<float> stereoWarpFixture(2,48000);
        for(int i=0;i<48000;++i){
            const float x=harmonic->getSample(0,i);
            stereoWarpFixture.setSample(0,i,x);
            stereoWarpFixture.setSample(1,i,x);
        }
        for(double ratio:{.5,1.,1.5,4.}){
            const auto a=AudioEngine::renderSpectralWarpOffline(stereoWarpFixture,ratio,0,0);
            const auto b=AudioEngine::renderSpectralWarpOffline(stereoWarpFixture,ratio,0,0);
            if(a.getNumChannels()!=2||b.getNumChannels()!=2||
                a.getNumSamples()!=(int)std::llround(48000*ratio)||
                b.getNumSamples()!=a.getNumSamples())
                throw std::runtime_error("ultimate stereo dimensions");
            double maxDelta=0,channelDelta=0;
            for(int i=0;i<a.getNumSamples();++i){
                const double l=a.getSample(0,i),rr=a.getSample(1,i);
                const double bl=b.getSample(0,i),br=b.getSample(1,i);
                if(!std::isfinite(l)||!std::isfinite(rr)||!std::isfinite(bl)||!std::isfinite(br))
                    throw std::runtime_error("ultimate stereo nonfinite");
                maxDelta=std::max(maxDelta,std::max(std::abs(l-bl),std::abs(rr-br)));
                channelDelta=std::max(channelDelta,std::abs(l-rr));
            }
            if(maxDelta>1.e-5||channelDelta>1.e-5)
                throw std::runtime_error("ultimate stereo repeatability");
            std::fprintf(stderr,"warp_ultimate_stereo,%.2f,%.9f,%.9f\n",
                ratio,maxDelta,channelDelta);
        }
        // Phase 2.3.9: a neutral fourfold stretch must preserve harmonic
        // energy without relying on diagnostic gain or a limiter.
        const auto extreme=AudioEngine::renderSpectralWarpOffline(*harmonic,4,0,0);
        if(extreme.getNumSamples()!=192000)throw std::runtime_error("perfection extreme length");
        double extremeEnergy=0,extremePeak=0;
        for(int i=0;i<extreme.getNumSamples();++i){
            const double x=extreme.getSample(0,i);
            if(!std::isfinite(x))throw std::runtime_error("perfection extreme nonfinite");
            extremeEnergy+=x*x;extremePeak=std::max(extremePeak,std::abs(x));
        }
        const double extremeRms=std::sqrt(extremeEnergy/extreme.getNumSamples());
        const double lossDb=20*std::log10(extremeRms/dryRms);
        if(!std::isfinite(lossDb)||std::abs(lossDb)>1.0||extremePeak>.8)
            throw std::runtime_error("perfection extreme energy or peak");
        std::fprintf(stderr,"warp_perfection_x4,%.6f,%.6f,%.4f\n",extremeRms,extremePeak,lossDb);
        // Export actual engine renders when the caller supplies a directory.
        // Both files use the same headroom factor after RMS matching; no clipper.
        if(const char* exportDir=std::getenv("LIBERTY_WARP_WAV_DIR")){
            const double match=dryRms/extremeRms;
            const double headroom=std::min(1.0,.95/std::max((double)harmonic->getMagnitude(0,48000),extremePeak*match));
            auto wav=[&](const char* name,const juce::AudioBuffer<float>& buffer,double gain){
                std::ofstream f(std::string(exportDir)+"/"+name,std::ios::binary|std::ios::trunc);
                auto u16=[&](std::uint16_t v){f.put(char(v&255));f.put(char((v>>8)&255));};
                auto u32=[&](std::uint32_t v){u16(std::uint16_t(v));u16(std::uint16_t(v>>16));};
                const auto bytes=std::uint32_t(buffer.getNumSamples()*2);
                f.write("RIFF",4);u32(36+bytes);f.write("WAVEfmt ",8);u32(16);
                u16(1);u16(1);u32(48000);u32(96000);u16(2);u16(16);
                f.write("data",4);u32(bytes);
                for(int i=0;i<buffer.getNumSamples();++i){
                    const auto sample=std::lround(buffer.getSample(0,i)*gain*32767);
                    if(sample < -32767 || sample > 32767)throw std::runtime_error("perfection WAV clipped");
                    u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(sample)));
                }
                f.close();if(!f)throw std::runtime_error("perfection WAV write");
            };
            wav("warp_dry.wav",*harmonic,headroom);
            wav("warp_x4_rms_matched.wav",extreme,match*headroom);
        }
        // Opt-in extended render: 60 seconds of stereo OUTPUT at 48 kHz.
        // Report offline elapsed time and buffer bytes, not live callback CPU/RSS.
        if(std::getenv("LIBERTY_WARP_EXTENDED")){
            juce::AudioBuffer<float> longSource(2,720000);
            for(int i=0;i<longSource.getNumSamples();++i){
                const auto x=harmonic->getSample(0,i%48000);
                longSource.setSample(0,i,x);longSource.setSample(1,i,x);
            }
            const auto started=std::chrono::steady_clock::now();
            const auto longWet=AudioEngine::renderSpectralWarpOffline(longSource,4,0,0);
            if(longWet.getNumChannels()!=2||longWet.getNumSamples()!=2880000)
                throw std::runtime_error("perfection extended length");
            double energy=0,peak=0;
            for(int ch=0;ch<2;++ch)for(int i=0;i<longWet.getNumSamples();++i){
                const double x=longWet.getSample(ch,i);
                if(!std::isfinite(x))throw std::runtime_error("perfection extended nonfinite");
                energy+=x*x;peak=std::max(peak,std::abs(x));
            }
            const double rms=std::sqrt(energy/(2.*longWet.getNumSamples()));
            if(rms<.1||rms>.2||peak>.8)throw std::runtime_error("perfection extended energy");
            std::fprintf(stderr,"warp_perfection_extended,60,%.4f,%.6f,%.6f,%zu\n",
                std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count(),
                rms,peak,(size_t)longWet.getNumSamples()*2*sizeof(float));
        }
        // Phase 2.3.10: retain the 48 kHz test above and add 44.1/96 kHz.
        // 15 seconds of input at each rate gives 60 seconds of stereo output.
        if(std::getenv("LIBERTY_WARP_EXTENDED")){
            for(int rate:{44100,96000}){
                juce::AudioBuffer<float> input(2,15*rate);
                double dry=0;
                for(int i=0;i<input.getNumSamples();++i){
                    const double p=juce::MathConstants<double>::twoPi*220*i/rate;
                    const float x=float(.18*std::sin(p)+.07*std::sin(2*p)+.03*std::sin(3*p));
                    input.setSample(0,i,x);input.setSample(1,i,x);dry+=double(x)*x;
                }
                const auto begin=std::chrono::steady_clock::now();
                const auto wet=AudioEngine::renderSpectralWarpOffline(input,4,0,0);
                if(wet.getNumChannels()!=2||wet.getNumSamples()!=60*rate)
                    throw std::runtime_error("master extended dimensions");
                double energy=0,peak=0;int crossings=0;
                for(int i=0;i<wet.getNumSamples();++i){
                    const double l=wet.getSample(0,i),rr=wet.getSample(1,i);
                    if(!std::isfinite(l)||!std::isfinite(rr)||std::abs(l-rr)>1.e-5)
                        throw std::runtime_error("master extended stereo");
                    energy+=l*l+rr*rr;peak=std::max(peak,std::max(std::abs(l),std::abs(rr)));
                    if(i>rate*29&&i<=rate*30&&wet.getSample(0,i-1)<=0&&l>0)++crossings;
                }
                const double rms=std::sqrt(energy/(2.*wet.getNumSamples()));
                const double delta=20*std::log10(rms/std::sqrt(dry/input.getNumSamples()));
                if(!std::isfinite(delta)||std::abs(delta)>1.0||peak>.8||std::abs(crossings-220)>4)
                    throw std::runtime_error("master extended level or pitch");
                std::fprintf(stderr,"warp_master_extended,%d,60,%.4f,%.6f,%.6f,%.4f,%d,%zu\n",
                    rate,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count(),
                    rms,peak,delta,crossings,(size_t)wet.getNumSamples()*2*sizeof(float));
            }
        }
        const auto rawRhythm=AudioEngine::analyseRawClipRhythm(beat);
        if(rawRhythm.transients.size()!=8||std::abs(rawRhythm.bpm-120)>1)throw std::runtime_error("raw async rhythm kernel");
        // The primary and additional paths must use the same Warp renderer.
        for(int mode=0;mode<=6;++mode){
            auto warped=stretched;warped.warp.enabled=true;warped.warp.mode=mode;warped.warp.count=2;
            warped.warp.source[1]=1;warped.warp.target[1]=1.25;warped.lengthSeconds=1.25;
            warped.clipGain=.6f;warped.fadeInSeconds=.02;warped.pitchSemitones=3;
            if(!engine.replaceArrangementAudioClips(0,{warped}))throw std::runtime_error("primary warp");
            juce::AudioBuffer<float> primary(2,60000),extra(2,60000);
            // Compare identical fallback DSP paths without an asynchronous FFT
            // cache becoming ready between renders; other tests retain spectral.
            if(!engine.renderOfflineForTesting(primary,48000,128,false))
                throw std::runtime_error("primary deterministic warp render");
            if(!engine.replaceArrangementAudioClips(0,{})||!engine.replaceAudioClips(0,{warped}))throw std::runtime_error("extra warp");
            if(!engine.renderOfflineForTesting(extra,48000,128,false))
                throw std::runtime_error("extra deterministic warp render");
            for(int i=0;i<60000;++i)near(primary.getSample(0,i),extra.getSample(0,i));
        }
        // Mixed harmonic and impulsive fixture, deterministic fallback renderer.
        auto complex=std::make_shared<juce::AudioBuffer<float>>(2,48000);
        for(int i=0;i<48000;++i){
            const double phase=2*juce::MathConstants<double>::pi*i/48000;
            const float pulse=i%12000<120?float(.1*(1.0-(i%12000)/120.0)):0.f;
            complex->setSample(0,i,pulse+float(.08*std::sin(220*phase)+.05*std::sin(330*phase)+.03*std::sin(440*phase)));
            complex->setSample(1,i,pulse+float(.08*std::sin(220*phase)+.05*std::sin(440*phase)+.03*std::sin(550*phase)));
        }
        for(int mode=0;mode<=6;++mode){
            auto item=repitched;item.sourceBuffer=complex;item.warp.mode=mode;item.lengthSeconds=.75;item.warp.target[1]=.75;
            juce::XmlElement xmlMode("Clip");AudioEngine::writeAudioClipProperties(xmlMode,item);
            auto restoredMode=AudioEngine::readAudioClipProperties(xmlMode);
            if(restoredMode.warp.mode!=mode||restoredMode.warp.target[1]!=.75)throw std::runtime_error("warp mode persistence");
            restoredMode.sourceBuffer=complex;restoredMode.sourceSampleRate=48000;
            if(!engine.replaceArrangementAudioClips(0,{restoredMode}))throw std::runtime_error("complex warp setup");
            juce::AudioBuffer<float> out(2,36000);
            engine.resetCpuPerformanceWhileStopped();
            if(!engine.renderOfflineForTesting(out,48000,128,false))throw std::runtime_error("complex warp render");
            double energy=0;float peak=0;
            for(int ch=0;ch<2;++ch)for(int i=0;i<36000;++i){
                const float x=out.getSample(ch,i);
                if(!std::isfinite(x))throw std::runtime_error("complex warp nonfinite");
                energy+=double(x)*x;peak=std::max(peak,std::abs(x));
            }
            if(energy<.001||peak>1.f)throw std::runtime_error("complex warp signal bounds");
            const auto perf=engine.getCpuPerformance();
            std::fprintf(stderr,"warp_quality,%d,%.4f,%.4f,%llu,%.6f,%.6f\n",mode,perf.averagePercent(),perf.peakPercent(),
                (unsigned long long)perf.overruns,std::sqrt(energy/72000),double(peak));
        }
        // Multi-marker metadata roundtrip and deterministic audio comparison.
        // This exercises clip XML, not a complete saved project with media.
        AudioEngine::AudioClipMetadata multi;
        if(!engine.prepareQuantizedArrangementClip(groove,.25,1.0,multi))
            throw std::runtime_error("perfection marker preparation");
        juce::XmlElement multiXml("Clip");
        AudioEngine::writeAudioClipProperties(multiXml,multi);
        auto multiRestored=AudioEngine::readAudioClipProperties(multiXml);
        if(multiRestored.warp.count!=multi.warp.count||
           multiRestored.warp.mode!=multi.warp.mode||
           multiRestored.warp.enabled!=multi.warp.enabled)
            throw std::runtime_error("perfection marker metadata");
        for(int i=0;i<multi.warp.count;++i){
            if(std::abs(multiRestored.warp.source[i]-multi.warp.source[i])>1.e-8||
               std::abs(multiRestored.warp.target[i]-multi.warp.target[i])>1.e-8)
                throw std::runtime_error("perfection marker coordinate");
        }
        multiRestored.sourceBuffer=multi.sourceBuffer;
        multiRestored.sourceSampleRate=multi.sourceSampleRate;
        juce::AudioBuffer<float> markerA(2,192000),markerB(2,192000);
        if(!engine.replaceArrangementAudioClips(0,{multi})||
           !engine.renderOfflineForTesting(markerA,48000,128,false)||
           !engine.replaceArrangementAudioClips(0,{multiRestored})||
           !engine.renderOfflineForTesting(markerB,48000,128,false))
            throw std::runtime_error("perfection marker playback");
        for(int ch=0;ch<2;++ch)for(int i=0;i<192000;++i)
            near(markerA.getSample(ch,i),markerB.getSample(ch,i));
        for(int i=1;i<multi.warp.count-1;++i){
            const int center=(int)std::llround(multi.warp.target[i]*48000);
            double jump=0;
            for(int j=std::max(1,center-128);j<std::min(192000,center+128);++j)
                jump=std::max(jump,std::abs(double(markerA.getSample(0,j))-markerA.getSample(0,j-1)));
            std::fprintf(stderr,"warp_perfection_junction,%d,%.6f\n",i,jump);
        }
        // Phase 2.3.11: continuous, non-impulsive multi-marker fixture.
        // Separate the junction discontinuities from genuine drum onsets.
        // Keep this diagnostic off the production signal path.
        if(std::getenv("LIBERTY_WARP_EXTENDED")){
            auto certificationEngine=std::make_unique<AudioEngine>();
            auto continuous=std::make_shared<juce::AudioBuffer<float>>(2,192000);
            for(int i=0;i<192000;++i){
                const double p=juce::MathConstants<double>::twoPi*i/48000.;
                continuous->setSample(0,i,float(.12*std::sin(223*p)+.04*std::sin(447*p)));
                continuous->setSample(1,i,float(.10*std::sin(227*p)+.03*std::sin(457*p)));
            }
            for(int mode=0;mode<=6;++mode){
                auto c=multi;c.id="warp-cert-continuous";c.startSeconds=0;c.sourceBuffer=continuous;
                c.sourceSampleRate=48000;c.lengthSeconds=4;c.warp.mode=mode;
                c.trimStartFraction=0;c.trimEndFraction=1;c.offsetSeconds=0;
                c.clipGain=1;c.pitchSemitones=0;c.formant=0;c.reverse=false;c.muted=false;
                c.fadeInSeconds=c.fadeOutSeconds=0;c.spectralCache.reset();
                if(!certificationEngine->replaceArrangementAudioClips(0,{c}))
                    throw std::runtime_error("certification continuous publication");
                juce::AudioBuffer<float> rendered(2,192000);
                certificationEngine->resetCpuPerformanceWhileStopped();
                if(!certificationEngine->renderOfflineForTesting(rendered,48000,128,false))
                    throw std::runtime_error("certification continuous render");
                const auto perf=certificationEngine->getCpuPerformance();
                double energy=0,peak=0,interiorJump=0,junctionJump=0;
                for(int ch=0;ch<2;++ch)for(int i=0;i<192000;++i){
                    const double x=rendered.getSample(ch,i);
                    if(!std::isfinite(x))throw std::runtime_error("certification continuous nonfinite");
                    energy+=x*x;peak=std::max(peak,std::abs(x));
                    if(i==0)continue;
                    bool atMarker=false;
                    for(int m=1;m<c.warp.count-1;++m)
                        if(std::abs(i-(int)std::llround(c.warp.target[m]*48000))<=128){atMarker=true;break;}
                    const double d=std::abs(x-rendered.getSample(ch,i-1));
                    if(atMarker)junctionJump=std::max(junctionJump,d);
                    else if(i>2048&&i<189952)interiorJump=std::max(interiorJump,d);
                }
                if(energy<.001||peak>1.)throw std::runtime_error("certification continuous signal bounds");
                // Report the excess relative to non-junction samples. This is
                // a diagnostic, not a listening-based certification threshold.
                std::fprintf(stderr,"warp_cert_junction,%d,%.8f,%.8f,%.8f,%.6f,%.4f,%.4f,%llu\n",
                    mode,junctionJump,interiorJump,junctionJump/std::max(1.e-12,interiorJump),
                    std::sqrt(energy/384000.),perf.averagePercent(),perf.peakPercent(),
                    (unsigned long long)perf.overruns);
            }
        }
        return true;
    }catch(const std::exception& e){std::fprintf(stderr,"AudioClipPlaybackSmoke: %s\n",e.what());return false;}
}
