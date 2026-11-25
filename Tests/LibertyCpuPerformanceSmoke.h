#pragma once
#include "AudioEngine.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <stdexcept>
inline bool runLibertyCpuPerformanceSmoke()
{
    try {
        // Keep the large analysis ring and engine state off the Windows thread stack.
        std::fprintf(stderr,"CPU benchmark: constructing engine (size=%zu bytes)\n",sizeof(AudioEngine));
        std::fflush(stderr);
        auto ownedEngine=std::make_unique<AudioEngine>();
        auto& engine=*ownedEngine;
        std::fprintf(stderr,"CPU benchmark: engine constructed\n");
        std::fflush(stderr);
        auto source=std::make_shared<juce::AudioBuffer<float>>(1,48000);
        for(int i=0;i<48000;++i)source->setSample(0,i,.12f*std::sin(2.0*juce::MathConstants<double>::pi*440.0*i/48000.0));
        AudioEngine::AudioClipMetadata clip;
        clip.id="cpu-fixture";clip.lengthSeconds=1.0;clip.sourceSampleRate=48000;clip.sourceBuffer=source;
        if(!engine.replaceAudioClips(0,{clip}))throw std::runtime_error("fixture publication");
        std::printf("rate,frames,blocks,average_percent,peak_percent,deadline_overruns,track0_percent,uninstrumented_wall_ms,instrumented_wall_ms,max_difference\n");
        for(const double rate:{44100.0,48000.0,96000.0,192000.0})
            for(const int frames:{32,128,512,1024}){
                std::fprintf(stderr,"CPU benchmark: %.0f Hz / %d frames\n",rate,frames);
                std::fflush(stderr);
                const int count=static_cast<int>(rate/4.0);
                juce::AudioBuffer<float> reference(2,count),measured(2,count);
                engine.setCpuProfiling(false);engine.resetCpuPerformanceWhileStopped();
                if(!engine.renderOfflineForTesting(reference,rate,frames,false))throw std::runtime_error("warmup");
                auto start=LibertyCpu::Clock::now();
                if(!engine.renderOfflineForTesting(reference,rate,frames,false))throw std::runtime_error("reference");
                const double refMs=std::chrono::duration<double,std::milli>(LibertyCpu::Clock::now()-start).count();
                if(engine.getCpuPerformance().blocks!=0)throw std::runtime_error("disabled profiler counted");
                engine.setCpuProfiling(true,true);start=LibertyCpu::Clock::now();
                if(!engine.renderOfflineForTesting(measured,rate,frames,false))throw std::runtime_error("measured");
                const double measuredMs=std::chrono::duration<double,std::milli>(LibertyCpu::Clock::now()-start).count();
                const auto total=engine.getCpuPerformance(),track=engine.getTrackCpuPerformance(0,false);
                const auto expected=static_cast<std::uint64_t>((count+frames-1)/frames);
                if(total.blocks!=expected||track.blocks!=expected||!total.budgetNs||!total.elapsedNs||total.overruns>total.blocks||track.elapsedNs>total.elapsedNs)throw std::runtime_error("callback accounting");
                float diff=0;
                for(int ch=0;ch<2;++ch)for(int i=0;i<count;++i){
                    const float a=reference.getSample(ch,i),b=measured.getSample(ch,i);
                    if(!std::isfinite(a)||!std::isfinite(b))throw std::runtime_error("nonfinite signal");
                    diff=(std::max)(diff,std::abs(a-b));
                }
                if(diff>0.000002f||measured.getMagnitude(0,count)<.01f)throw std::runtime_error("signal parity");
                std::printf("%.0f,%d,%llu,%.4f,%.4f,%llu,%.4f,%.4f,%.4f,%.9f\n",rate,frames,(unsigned long long)total.blocks,total.averagePercent(),total.peakPercent(),(unsigned long long)total.overruns,track.averagePercent(),refMs,measuredMs,diff);
            }
        std::fflush(stdout);
        std::fprintf(stderr,"CPU benchmark: all scenarios completed\n");
        std::fflush(stderr);
        return true;
    }catch(const std::exception& e){std::fprintf(stderr,"CPU performance: %s\n",e.what());return false;}
}
