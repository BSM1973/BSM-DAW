#pragma once
#include "AudioEngine.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <cmath>
#include <stdexcept>
inline bool runLibertyCpuPerformanceSmoke()
{
    try {
        // A Windows GUI process may not have valid CRT stderr handles.
        // Record breadcrumbs to a file independently of console redirection.
        std::ofstream trace("liberty-cpu-benchmark-trace.txt", std::ios::app);
        trace << "benchmark entered" << std::endl;
        // Keep the large analysis ring and engine state off the Windows thread stack.
        std::fprintf(stderr,"CPU benchmark: constructing engine (size=%zu bytes)\n",sizeof(AudioEngine));
        std::fflush(stderr);
        auto ownedEngine=std::make_unique<AudioEngine>();
        auto& engine=*ownedEngine;
        trace << "engine constructed, bytes=" << sizeof(AudioEngine) << std::endl;
        std::fprintf(stderr,"CPU benchmark: engine constructed\n");
        std::fflush(stderr);
        auto source=std::make_shared<juce::AudioBuffer<float>>(1,48000);
        for(int i=0;i<48000;++i)source->setSample(0,i,.12f*std::sin(2.0*juce::MathConstants<double>::pi*440.0*i/48000.0));
        AudioEngine::AudioClipMetadata clip;
        clip.id="cpu-fixture";clip.lengthSeconds=1.0;clip.sourceSampleRate=48000;clip.sourceBuffer=source;
        if(!engine.replaceAudioClips(0,{clip}))throw std::runtime_error("fixture publication");
        trace << "fixture published" << std::endl;
        std::printf("rate,frames,blocks,average_percent,peak_percent,deadline_overruns,track0_percent,uninstrumented_wall_ms,instrumented_wall_ms,max_difference\n");
        for(const double rate:{44100.0,48000.0,96000.0,192000.0})
            for(const int frames:{32,64,128,512,1024}){
                std::fprintf(stderr,"CPU benchmark: %.0f Hz / %d frames\n",rate,frames);
                std::fflush(stderr);
                trace << rate << " Hz / " << frames << " frames: begin" << std::endl;
                const int count=static_cast<int>(rate/4.0);
                juce::AudioBuffer<float> reference(2,count),measured(2,count);
                engine.setCpuProfiling(false);engine.resetCpuPerformanceWhileStopped();
                if(!engine.renderOfflineForTesting(reference,rate,frames,false))throw std::runtime_error("warmup");
                if(engine.getPreparedScratchFrames()<frames||engine.getOversizedCallbackCount()!=0||engine.isAudioDeviceRunning())throw std::runtime_error("scratch or device state");
                trace << "warmup passed" << std::endl;
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
                trace << "metrics," << rate << ',' << frames << ',' << total.blocks << ','
                      << total.averagePercent() << ',' << total.peakPercent() << ',' << total.overruns
                      << ',' << track.averagePercent() << ',' << refMs << ',' << measuredMs << ',' << diff
                      << std::endl;
                trace << "scenario passed" << std::endl;
                std::printf("%.0f,%d,%llu,%.4f,%.4f,%llu,%.4f,%.4f,%.4f,%.9f\n",rate,frames,(unsigned long long)total.blocks,total.averagePercent(),total.peakPercent(),(unsigned long long)total.overruns,track.averagePercent(),refMs,measuredMs,diff);
            }
        // Four independent audio lanes, two rates, two low-latency block sizes.
        for(int lane=1;lane<4;++lane){
            auto next=clip;next.id=juce::String("cpu-lane-")+juce::String(lane);
            if(!engine.replaceAudioClips(lane,{next}))throw std::runtime_error("multitrack fixture");
        }
        for(const double rate:{44100.0,48000.0}){
            const int count=static_cast<int>(rate/2.0);
            juce::AudioBuffer<float> ref(2,count),out(2,count);
            if(!engine.renderOfflineForTesting(ref,rate,512,false))throw std::runtime_error("multitrack reference");
            for(const int frames:{32,64}){
                engine.resetCpuPerformanceWhileStopped();
                if(!engine.renderOfflineForTesting(out,rate,frames,false))throw std::runtime_error("multitrack low latency");
                float diff=0;
                for(int ch=0;ch<2;++ch)for(int i=0;i<count;++i){
                    const float x=out.getSample(ch,i),y=ref.getSample(ch,i);
                    if(!std::isfinite(x)||!std::isfinite(y))throw std::runtime_error("multitrack nonfinite");
                    diff=(std::max)(diff,std::abs(x-y));
                }
                if(diff>0.00002f||out.getMagnitude(0,count)<.01f||engine.getOversizedCallbackCount()!=0)
                    throw std::runtime_error("multitrack parity");
                const auto stats=engine.getCpuPerformance();
                trace << "low_latency_4_tracks," << rate << ',' << frames << ',' << stats.averagePercent()
                      << ',' << stats.peakPercent() << ',' << stats.overruns << ',' << diff << std::endl;
            }
        }
        trace << "all scenarios completed" << std::endl;
        std::fflush(stdout);
        std::fprintf(stderr,"CPU benchmark: all scenarios completed\n");
        std::fflush(stderr);
        return true;
    }catch(const std::exception& e){std::fprintf(stderr,"CPU performance: %s\n",e.what());return false;}
}
