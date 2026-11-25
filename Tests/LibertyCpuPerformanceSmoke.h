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
        // Compare actual engine callback output using 16..128 independent lanes.
        // Low gain prevents clipping from obscuring differences in summation.
        const unsigned workers=(std::min)(4u,(std::max)(1u,std::thread::hardware_concurrency()>1?std::thread::hardware_concurrency()-1:1u));
        for(int lanes:{16,32,64,128}){
            while(engine.getAudioTrackCount()<lanes){
                const int lane=engine.addAudioTrack();
                if(lane<0)throw std::runtime_error("multicore add lane");
                auto next=clip;next.id=juce::String("mc-")+juce::String(lane);
                if(!engine.replaceAudioClips(lane,{next}))throw std::runtime_error("multicore fixture");
            }
            for(int lane=0;lane<lanes;++lane)engine.setTrackGain(lane,1.0f/float(lanes));
            for(int frames:{32,64,128,512}){
                const int count=4800;
                juce::AudioBuffer<float> serial(2,count),parallel(2,count);
                engine.setCpuProfiling(true,false);
                if(!engine.configureOfflineMulticoreForTesting(0))throw std::runtime_error("serial configure");
                engine.resetCpuPerformanceWhileStopped();
                if(!engine.renderOfflineForTesting(serial,48000,frames,false))throw std::runtime_error("serial render");
                const auto serialStats=engine.getCpuPerformance();
                if(!engine.configureOfflineMulticoreForTesting(workers))throw std::runtime_error("parallel configure");
                engine.resetCpuPerformanceWhileStopped();
                if(!engine.renderOfflineForTesting(parallel,48000,frames,false))throw std::runtime_error("parallel render");
                const auto parallelStats=engine.getCpuPerformance();
                float diff=0;
                for(int ch=0;ch<2;++ch)for(int i=0;i<count;++i){
                    const float x=serial.getSample(ch,i),y=parallel.getSample(ch,i);
                    if(!std::isfinite(x)||!std::isfinite(y))throw std::runtime_error("multicore nonfinite");
                    diff=(std::max)(diff,std::abs(x-y));
                }
                const auto expected=static_cast<std::uint64_t>((count+frames-1)/frames);
                if(diff>0.00002f||serial.getMagnitude(0,count)<.01f||engine.getParallelBlocksForTesting()!=expected||
                   engine.getParallelFallbacksForTesting()!=0||parallelStats.blocks!=expected||serialStats.blocks!=expected)
                    throw std::runtime_error("multicore parity/accounting");
                trace << "multicore," << lanes << ',' << frames << ',' << workers << ','
                      << serialStats.averagePercent() << ',' << parallelStats.averagePercent() << ','
                      << serialStats.peakPercent() << ',' << parallelStats.peakPercent() << ','
                      << serialStats.overruns << ',' << parallelStats.overruns << ','
                      << engine.getWorkerJobsForTesting() << ',' << diff << std::endl;
            }
        }
        // Verify adaptive admission retains the serial path at 32/64 frames.
        for(int frames:{32,64}){
            juce::AudioBuffer<float> reference(2,4800),adaptive(2,4800);
            if(!engine.configureOfflineMulticoreForTesting(0)||!engine.renderOfflineForTesting(reference,48000,frames,false))
                throw std::runtime_error("adaptive serial reference");
            if(!engine.configureOfflineMulticoreForTesting(8,true)||!engine.renderOfflineForTesting(adaptive,48000,frames,false))
                throw std::runtime_error("adaptive small block");
            if(engine.getSelectedWorkersForTesting()!=0||engine.getParallelBlocksForTesting()!=0||
               engine.getAdaptiveSerialBlocksForTesting()!=static_cast<std::uint64_t>((4800+frames-1)/frames))
                throw std::runtime_error("adaptive admission");
            for(int ch=0;ch<2;++ch)for(int i=0;i<4800;++i)
                if(std::abs(reference.getSample(ch,i)-adaptive.getSample(ch,i))>0.00002f)
                    throw std::runtime_error("adaptive serial parity");
        }
        // 60 seconds of actual non-silent audio per render, not a wall-clock sleep.
        constexpr int stressRate=48000,stressFrames=128,stressSamples=60*stressRate;
        auto longSource=std::make_shared<juce::AudioBuffer<float>>(1,stressSamples);
        for(int i=0;i<stressSamples;++i)longSource->setSample(0,i,source->getSample(0,i%48000));
        for(int lane=0;lane<128;++lane){
            auto next=clip;next.id=juce::String("long-")+juce::String(lane);
            next.lengthSeconds=60.0;next.sourceBuffer=longSource;
            if(!engine.replaceAudioClips(lane,{next}))throw std::runtime_error("stress fixture");
        }
        trace << "stress 128 tracks 60 seconds serial begin" << std::endl;
        juce::AudioBuffer<float> stressReference(2,stressSamples),stressAdaptive(2,stressSamples);
        if(!engine.configureOfflineMulticoreForTesting(0))throw std::runtime_error("stress serial configure");
        engine.resetCpuPerformanceWhileStopped();
        if(!engine.renderOfflineForTesting(stressReference,stressRate,stressFrames,false))throw std::runtime_error("stress serial");
        const auto stressSerial=engine.getCpuPerformance();
        trace << "stress 128 tracks 60 seconds adaptive begin" << std::endl;
        if(!engine.configureOfflineMulticoreForTesting(8,true))throw std::runtime_error("stress adaptive configure");
        engine.resetCpuPerformanceWhileStopped();
        if(!engine.renderOfflineForTesting(stressAdaptive,stressRate,stressFrames,false))throw std::runtime_error("stress adaptive");
        const auto stressParallel=engine.getCpuPerformance();
        const auto expectedStress=static_cast<std::uint64_t>(stressSamples/stressFrames);
        if(stressSerial.blocks!=expectedStress||stressParallel.blocks!=expectedStress||
            engine.getParallelBlocksForTesting()+engine.getAdaptiveSerialBlocksForTesting()!=expectedStress||
            std::abs(engine.getCurrentTimeSeconds()-60.0)>0.001)
            throw std::runtime_error("stress accounting");
        float stressDiff=0;
        for(int ch=0;ch<2;++ch)for(int i=0;i<stressSamples;++i){
            const float a=stressReference.getSample(ch,i),b=stressAdaptive.getSample(ch,i);
            if(!std::isfinite(a)||!std::isfinite(b))throw std::runtime_error("stress nonfinite");
            stressDiff=(std::max)(stressDiff,std::abs(a-b));
        }
        if(stressDiff>0.00002f||stressAdaptive.getMagnitude(0,stressSamples-stressRate,stressRate)<.01f)
            throw std::runtime_error("stress parity or truncated audio");
        trace << "stress60,128,128," << engine.getSelectedWorkersForTesting() << ','
              << stressSerial.averagePercent() << ',' << stressParallel.averagePercent() << ','
              << stressSerial.peakPercent() << ',' << stressParallel.peakPercent() << ','
              << stressSerial.overruns << ',' << stressParallel.overruns << ','
              << engine.getParallelBlocksForTesting() << ',' << engine.getAdaptiveSerialBlocksForTesting() << ','
              << engine.getWorkerJobsForTesting() << ',' << stressDiff << std::endl;
        engine.configureOfflineMulticoreForTesting(0);
        LibertyRealtime::Plan plan;
        if(!engine.buildRealtimePlanForTesting(plan)||plan.size<257||plan.master!=plan.size-1)
            throw std::runtime_error("engine realtime dependency plan");
        trace << "realtime_plan," << plan.size << ',' << plan.latency << std::endl;
        trace << "all scenarios completed" << std::endl;
        std::fflush(stdout);
        std::fprintf(stderr,"CPU benchmark: all scenarios completed\n");
        std::fflush(stderr);
        return true;
    }catch(const std::exception& e){std::fprintf(stderr,"CPU performance: %s\n",e.what());return false;}
}
