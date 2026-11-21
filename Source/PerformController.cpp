#define private public
#include "MainComponent.h"
#undef private

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "PerformSessionTiming.h"
#include "PerformClipSettings.h"
#include "PerformMidiEditorOps.h"
#include "PerformMidiGenerator.h"
#include "PerformLivePro.h"
#include "NoOverlapLayout.h"
#include "LibertyDesignSystem.h"
#include "StepSequencer.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <algorithm>
#include <cstdint>
#include <cmath>

bool isLibertyMixConsoleVisible(MainComponent* owner);
void setLibertyMixConsoleVisible(MainComponent* owner, bool shouldShow);
bool commitLibertyPerformCapturedInstrumentClip(MainComponent&,
    const std::vector<MidiEngine::NoteEvent>&,int,double,double);
int getLibertyTrackColourId(int track);
juce::String getLibertyTrackName(int track);

namespace
{
constexpr int transportHeight = 76;
constexpr int sceneCount = 8;

juce::Colour trackColour(int id)
{
    static constexpr std::array<juce::uint32, 9> colours {
        0xff31506a, 0xff3b82f6, 0xff22c55e, 0xffeab308, 0xfff97316,
        0xffef4444, 0xffa855f7, 0xffec4899, 0xff14b8a6
    };
    return juce::Colour(colours[(size_t)juce::jlimit(0, 8, id)]);
}

bool isSupportedAudioFile(const juce::File& file)
{
    const auto ext = file.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aif" || ext == ".aiff";
}

class PerformAudioPlayer final : private juce::AudioIODeviceCallback
{
public:
    explicit PerformAudioPlayer(MainComponent& ownerIn) : owner(ownerIn)
    {
        formats.registerBasicFormats();
        tracks.resize((size_t)owner.getTotalArrangeTrackCount());
        // AudioEngine is the sole owner of the hardware output callback.
        callbackAttached = false;
    }

    ~PerformAudioPlayer() override
    {
        if (callbackAttached)
        {
            owner.audioEngine.getDeviceManager().removeAudioCallback(this);
            callbackAttached = false;
        }
        const juce::ScopedLock sl(lock);
        stopCaptureInternal();
    }

    void syncTrackCount()
    {
        const juce::ScopedLock sl(lock);
        tracks.resize((size_t)juce::jmax(0,owner.getTotalArrangeTrackCount()));
    }
    void insertNewAudioTrackAt(int index)
    {
        const juce::ScopedLock sl(lock);
        if(index>=0 && index<=(int)tracks.size())
            tracks.insert(tracks.begin()+index,TrackState{});
    }

    void setEnabled(bool shouldEnable)
    {
        syncTrackCount();
        const juce::ScopedLock sl(lock);
        enabled.store(shouldEnable,std::memory_order_release);
        if(!shouldEnable)
        {
            for(auto& track:tracks)
            {
                track.activeScene=-1;
                track.queuedScene=-2;
                track.panicNext=true;
            }
            sessionSamples=0;
            owner.audioEngine.notifyInstrumentChanged();
        }
    }

    bool prepareAudioClip(int track, int scene, const juce::File& file, juce::String& error)
    {
        if(track<0 || track>=owner.getAudioTrackCount() || scene<0 || scene>=sceneCount
            || !file.existsAsFile()) {error="Fichier audio introuvable.";return false;}
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if(!reader || reader->lengthInSamples<=0 || reader->lengthInSamples>0x7fffffff)
        {error="Fichier audio non pris en charge.";return false;}
        const int channels=juce::jlimit(1,2,(int)reader->numChannels);
        const int n=(int)reader->lengthInSamples;
        juce::AudioBuffer<float> source(channels,n);
        source.clear();
        if(!reader->read(&source,0,n,0,true,true))
        {error="Lecture audio impossible.";return false;}
        const double deviceRate=juce::jmax(1.0,owner.audioEngine.getSampleRate());
        const double sourceRate=juce::jmax(1.0,reader->sampleRate);
        const int target=(int)juce::jlimit(1.0,(double)0x7fffffff,
            std::round((double)n*deviceRate/sourceRate));
        auto buffer=std::make_shared<juce::AudioBuffer<float>>(channels,target);
        buffer->clear();
        if(std::abs(sourceRate-deviceRate)<0.5)
            for(int ch=0;ch<channels;++ch)buffer->copyFrom(ch,0,source,ch,0,target);
        else
            for(int ch=0;ch<channels;++ch)
            {
                juce::LagrangeInterpolator converter;
                converter.process(sourceRate/deviceRate,source.getReadPointer(ch),
                                  buffer->getWritePointer(ch),target);
            }
        syncTrackCount();
        const juce::ScopedLock sl(lock);
        if(track>=(int)tracks.size())return false;
        auto& state=tracks[(size_t)track];
        auto& slot=state.slots[(size_t)scene];
        slot.audio=std::move(buffer);
        slot.notes.clear();
        slot.midiLengthTicks=0;
        slot.sourceMidiLengthTicks=0;
        slot.settings={};
        slot.audioFirst=0;
        slot.audioLength=slot.audio->getNumSamples();
        slot.linearGain=1.0f;
        slot.valid=true;
        if(state.activeScene==scene) {state.activeScene=-1;state.panicNext=true;}
        return true;
    }

    bool prepareMidiClip(int track, int scene,
                         const std::vector<MidiEngine::NoteEvent>& notes,
                         std::int64_t lengthTicks)
    {
        if(track<owner.getAudioTrackCount() || track>=owner.getTotalArrangeTrackCount()
            || scene<0 || scene>=sceneCount || lengthTicks<=0)return false;
        syncTrackCount();
        const juce::ScopedLock sl(lock);
        if(track>=(int)tracks.size())return false;
        auto& state=tracks[(size_t)track];
        auto& slot=state.slots[(size_t)scene];
        slot.audio.reset();
        slot.notes=notes;
        slot.midiLengthTicks=lengthTicks;
        slot.sourceMidiLengthTicks=lengthTicks;
        slot.settings={};
        slot.audioFirst=slot.audioLength=0;
        slot.linearGain=1.0f;
        slot.valid=true;
        if(state.activeScene==scene){state.activeScene=-1;state.panicNext=true;}
        return true;
    }

    bool replaceMidiNotes(int track,int scene,
                          const std::vector<MidiEngine::NoteEvent>& edited,
                          std::int64_t lengthTicks,int numerator,int denominator)
    {
        if(lengthTicks<=0 || edited.size()>8192)return false;
        const juce::ScopedLock sl(lock);
        if(track<0 || track>=(int)tracks.size() ||
           scene<0 || scene>=sceneCount)return false;
        auto& state=tracks[(size_t)track];
        auto& slot=state.slots[(size_t)scene];
        if(!slot.valid || slot.audio)return false;
        slot.notes=edited;
        slot.sourceMidiLengthTicks=lengthTicks;
        slot.midiLengthTicks=LibertyPerformClip::midiLengthTicks(
            lengthTicks,slot.settings.midiBars,numerator,denominator);
        if(state.activeScene==scene)state.panicNext=true;
        return true;
    }

    bool updateClipSettings(int track,int scene,LibertyPerformClip::Settings parameters,
                            int numerator,int denominator)
    {
        parameters=LibertyPerformClip::validated(parameters);
        const juce::ScopedLock sl(lock);
        if(track<0 || track>=(int)tracks.size() || scene<0 || scene>=sceneCount)return false;
        auto& state=tracks[(size_t)track];
        auto& slot=state.slots[(size_t)scene];
        if(!slot.valid)return false;
        const auto previous=slot.settings;
        slot.settings=parameters;
        slot.linearGain=LibertyPerformClip::linearGain(parameters.gainDb);
        if(slot.audio)
        {
            const auto range=LibertyPerformClip::audioSlice(
                slot.audio->getNumSamples(),parameters);
            slot.audioFirst=range.first;
            slot.audioLength=range.length;
        }
        else
            slot.midiLengthTicks=LibertyPerformClip::midiLengthTicks(
                slot.sourceMidiLengthTicks,parameters.midiBars,numerator,denominator);
        if(state.activeScene==scene &&
           (parameters.transpose!=previous.transpose ||
            parameters.midiBars!=previous.midiBars ||
            parameters.loop!=previous.loop))
            state.panicNext=true;
        return true;
    }

    bool hasClip(int track,int scene) const
    {
        const juce::ScopedLock sl(lock);
        return track>=0 && track<(int)tracks.size() && scene>=0 && scene<sceneCount
            && tracks[(size_t)track].slots[(size_t)scene].valid;
    }

    struct CaptureEvent
    {
        int track=-1,scene=-1;
        std::int64_t atSample=0; // session clock, relative to startCapture()
        std::int64_t sourceOffsetSamples=0; // clip playhead at capture start
    };
    std::int64_t getSessionSamples() const
    {
        const juce::ScopedLock sl(lock);
        return sessionSamples;
    }
    std::int64_t nextQuantizedSample(int quantize,double bpm,int numerator,int denominator)
    {
        const juce::ScopedLock sl(lock);
        const double rate=juce::jmax(1.0,owner.audioEngine.getSampleRate());
        const auto quantum=LibertyPerformTiming::quantumSamples(
            quantize,bpm,numerator,denominator,rate);
        return LibertyPerformTiming::nextBoundary(sessionSamples,quantum);
    }
    void setTransitionMilliseconds(int ms) noexcept
    {transitionMs.store(juce::jlimit(0,1000,ms),std::memory_order_release);}
    void setLiveAudioGain(float gain) noexcept
    {audioMacroGain.store(juce::jlimit(0.f,2.f,gain),std::memory_order_release);}
    void setLiveInstrumentGain(float gain) noexcept
    {instrumentMacroGain.store(juce::jlimit(0.f,2.f,gain),std::memory_order_release);}
    float getLiveInstrumentGain() const noexcept
    {return instrumentMacroGain.load(std::memory_order_acquire);}
    void setLiveMasterGain(float gain) noexcept
    {masterMacroGain.store(juce::jlimit(0.f,2.f,gain),std::memory_order_release);}

    void queueScene(int track,int scene,int quantize,double bpm,int numerator,int denominator)
    {
        const juce::ScopedLock sl(lock);
        if(track<0 || track>=(int)tracks.size() || scene>=sceneCount || scene< -1)return;
        auto& state=tracks[(size_t)track];
        if(scene>=0 && !state.slots[(size_t)scene].valid)return;
        const double rate=juce::jmax(1.0,owner.audioEngine.getSampleRate());
        const auto q=LibertyPerformTiming::quantumSamples(quantize,bpm,numerator,denominator,rate);
        sessionTempo.store(juce::jmax(1.0,bpm),std::memory_order_release);
        state.queuedAt=LibertyPerformTiming::nextBoundary(sessionSamples,q);
        state.queuedScene=scene;
    }

    void stopTrack(int track)
    {
        const juce::ScopedLock sl(lock);
        if(track<0 || track>=(int)tracks.size())return;
        auto& s=tracks[(size_t)track];
        if(s.activeScene>=0)
        {
            s.fadeFromScene=s.activeScene;
            s.fadeFromStarted=s.startedAt;
            s.fadeStart=sessionSamples;
            s.fadeLength=LibertyPerformLive::transitionSamples(
                transitionMs.load(),juce::jmax(1.0,owner.audioEngine.getSampleRate()));
            if(captureActive.load() && captureCount<(int)captureEvents.size())
                captureEvents[(size_t)captureCount++]={track,-1,
                    juce::jmax<std::int64_t>(0,sessionSamples-captureStartSample)};
        }
        s.activeScene=-1;s.queuedScene=-2;s.panicNext=true;
    }
    void stopAll()
    {
        const juce::ScopedLock sl(lock);
        for(int i=0;i<(int)tracks.size();++i)
        {
            auto& s=tracks[(size_t)i];
            if(captureActive.load() && s.activeScene>=0 &&
               captureCount<(int)captureEvents.size())
                captureEvents[(size_t)captureCount++]={i,-1,
                    juce::jmax<std::int64_t>(0,sessionSamples-captureStartSample)};
            if(s.activeScene>=0)
            {
                s.fadeFromScene=s.activeScene;
                s.fadeFromStarted=s.startedAt;
                s.fadeStart=sessionSamples;
                s.fadeLength=LibertyPerformLive::transitionSamples(
                    transitionMs.load(),juce::jmax(1.0,owner.audioEngine.getSampleRate()));
            }
            s.activeScene=-1;s.queuedScene=-2;s.panicNext=true;
        }
    }
    void resetSession()
    {
        const juce::ScopedLock sl(lock);
        for(auto& s:tracks){s=TrackState{};}
        sessionSamples=lastBlockStart=0;
    }
    bool isTrackPlaying(int track,int scene=-1) const
    {
        const juce::ScopedLock sl(lock);
        return track>=0 && track<(int)tracks.size()
            && tracks[(size_t)track].activeScene>=0
            && (scene<0 || tracks[(size_t)track].activeScene==scene);
    }
    bool isTrackQueued(int track,int scene) const
    {
        const juce::ScopedLock sl(lock);
        return track>=0 && track<(int)tracks.size()
            && tracks[(size_t)track].queuedScene==scene;
    }
    void setLoopEnabled(bool value) noexcept { loopEnabled.store(value,std::memory_order_release); }
    void setTempoBpm(double bpm) noexcept
    {sessionTempo.store(juce::jmax(1.0,bpm),std::memory_order_release);}

    void appendMidiForInstrument(int instrument,juce::MidiBuffer& midi,int numSamples)
    {
        if(!enabled.load(std::memory_order_acquire) || numSamples<=0)return;
        if(!lock.tryEnter())return;
        const int audioTracks=owner.getAudioTrackCount();
        const int midiTracks=owner.getMidiTrackCount();
        const int instrumentTracks=owner.getInstrumentTrackCount();
        const double bpm=sessionTempo.load(std::memory_order_acquire);
        const double rate=juce::jmax(1.0,owner.audioEngine.getSampleRate());
        const std::int64_t blockStart=lastBlockStart;
        const bool repeat=loopEnabled.load(std::memory_order_acquire);

        for(int logical=audioTracks;logical<(int)tracks.size();++logical)
        {
            if(LibertyPerformTiming::instrumentForLogicalTrack(
                logical,audioTracks,midiTracks,instrumentTracks)!=instrument)continue;
            const auto& state=tracks[(size_t)logical];
            if(state.panicOffset>=0)
                for(int channel=1;channel<=16;++channel)
                {
                    midi.addEvent(juce::MidiMessage::allNotesOff(channel),state.panicOffset);
                    midi.addEvent(juce::MidiMessage::allSoundOff(channel),state.panicOffset);
                }
            const auto emit=[&](int scene,std::int64_t startedAt,int from,int to)
            {
                if(scene<0 || from>=to)return;
                const auto& slot=state.slots[(size_t)scene];
                if(!slot.valid || slot.midiLengthTicks<=0)return;
                const auto cycle=juce::jmax<std::int64_t>(1,
                    LibertyPerformTiming::ticksToSamples(slot.midiLengthTicks,bpm,rate));
                const bool clipRepeats=repeat && slot.settings.loop;
                const auto phaseStart=blockStart+from;
                const auto phaseEnd=blockStart+to;
                const auto relative=phaseStart-startedAt;
                const auto first=juce::jmax<std::int64_t>(0,relative>0?relative/cycle-1:0);
                const auto last=clipRepeats?
                    juce::jmax<std::int64_t>(0,(phaseEnd-startedAt)/cycle+1):0;
                for(std::int64_t iteration=first;iteration<=last;++iteration)
                {
                    const std::int64_t base=startedAt+iteration*cycle;
                    if(base>=phaseEnd)break;
                    for(const auto& note:slot.notes)
                    {
                        if(note.muted || note.lengthTicks<=0 || note.startTick<0)continue;
                        const auto start=base+LibertyPerformTiming::ticksToSamples(note.startTick,bpm,rate);
                        const auto end=juce::jmin(base+cycle,
                            base+LibertyPerformTiming::ticksToSamples(
                                note.startTick+note.lengthTicks,bpm,rate));
                        const int channel=juce::jlimit(1,16,(int)note.channel);
                        const int pitch=LibertyPerformClip::notePitch((int)note.pitch,
                                                                      slot.settings.transpose);
                        const float velocity=juce::jlimit(0.f,1.f,(float)note.velocity/127.f);
                        if(start>=phaseStart && start<phaseEnd)
                            midi.addEvent(juce::MidiMessage::noteOn(channel,pitch,velocity),
                                juce::jlimit(0,numSamples-1,(int)(start-blockStart)));
                        if(end>=phaseStart && end<phaseEnd && end>start)
                            midi.addEvent(juce::MidiMessage::noteOff(channel,pitch),
                                juce::jlimit(0,numSamples-1,(int)(end-blockStart)));
                    }
                    if(!clipRepeats)break;
                }
            };
            if(state.transitionOffset>=0)
            {
                emit(state.previousScene,state.previousStartedAt,0,state.transitionOffset);
                emit(state.activeScene,state.startedAt,state.transitionOffset,numSamples);
            }
            else
                emit(state.activeScene,state.startedAt,0,numSamples);
        }
        lock.exit();
    }

    bool startCapture(juce::String& error)
    {
        const juce::ScopedLock sl(lock);
        const juce::ScopedLock captureGuard(captureLock);
        if (captureActive) return true;

        const double rate = owner.audioEngine.getSampleRate();
        if (rate <= 0.0)
        {
            error = "No audio device is available.";
            return false;
        }

        auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                          .getChildFile("BSM").getChildFile("Liberty").getChildFile("Perform Captures");
        if (!folder.createDirectory().wasOk() && !folder.isDirectory())
        {
            error = "The PERFORM capture folder could not be created.";
            return false;
        }

        captureFile = folder.getNonexistentChildFile("Perform Capture " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S"), ".wav", false);
        std::unique_ptr<juce::FileOutputStream> stream(captureFile.createOutputStream());
        if (stream == nullptr)
        {
            error = "The PERFORM capture file could not be created.";
            return false;
        }

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(stream.get(), rate, 2, 24, {}, 0));
        if (writer == nullptr)
        {
            error = "The PERFORM capture writer could not be created.";
            return false;
        }
        stream.release();

        // Capture the final bus after instruments, effects and master output.
        // The scratch buffer is preallocated here, never on the audio thread.
        captureMix.setSize(2,65536,false,false,true);
        captureMix.clear();
        captureStartSample=sessionSamples;
        captureEndSample=sessionSamples;
        captureCount=0;
        for(int t=0;t<(int)tracks.size() && captureCount<(int)captureEvents.size();++t)
            if(tracks[(size_t)t].activeScene>=0)
                captureEvents[(size_t)captureCount++]={t,
                    tracks[(size_t)t].activeScene,0,
                    juce::jmax<std::int64_t>(0,
                        sessionSamples-tracks[(size_t)t].startedAt)};
        captureThread = std::make_unique<juce::TimeSliceThread>("Liberty PERFORM Capture");
        captureThread->startThread();
        captureWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(writer.release(), *captureThread, 65536);
        captureActive = true;
        return true;
    }

    juce::File stopCapture()
    {
        const juce::ScopedLock sl(lock);
        return stopCaptureInternal();
    }

    bool isCapturing() const noexcept { return captureActive.load(std::memory_order_relaxed); }

    std::vector<CaptureEvent> getCaptureEvents() const
    {
        const juce::ScopedLock sl(lock);
        return std::vector<CaptureEvent>(captureEvents.begin(),
            captureEvents.begin()+captureCount);
    }
    std::int64_t getCapturedSamples() const
    {
        const juce::ScopedLock sl(lock);
        return juce::jmax<std::int64_t>(0,captureEndSample-captureStartSample);
    }
    float captureFinalMix(float* const* output,int channels,int samples)
    {
        const float gain=masterMacroGain.load(std::memory_order_acquire);
        // 1-buffer smoothing prevents controller jumps from producing clicks.
        if(output!=nullptr && samples>0)
        {
            const float from=lastMasterMacroGain;
            const float delta=(gain-from)/(float)samples;
            for(int ch=0;ch<juce::jmin(channels,2);++ch)
                if(output[ch]!=nullptr)
                    for(int i=0;i<samples;++i)
                        output[ch][i]*=from+delta*(i+1);
            lastMasterMacroGain=gain;
        }
        // GUI clip editing must not make us drop recorded output blocks.
        if(!captureActive.load(std::memory_order_acquire) || samples<=0 ||
           output==nullptr || !captureLock.tryEnter())return gain;
        if(captureWriter!=nullptr)
        {
            int offset=0;
            while(offset<samples)
            {
                const int count=juce::jmin(samples-offset,captureMix.getNumSamples());
                for(int ch=0;ch<2;++ch)
                {
                    captureMix.clear(ch,0,count);
                    if(ch<channels && output[ch]!=nullptr)
                        captureMix.copyFrom(ch,0,output[ch]+offset,count);
                    else if(channels>0 && output[0]!=nullptr)
                        captureMix.copyFrom(ch,0,output[0]+offset,count);
                }
                captureWriter->write(captureMix.getArrayOfReadPointers(),count);
                offset+=count;
            }
        }
        captureLock.exit();
        return gain;
    }

    void renderAudioBlock(float* const* outputs, int numOutputs, int numSamples)
    {
        audioDeviceIOCallbackWithContext(nullptr, 0, outputs, numOutputs, numSamples, {});
    }

private:
    struct Slot
    {
        std::shared_ptr<juce::AudioBuffer<float>> audio;
        std::vector<MidiEngine::NoteEvent> notes;
        std::int64_t midiLengthTicks=0,sourceMidiLengthTicks=0;
        LibertyPerformClip::Settings settings;
        int audioFirst=0,audioLength=0;
        float linearGain=1.0f;
        bool valid=false;
    };
    struct TrackState
    {
        std::array<Slot,sceneCount> slots;
        int activeScene=-1;
        int queuedScene=-2; // -2 means no pending action, -1 means stop.
        std::int64_t queuedAt=0;
        std::int64_t startedAt=0;
        int previousScene=-1;
        std::int64_t previousStartedAt=0;
        int transitionOffset=-1;
        int fadeFromScene=-1;
        std::int64_t fadeFromStarted=0,fadeStart=0,fadeLength=0;
        int panicOffset=-1;
        bool panicNext=false;
    };

    juce::File stopCaptureInternal()
    {
        const juce::ScopedLock captureGuard(captureLock);
        captureEndSample=sessionSamples;
        captureActive.store(false, std::memory_order_relaxed);
        captureWriter.reset();
        if (captureThread != nullptr)
        {
            captureThread->stopThread(2000);
            captureThread.reset();
        }
        return captureFile;
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        const int block = device != nullptr ? juce::jmax(32, device->getCurrentBufferSizeSamples()) : 512;
        captureMix.setSize(2, block, false, false, true);
    }

    void audioDeviceStopped() override
    {
        const juce::ScopedLock sl(lock);
        for (auto& track : tracks) { track.activeScene = -1; track.queuedScene = -2; track.panicNext = true; }
        sessionSamples = lastBlockStart = 0;
        if (captureActive.load(std::memory_order_relaxed))
            stopCaptureInternal();
    }

    void audioDeviceIOCallbackWithContext(const float* const*, int,
                                          float* const* outputs,int channels,int samples,
                                          const juce::AudioIODeviceCallbackContext&) override
    {
        if(!enabled.load(std::memory_order_acquire) || samples<=0 || !lock.tryEnter())return;
        const auto blockStart=sessionSamples;
        lastBlockStart=blockStart;
        const double rate=juce::jmax(1.0,owner.audioEngine.getSampleRate());
        const double bpm=sessionTempo.load(std::memory_order_acquire);
        const bool repeat=loopEnabled.load(std::memory_order_acquire);
        // The post-mixer capture hook owns the WAV writer, not this per-track
        // render callback (which executes before the instruments/effects).

        for(int logical=0;logical<(int)tracks.size();++logical)
        {
            auto& state=tracks[(size_t)logical];
            state.panicOffset=state.panicNext?0:-1;
            state.panicNext=false;
            state.previousScene=state.activeScene;
            state.previousStartedAt=state.startedAt;
            state.transitionOffset=-1;
            const bool isAudio=logical<owner.getAudioTrackCount();
            const bool muted=isAudio?owner.audioEngine.isTrackMuted(logical):false;
            const bool soloed=isAudio?owner.audioEngine.isTrackSolo(logical):false;
            const bool audible=!muted && (!isAudio || !owner.audioEngine.isAnyTrackSolo() || soloed);
            const float gain=isAudio?owner.audioEngine.getTrackGain(logical):1.0f;
            const float pan=isAudio?owner.audioEngine.getTrackPan(logical):0.0f;
            const float left=gain*(pan>0.0f?1.0f-pan:1.0f);
            const float right=gain*(pan<0.0f?1.0f+pan:1.0f);

            for(int i=0;i<samples;++i)
            {
                const auto absolute=blockStart+i;
                if(state.queuedScene!=-2 && absolute>=state.queuedAt)
                {
                    if(state.activeScene!=state.queuedScene)
                    {
                        state.fadeFromScene=state.activeScene;
                        state.fadeFromStarted=state.startedAt;
                        state.fadeStart=absolute;
                        state.fadeLength=LibertyPerformLive::transitionSamples(
                            transitionMs.load(std::memory_order_relaxed),rate);
                    }
                    state.activeScene=state.queuedScene;
                    state.startedAt=state.queuedAt;
                    state.transitionOffset=i;
                    state.queuedScene=-2;
                    state.panicOffset=i;
                    if(captureActive.load(std::memory_order_relaxed) &&
                       captureCount<(int)captureEvents.size())
                        captureEvents[(size_t)captureCount++]={logical,state.activeScene,
                            juce::jmax<std::int64_t>(0,absolute-captureStartSample)};
                }
                if(state.activeScene>=0)
                {
                    const auto& slot=state.slots[(size_t)state.activeScene];
                    const auto relative=absolute-state.startedAt;
                    if(relative>=0)
                    {
                        const auto length=slot.audio?(std::int64_t)slot.audioLength:
                            LibertyPerformTiming::ticksToSamples(slot.midiLengthTicks,bpm,rate);
                        if(length<=0 || (!(repeat && slot.settings.loop) && relative>=length))
                        {
                            if(captureActive.load() && captureCount<(int)captureEvents.size())
                                captureEvents[(size_t)captureCount++]={logical,-1,
                                    juce::jmax<std::int64_t>(0,absolute-captureStartSample)};
                            state.activeScene=-1;state.panicOffset=i;state.transitionOffset=i;
                        }
                    }
                }
                if(!isAudio || !audible)continue;
                const auto readAudio=[&](int scene,std::int64_t startAt,float mix,int channel)
                {
                    if(scene<0 || mix<=0.f)return 0.f;
                    const auto& slot=state.slots[(size_t)scene];
                    if(slot.audio==nullptr || slot.audioLength<=0)return 0.f;
                    const auto position=absolute-startAt;
                    if(position<0 || (!(repeat && slot.settings.loop) &&
                                      position>=slot.audioLength))return 0.f;
                    const auto index=slot.audioFirst+(int)(position%slot.audioLength);
                    return slot.audio->getSample(juce::jmin(channel,
                        slot.audio->getNumChannels()-1),index)*slot.linearGain*mix;
                };
                const auto elapsed=absolute-state.fadeStart;
                const bool inFade=state.fadeFromScene>=0 &&
                                  elapsed>=0 && elapsed<state.fadeLength;
                const float t=inFade?
                    LibertyPerformLive::crossfadeIn(elapsed,state.fadeLength):1.f;
                const float oldGain=inFade?1.f-t:0.f;
                const float newGain=state.activeScene>=0?t:0.f;
                const float volume=audioMacroGain.load(std::memory_order_relaxed);
                const float l=(readAudio(state.activeScene,state.startedAt,newGain,0)+
                    readAudio(state.fadeFromScene,state.fadeFromStarted,oldGain,0))*left*volume;
                const float r=(readAudio(state.activeScene,state.startedAt,newGain,1)+
                    readAudio(state.fadeFromScene,state.fadeFromStarted,oldGain,1))*right*volume;
                if(channels>0 && outputs[0])outputs[0][i]+=l;
                if(channels>1 && outputs[1])outputs[1][i]+=r;
            }
        }
        sessionSamples+=samples;
        lock.exit();
    }

    MainComponent& owner;
    juce::AudioFormatManager formats;
    std::vector<TrackState> tracks;
    std::int64_t sessionSamples=0,lastBlockStart=0;
    std::atomic<bool> loopEnabled { true };
    std::atomic<double> sessionTempo { 120.0 };
    std::atomic<int> transitionMs { 150 };
    std::atomic<float> audioMacroGain { 1.0f };
    std::atomic<float> instrumentMacroGain { 1.0f };
    std::atomic<float> masterMacroGain { 1.0f };
    mutable juce::CriticalSection lock;
    mutable juce::CriticalSection captureLock;
    float lastMasterMacroGain=1.f; // audio thread only
    std::atomic<bool> enabled { false };
    bool callbackAttached = false;
    juce::AudioBuffer<float> captureMix;
    std::unique_ptr<juce::TimeSliceThread> captureThread;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> captureWriter;
    juce::File captureFile;
    std::array<CaptureEvent,32768> captureEvents {};
    int captureCount=0;
    std::int64_t captureStartSample=0,captureEndSample=0;
    std::atomic<bool> captureActive { false };
};

class PerformView final : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          private juce::Timer
{
public:
    explicit PerformView(MainComponent& ownerIn)
        : owner(ownerIn), player(ownerIn)
    {
        setOpaque(true);
        setWantsKeyboardFocus(true);
        setInterceptsMouseClicks(true, true);
        // PERFORM is a normal page, not a floating overlay. The Browser
        // and its resize handle own the always-on-top layer.
        // Giving this full-screen page the same priority hides the handle
        // and makes its blue grip appear to flicker during page updates.
        clipButtons.resize((size_t)trackCount());
        stopTrackButtons.resize((size_t)trackCount());
        trackHeaders.resize((size_t)trackCount());
        activeTrackScene.assign((size_t)trackCount(), -1);
        performAudioFiles.resize((size_t)audioTrackCount());
        performMidiClips.resize((size_t)trackCount());
        clipMetadata.resize((size_t)trackCount());

        for (int track = 0; track < trackCount(); ++track)
        {
            auto& stop = stopTrackButtons[(size_t)track];
            stop.setButtonText("STOP");
            stop.setMouseClickGrabsKeyboardFocus(false);
            stop.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252b32));
            stop.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            stop.onClick = [this, track]
            {
                player.stopTrack(track);
                if (track >= 0 && track < (int)activeTrackScene.size())
                    activeTrackScene[(size_t)track] = -1;
                refreshClipLabels();
                repaint();
            };
            addAndMakeVisible(stop);

            for (int scene = 0; scene < sceneCount; ++scene)
            {
                auto& cell = clipButtons[(size_t)track][(size_t)scene];
                cell.setMouseClickGrabsKeyboardFocus(false);
                cell.onClick = [this, track, scene] { launchClip(track, scene); };
                addAndMakeVisible(cell);
            }
        }

        for (int scene = 0; scene < sceneCount; ++scene)
        {
            auto& launch = sceneButtons[(size_t)scene];
            launch.setButtonText(juce::String::fromUTF8("SCÈNE ") + juce::String(scene + 1));
            launch.setMouseClickGrabsKeyboardFocus(false);
            launch.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff27313a));
            launch.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            launch.onClick = [this, scene] { launchScene(scene); };
            addAndMakeVisible(launch);
        }

        stopAllButton.setButtonText("STOP ALL");
        stopAllButton.setMouseClickGrabsKeyboardFocus(false);
        stopAllButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff6f3030));
        stopAllButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        stopAllButton.onClick = [this]
        {
            player.stopAll();
            followNextAt=0;
            activeFollowScene=-1;
            std::fill(activeTrackScene.begin(), activeTrackScene.end(), -1);
            refreshClipLabels();
            repaint();
        };
        addAndMakeVisible(stopAllButton);

        recordArrangeButton.setButtonText("CAPTURER VERS ARRANGEMENT");
        recordArrangeButton.setMouseClickGrabsKeyboardFocus(false);
        recordArrangeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff31404d));
        recordArrangeButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        recordArrangeButton.onClick = [this] { toggleCaptureToArrange(); };
        addAndMakeVisible(recordArrangeButton);
        quantizeSelector.addItem(juce::String::fromUTF8("IMMÉDIAT"),1);
        quantizeSelector.addItem("1 TEMPS",2);
        quantizeSelector.addItem("1 MESURE",3);
        quantizeSelector.setSelectedId(3,juce::dontSendNotification);
        quantizeSelector.setTooltip(juce::String::fromUTF8("Quantification du lancement des clips et des scènes."));
        addAndMakeVisible(quantizeSelector);
        loopButton.setButtonText("BOUCLE : ON");
        loopButton.setClickingTogglesState(true);
        loopButton.setToggleState(true,juce::dontSendNotification);
        loopButton.onClick=[this]
        {
            const bool on=loopButton.getToggleState();
            loopButton.setButtonText(on?"BOUCLE : ON":"BOUCLE : OFF");
            player.setLoopEnabled(on);
            ++sessionRevision;
        };
        addAndMakeVisible(loopButton);
        initialiseLiveControls();
        initialiseInspector();
        std::fill(activeTrackScene.begin(), activeTrackScene.end(), -1);
        setVisible(false);
        owner.addAndMakeVisible(this);
        startTimerHz(20);
    }

    ~PerformView() override
    {
        stopTimer();
        if (player.isCapturing()) player.stopCapture();
        player.setEnabled(false);
    }

    void setPerformVisible(bool shouldShow)
    {
        performVisible = shouldShow;
        dragTrack = dragScene = -1;
        setVisible(shouldShow);
        if(!shouldShow && player.isCapturing())finishCaptureToArrange();
        player.setEnabled(shouldShow);

        if (shouldShow)
        {
            syncTrackContainers();
            // ARRANGE transport is deliberately stopped. PERFORM owns independent audio.
            owner.audioEngine.setPlaying(false);
            owner.audioEngine.setStepPreview(-1,false);
            owner.isPlaying = false;
            player.setTempoBpm(owner.getTempoBpm());
            setBounds(0, transportHeight, owner.getWidth(), juce::jmax(1, owner.getHeight() - transportHeight));
            resized();
            refreshClipLabels();
            toFront(false);
            repaint();
        }
        else
        {
            cancelTemporaryGeneratorPreview();
            player.stopAll();
            followNextAt=0;
            activeFollowScene=-1;
            std::fill(activeTrackScene.begin(), activeTrackScene.end(), -1);
            // Recording is finalized before disabling the session clock.
        }
    }

    void renderAudioBlock(float* const* outputs, int numOutputs, int numSamples)
    {
        player.renderAudioBlock(outputs, numOutputs, numSamples);
    }

    bool isPerformVisible() const noexcept { return performVisible; }
    void appendMidiForInstrument(int lane,juce::MidiBuffer& midi,int samples)
    {if(performVisible)player.appendMidiForInstrument(lane,midi,samples);}
    float instrumentMacro() const noexcept
    {return performVisible?player.getLiveInstrumentGain():1.f;}
    void finalMix(float* const* data,int channels,int count)
    {if(performVisible)player.captureFinalMix(data,channels,count);}
    std::uint64_t getSessionRevision() const noexcept{return sessionRevision;}

    void clearSession()
    {
        player.stopAll();
        player.resetSession();
        for(auto& bank:performAudioFiles)bank={};
        for(auto& bank:performMidiClips)bank={};
        for(auto& bank:clipMetadata)bank={};
        selectedClipTrack=selectedClipScene=-1;
        refreshInspector();
        quantizeSelector.setSelectedId(3,juce::dontSendNotification);
        loopButton.setToggleState(true,juce::dontSendNotification);
        loopButton.setButtonText("BOUCLE : ON");
        player.setLoopEnabled(true);
        for(auto& value:followActions)value={};
        for(auto& binding:midiBindings)binding={};
        followNextAt=0;
        activeFollowScene=-1;
        selectedFollowScene=0;
        for(int i=0;i<4;++i)
            liveMacroSliders[(size_t)i].setValue(i==3?150:100,
                                                juce::dontSendNotification);
        applyLiveMacros();
        updateFollowControls();
        updateMidiLearnLabel();
        ++sessionRevision;
        refreshClipLabels();
    }

    void saveSession(juce::XmlElement& project,const juce::File& projectFile) const
    {
        auto* session=project.createNewChildElement("PerformanceSession");
        session->setAttribute("quantization",quantizeSelector.getSelectedId());
        session->setAttribute("loop",loopButton.getToggleState());
        auto* live=session->createNewChildElement("LivePro");
        live->setAttribute("followScene",selectedFollowScene);
        for(int i=0;i<LibertyPerformLive::scenes;++i)
        {
            auto* rule=live->createNewChildElement("Follow");
            rule->setAttribute("scene",i);
            rule->setAttribute("action",followActions[(size_t)i].action);
            rule->setAttribute("bars",followActions[(size_t)i].afterBars);
        }
        for(int i=0;i<LibertyPerformLive::targets;++i)
        {
            auto* binding=live->createNewChildElement("Binding");
            binding->setAttribute("target",i);
            const auto& value=midiBindings[(size_t)i];
            binding->setAttribute("kind",value.kind);
            binding->setAttribute("channel",value.channel);
            binding->setAttribute("number",value.number);
        }
        for(int i=0;i<4;++i)
            live->setAttribute("macro"+juce::String(i+1),
                               (int)liveMacroSliders[(size_t)i].getValue());
        for(int track=0;track<trackCount();++track)
            for(int scene=0;scene<sceneCount;++scene)
            {
                if(!slotHasClip(track,scene))continue;
                auto* clip=session->createNewChildElement("Clip");
                clip->setAttribute("track",track);
                clip->setAttribute("scene",scene);
                const auto& data=clipMetadata[(size_t)track][(size_t)scene];
                const auto& params=data.settings;
                clip->setAttribute("name",data.name);
                clip->setAttribute("colourId",data.colourId);
                clip->setAttribute("clipLoop",params.loop);
                clip->setAttribute("clipGainDb",(double)params.gainDb);
                clip->setAttribute("transpose",params.transpose);
                clip->setAttribute("inFraction",(double)params.inFraction);
                clip->setAttribute("outFraction",(double)params.outFraction);
                clip->setAttribute("clipQuantize",params.quantization);
                clip->setAttribute("launchMode",params.launchMode);
                clip->setAttribute("midiBars",params.midiBars);
                if(track<audioTrackCount())
                {
                    clip->setAttribute("kind","audio");
                    clip->setAttribute("source",performAudioFiles[(size_t)track][(size_t)scene]
                        .getRelativePathFrom(projectFile.getParentDirectory()));
                }
                else
                {
                    clip->setAttribute("kind","midi");
                    const auto& data=performMidiClips[(size_t)track][(size_t)scene];
                    clip->setAttribute("lengthTicks",juce::String(data.lengthTicks));
                    const auto& p=data.generator;
                    clip->setAttribute("generatorKind",p.kind);
                    clip->setAttribute("generatorDivision",p.division);
                    clip->setAttribute("generatorSteps",p.steps);
                    clip->setAttribute("generatorDirection",p.direction);
                    clip->setAttribute("generatorOctaves",p.octaves);
                    clip->setAttribute("generatorDensity",p.density);
                    clip->setAttribute("generatorSwing",p.swing);
                    clip->setAttribute("generatorVariation",p.variation);
                    clip->setAttribute("generatorVelocity",p.velocity);
                    clip->setAttribute("generatorRhythmStyle",p.rhythmStyle);
                    clip->setAttribute("generatorSeed",juce::String((juce::int64)p.seed));
                    clip->setAttribute("generatorEnabled",data.generatorState==2);
                    // The XML holds the original notes. Derived notes are
                    // regenerated at load time, never baked into the source.
                    for(const auto& n:data.notes)
                    {
                        auto* e=clip->createNewChildElement("Note");
                        e->setAttribute("start",juce::String(n.startTick));
                        e->setAttribute("length",juce::String(n.lengthTicks));
                        e->setAttribute("pitch",(int)n.pitch);
                        e->setAttribute("velocity",(int)n.velocity);
                        e->setAttribute("channel",(int)n.channel);
                        e->setAttribute("muted",n.muted);
                        e->setAttribute("probability",(int)n.probability);
                    }
                }
            }
    }

    void loadSession(const juce::XmlElement& project,const juce::File& file)
    {
        clearSession();
        const auto* session=project.getChildByName("PerformanceSession");
        if(!session)return;
        quantizeSelector.setSelectedId(juce::jlimit(1,3,
            session->getIntAttribute("quantization",3)),juce::dontSendNotification);
        const bool repeat=session->getBoolAttribute("loop",true);
        loopButton.setToggleState(repeat,juce::dontSendNotification);
        loopButton.setButtonText(repeat?"BOUCLE : ON":"BOUCLE : OFF");
        player.setLoopEnabled(repeat);
        if(const auto* live=session->getChildByName("LivePro"))
        {
            selectedFollowScene=juce::jlimit(0,7,live->getIntAttribute("followScene",0));
            for(const auto* value:live->getChildIterator())
            {
                if(value->hasTagName("Follow"))
                {
                    const int index=value->getIntAttribute("scene",-1);
                    if(index>=0&&index<LibertyPerformLive::scenes)
                        followActions[(size_t)index]=LibertyPerformLive::validFollow(
                            {value->getIntAttribute("action",0),
                             value->getIntAttribute("bars",4)});
                }
                if(value->hasTagName("Binding"))
                {
                    const int index=value->getIntAttribute("target",-1);
                    if(index>=0&&index<LibertyPerformLive::targets)
                        midiBindings[(size_t)index]=LibertyPerformLive::validBinding(
                            {value->getIntAttribute("channel",0),
                             value->getIntAttribute("kind",0),
                             value->getIntAttribute("number",-1)});
                }
            }
            for(int i=0;i<4;++i)
                liveMacroSliders[(size_t)i].setValue(
                    juce::jlimit(0,i==3?1000:200,
                        live->getIntAttribute("macro"+juce::String(i+1),
                                               i==3?150:100)),
                    juce::dontSendNotification);
            applyLiveMacros();
            updateFollowControls();
            updateMidiLearnLabel();
        }
        for(const auto* clip:session->getChildIterator())
        {
            if(!clip->hasTagName("Clip"))continue;
            const int track=clip->getIntAttribute("track",-1);
            const int scene=clip->getIntAttribute("scene",-1);
            if(track<0 || track>=trackCount() || scene<0 || scene>=sceneCount)continue;
            if(clip->getStringAttribute("kind")=="audio" && track<audioTrackCount())
            {
                const auto source=clip->getStringAttribute("source");
                const auto path=juce::File::isAbsolutePath(source)?juce::File(source)
                    :file.getParentDirectory().getChildFile(source);
                juce::String error;
                if(path.existsAsFile() && player.prepareAudioClip(track,scene,path,error))
                    performAudioFiles[(size_t)track][(size_t)scene]=path;
            }
            else if(clip->getStringAttribute("kind")=="midi" && track>=audioTrackCount())
            {
                SessionMidiData data;
                data.valid=true;
                data.lengthTicks=juce::jmax<std::int64_t>(1,
                    clip->getStringAttribute("lengthTicks","3840").getLargeIntValue());
                for(const auto* e:clip->getChildIterator())
                {
                    if(!e->hasTagName("Note") || data.notes.size()>=8192)continue;
                    MidiEngine::NoteEvent note;
                    note.startTick=e->getStringAttribute("start").getLargeIntValue();
                    note.lengthTicks=e->getStringAttribute("length").getLargeIntValue();
                    if(note.startTick<0 || note.lengthTicks<=0 ||
                        note.lengthTicks>data.lengthTicks ||
                        note.startTick>data.lengthTicks-note.lengthTicks)continue;
                    note.pitch=(std::uint8_t)juce::jlimit(0,127,e->getIntAttribute("pitch",60));
                    note.velocity=(std::uint8_t)juce::jlimit(1,127,e->getIntAttribute("velocity",100));
                    note.channel=(std::uint8_t)juce::jlimit(1,16,e->getIntAttribute("channel",1));
                    note.muted=e->getBoolAttribute("muted",false);
                    note.probability=(std::uint8_t)juce::jlimit(0,100,
                        e->getIntAttribute("probability",100));
                    data.notes.push_back(note);
                }
                if(assignMidiClip(track,scene,data))
                {
                    auto& stored=performMidiClips[(size_t)track][(size_t)scene];
                    auto p=LibertyPerformGenerator::Parameters{};
                    p.kind=clip->getIntAttribute("generatorKind",0);
                    p.division=clip->getIntAttribute("generatorDivision",1);
                    p.steps=clip->getIntAttribute("generatorSteps",16);
                    p.direction=clip->getIntAttribute("generatorDirection",0);
                    p.octaves=clip->getIntAttribute("generatorOctaves",1);
                    p.density=clip->getIntAttribute("generatorDensity",75);
                    p.swing=clip->getIntAttribute("generatorSwing",0);
                    p.variation=clip->getIntAttribute("generatorVariation",30);
                    p.velocity=clip->getIntAttribute("generatorVelocity",100);
                    p.rhythmStyle=clip->getIntAttribute("generatorRhythmStyle",0);
                    p.seed=(std::uint32_t)clip->getStringAttribute(
                        "generatorSeed","950923611").getLargeIntValue();
                    stored.generator=LibertyPerformGenerator::validated(p);
                    if(clip->getBoolAttribute("generatorEnabled",false))
                    {
                        stored.generatedNotes=LibertyPerformGenerator::render(
                            stored.notes,stored.lengthTicks,stored.generator);
                        stored.generatorState=2;
                        player.replaceMidiNotes(track,scene,stored.generatedNotes,
                            stored.lengthTicks,owner.getTimeSignatureNumerator(),
                            owner.getTimeSignatureDenominator());
                    }
                }
            }
            if(!slotHasClip(track,scene))continue;
            ClipMetadata metadata;
            metadata.name=clip->getStringAttribute("name").substring(0,80);
            metadata.colourId=juce::jlimit(-1,8,clip->getIntAttribute("colourId",-1));
            auto& params=metadata.settings;
            params.loop=clip->getBoolAttribute("clipLoop",true);
            params.gainDb=(float)clip->getDoubleAttribute("clipGainDb",0.0);
            params.transpose=clip->getIntAttribute("transpose",0);
            params.inFraction=(float)clip->getDoubleAttribute("inFraction",0.0);
            params.outFraction=(float)clip->getDoubleAttribute("outFraction",1.0);
            params.quantization=clip->getIntAttribute("clipQuantize",-1);
            params.launchMode=clip->getIntAttribute("launchMode",0);
            params.midiBars=clip->getIntAttribute("midiBars",0);
            metadata.settings=LibertyPerformClip::validated(params);
            clipMetadata[(size_t)track][(size_t)scene]=metadata;
            player.updateClipSettings(track,scene,metadata.settings,
                owner.getTimeSignatureNumerator(),owner.getTimeSignatureDenominator());
        }
        ++sessionRevision;
        refreshClipLabels();
    }

    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        if(!performVisible)return false;
        for(const auto& path:files)
            if(isSupportedAudioFile(juce::File(path)) || isSupportedMidiFile(juce::File(path)))return true;
        return false;
    }
    void fileDragEnter(const juce::StringArray& files,int x,int y) override
    {updateDropTarget({x,y},files);}
    void fileDragMove(const juce::StringArray& files,int x,int y) override
    {updateDropTarget({x,y},files);}
    void fileDragExit(const juce::StringArray&) override
    {dragTrack=dragScene=-1;refreshClipLabels();repaint();}
    void filesDropped(const juce::StringArray& files,int x,int y) override
    {
        updateDropTarget({x,y},files);
        if(dragTrack<0 || dragScene<0)return;
        const bool audioTarget=dragTrack<audioTrackCount();
        juce::File chosen;
        for(const auto& path:files)
        {
            const juce::File file(path);
            if(audioTarget?isSupportedAudioFile(file):isSupportedMidiFile(file))
            {chosen=file;break;}
        }
        if(!chosen.existsAsFile())return;
        if(audioTarget)
        {
            juce::String error;
            if(!player.prepareAudioClip(dragTrack,dragScene,chosen,error))
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Liberty - PERFORMANCE",error,"OK");
                dragTrack=dragScene=-1;return;
            }
            performAudioFiles[(size_t)dragTrack][(size_t)dragScene]=chosen;
            clipMetadata[(size_t)dragTrack][(size_t)dragScene]={};
        }
        else
        {
            SessionMidiData data;
            juce::String error;
            if(!parseMidiFile(chosen,data,error)
                || !assignMidiClip(dragTrack,dragScene,data))
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "Liberty - PERFORMANCE",error.isNotEmpty()?error:
                    "Impossible de charger ce clip MIDI.","OK");
                dragTrack=dragScene=-1;return;
            }
        }
        ++sessionRevision;
        const int loadedTrack=dragTrack,loadedScene=dragScene;
        dragTrack=dragScene=-1;
        selectClip(loadedTrack,loadedScene);
        refreshClipLabels();repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(LibertyUI::Colour::canvasDeep);
        g.setGradientFill(juce::ColourGradient(
            LibertyUI::Colour::surface,0.f,0.f,
            LibertyUI::Colour::header,(float)getWidth(),64.f,false));
        g.fillRect(0,0,getWidth(),64);
        g.setColour(LibertyUI::Colour::dividerSoft);
        g.drawHorizontalLine(63,0.0f,(float)getWidth());
        LibertyUI::accentRule(g,{0.f,62.f,(float)getWidth(),1.f});

        // The MainComponent track-creation toolbar occupies x=0..330
        // in this same header. Keep this region text-free so that
        // + AUDIO / + MIDI / + INSTRUMENT / + BUS never cover another label.
        const int titleX=juce::jmax(loopButton.getRight()+18,
            LibertyNoOverlap::creationZoneWidth+16);
        if(titleX+160<getWidth()-16)
        {
            g.setColour(LibertyUI::Colour::textPrimary);
            g.setFont(juce::Font(14.0f,juce::Font::bold));
            g.drawFittedText("PERFORMANCE",titleX,16,160,24,
                juce::Justification::centredLeft,1);
        }

        if(!midiWorkspaceVisible())
        {
        const int paintedTracks = juce::jmin(trackCount(), (int)trackHeaders.size());
        for (int track = 0; track < paintedTracks; ++track)
        {
            const auto header = trackHeaders[(size_t)track];
            const auto colour = colourForTrack(track);
            LibertyUI::fillPanel(g,header.toFloat(),false,7.f);
            g.setColour(colour.brighter(0.26f));
            g.fillRoundedRectangle((float)header.getX()+10.f,
                (float)header.getY()+1.f,
                (float)juce::jmax(1,header.getWidth()-20),3.f,1.5f);
            g.setColour(LibertyUI::Colour::textPrimary);
            g.setFont(juce::Font(12.0f,juce::Font::bold));
            g.drawFittedText(trackName(track),
                header.reduced(10,6),juce::Justification::centredTop,1);
            g.setColour(LibertyUI::Colour::textMuted);
            g.setFont(juce::Font(9.0f,juce::Font::plain));
            g.drawFittedText(trackType(track),
                header.getX()+8,header.getY()+27,
                header.getWidth()-16,14,juce::Justification::centred,1);
        }

        for (int scene = 0; scene < sceneCount; ++scene)
        {
            const auto y = sceneRows[(size_t)scene].getY();
            g.setColour(scene % 2 == 0?
                LibertyUI::Colour::canvas : LibertyUI::Colour::canvasDeep);
            g.fillRect(0,y,getWidth(),sceneRows[(size_t)scene].getHeight());
            g.setColour(LibertyUI::Colour::dividerSoft.withAlpha(0.47f));
            g.drawHorizontalLine(sceneRows[(size_t)scene].getBottom()-1,
                                 0.0f,(float)getWidth());
        }
        // Subtle track lanes: rhythm and vertical alignment remain readable
        // without adding outlines that compete with clip launch buttons.
        for(int track=0;track<paintedTracks;++track)
        {
            const auto header=trackHeaders[(size_t)track];
            if(header.isEmpty())continue;
            const auto lane=juce::Rectangle<float>(
                (float)header.getX()+0.5f,
                (float)sceneRows[0].getY(),
                (float)header.getWidth()-1.f,
                (float)juce::jmax(0,
                    sceneRows[sceneCount-1].getBottom()-sceneRows[0].getY()));
            g.setColour(colourForTrack(track).withAlpha(0.075f));
            g.fillRoundedRectangle(lane,4.f);
        }
        }

        if(livePanelHeight()>0)
        {
            const auto live=juce::Rectangle<int>(
                18,getHeight()-54-inspectorHeight()-livePanelHeight()-7,
                juce::jmax(1,getWidth()-36),livePanelHeight());
            LibertyUI::fillPanel(g,live.toFloat(),false,8.f);
            LibertyUI::accentRule(g,{(float)live.getX()+12.f,
                (float)live.getY()+1.f,165.f,1.7f});
            g.setColour(LibertyUI::Colour::textMuted);
            g.setFont(juce::Font(9.f,juce::Font::bold));
            g.drawText(juce::String::fromUTF8("SCÈNES ET MIDI"),live.getX()+12,
                live.getY()+2,juce::jmax(1,live.getWidth()-20),13,
                juce::Justification::centredLeft);
            const char* labels[]={"MASTER","AUDIO","INSTR.","FADE"};
            const auto width=juce::jmax(1,(live.getWidth()-42)/4);
            for(int i=0;i<4;++i)
                g.drawText(juce::String("MACRO ")+juce::String(i+1)+
                    "  "+labels[i],live.getX()+12+i*width,live.getY()+53,
                    juce::jmax(1,width-6),12,juce::Justification::centredLeft);
        }

        if(hasSelectedClip())
        {
            const auto area=inspectorArea();
            LibertyUI::fillPanel(g,area.toFloat(),true,10.f);
            const auto title=slotName(selectedClipTrack,selectedClipScene);
            g.setColour(juce::Colour(0xffbfccd8));
            g.setFont(juce::Font(11.5f,juce::Font::bold));
            if(!midiWorkspaceVisible())
            {
                // Compact CLIP / LANCEMENT inspector; tabs own the header row.
                if(selectedClipTrack<audioTrackCount())
                    g.drawFittedText(juce::String::fromUTF8("INSPECTEUR — ")+title,
                        area.getX()+264,area.getY()+9,
                        juce::jmax(1,area.getWidth()-378),24,
                        juce::Justification::centredLeft,1);
            }
            else if(area.getWidth()>1150)
                g.drawFittedText(title,area.getX()+670,area.getY()+11,
                    juce::jmax(1,area.getWidth()-780),20,
                    juce::Justification::centredLeft,1);
            const int x=area.getX()+16;
            const int bodyWidth=juce::jmax(1,area.getWidth()-32);
            const int top=area.getY();
            g.setFont(juce::Font(10.0f));
            g.setColour(juce::Colour(0xff9eb0c1));
            if(inspectorTab==0)
            {
                g.drawText("NOM DU CLIP",x,top+37,bodyWidth*45/100,16,
                           juce::Justification::centredLeft);
                g.drawText("COULEUR",x+bodyWidth*46/100,top+37,
                           bodyWidth*24/100,16,juce::Justification::centredLeft);
                if(selectedClipTrack<audioTrackCount())
                {
                    g.drawText("GAIN",x,top+104,bodyWidth/3,15,
                               juce::Justification::centredLeft);
                    g.drawText(juce::String::fromUTF8("DÉBUT"),x+bodyWidth/3+5,top+104,
                               bodyWidth/3-5,15,juce::Justification::centredLeft);
                    g.drawText("FIN",x+2*bodyWidth/3+5,top+104,
                               bodyWidth/3-5,15,juce::Justification::centredLeft);
                }
                else
                {
                    g.drawText("TRANSPOSITION",x,top+104,bodyWidth/3,15,
                               juce::Justification::centredLeft);
                    g.drawText("LONGUEUR MIDI",x+bodyWidth/3+5,top+104,
                               bodyWidth/3,15,juce::Justification::centredLeft);
                }
            }
            else if(inspectorTab==1)
            {
                g.drawText("QUANTIFICATION",x,top+48,bodyWidth/3,16,
                           juce::Justification::centredLeft);
                g.drawText("MODE DE LANCEMENT",x+bodyWidth/3+12,top+48,
                           bodyWidth/3,16,juce::Justification::centredLeft);
                g.setFont(juce::Font(10.5f));
                g.drawText(juce::String::fromUTF8(
                    "RELANCER : redémarre le clip. BASCULE : un second clic arrête le clip."),
                    x,top+119,bodyWidth-12,25,juce::Justification::centredLeft);
            }
            else if(midiEditorVisible())
            {
                g.setColour(juce::Colour(0xffa9bfce));
                g.setFont(juce::Font(10.0f));
                const int controlY=top+73;
                const int third=juce::jmax(1,bodyWidth/3);
                g.drawText("HAUTEUR",x,controlY,third-6,16,
                    juce::Justification::centredLeft);
                g.drawText(juce::String::fromUTF8("VÉLOCITÉ"),
                    x+third+5,controlY,third-6,16,
                    juce::Justification::centredLeft);
                g.drawText(juce::String::fromUTF8("DURÉE"),
                    x+2*third+5,controlY,third-6,16,
                    juce::Justification::centredLeft);
                paintMidiPiano(g);
            }
            else if(midiGeneratorVisible())
            {
                const bool rhythms=inspectorTab==4;
                const int gap=8;
                const int five=juce::jmax(1,(bodyWidth-4*gap)/5);
                const int quarter=juce::jmax(1,(bodyWidth-3*gap)/4);
                g.setFont(juce::Font(10.0f));
                g.setColour(juce::Colour(0xffafc0d0));
                const auto label=[&](const juce::String& text,int col,int y,int w)
                {
                    g.drawFittedText(text,x+col*(w+gap),y,w,15,
                        juce::Justification::centredLeft,1);
                };
                label(rhythms?"STYLE":"MOTEUR",0,top+48,five);
                label("DIVISION",1,top+48,five);
                label("PAS",2,top+48,five);
                if(!rhythms && generatorKind.getSelectedId()==1)
                {
                    label("DIRECTION",3,top+48,five);
                    label("OCTAVES",4,top+48,five);
                }
                label(juce::String::fromUTF8("DENSITÉ"),0,top+114,quarter);
                label("SWING",1,top+114,quarter);
                label("VARIATION",2,top+114,quarter);
                label(juce::String::fromUTF8("VÉLOCITÉ"),3,top+114,quarter);
                const auto* data=selectedMidiData();
                if(data!=nullptr)
                {
                    const auto status=data->generatorState==2?
                        juce::String::fromUTF8("ACTIF — source MIDI conservée"):
                        data->generatorState==1?
                        juce::String::fromUTF8("PRÉÉCOUTE — non enregistrée"):
                        juce::String::fromUTF8("APERÇU VISUEL — appuyer sur PRÉÉCOUTER pour entendre");
                    g.setColour(data->generatorState==2?
                        juce::Colour(0xff79d7b6):juce::Colour(0xffa7bbc9));
                    g.drawFittedText(status,x,top+224,bodyWidth,22,
                        juce::Justification::centredLeft,1);
                }
                paintMidiPiano(g);
            }
        }

        if (!midiWorkspaceVisible() && dragTrack >= 0 && dragTrack < (int)clipButtons.size() && dragScene >= 0 && dragScene < sceneCount)
        {
            const auto r = clipButtons[(size_t)dragTrack][(size_t)dragScene].getBounds().expanded(2);
            g.setColour(juce::Colour(0xff69d4ff));
            g.drawRoundedRectangle(r.toFloat(), 5.0f, 3.0f);
            g.setFont(juce::Font(9.0f, juce::Font::bold));
            g.drawText(juce::String::fromUTF8(dragTrack<audioTrackCount()?"DÉPOSER AUDIO":"DÉPOSER MIDI"),
                       r, juce::Justification::centred);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if(!midiEditorVisible())return;
        const auto point=event.getPosition();
        const auto roll=midiPianoBounds();
        const auto velocity=midiVelocityBounds();
        gestureNote=-1;
        gestureOriginal.clear();
        gestureHistorySaved=false;
        gestureResizing=false;
        gestureVelocity=false;
        if(roll.contains(point))
        {
            grabKeyboardFocus();
            const int index=midiNoteAt(point);
            if(event.mods.isPopupMenu())
            {
                if(index>=0){selectedMidiNote=index;deleteMidiNote();}
                return;
            }
            selectedMidiNote=index;
            midiLastClickTick=midiTickAt(point.x);
            if(index>=0)
            {
                gestureNote=index;
                gestureStart=point;
                if(const auto* clip=selectedMidiData())
                {
                    gestureOriginal=clip->notes;
                    const auto noteBounds=midiNoteBounds(clip->notes[(size_t)index]);
                    gestureResizing=point.x>=noteBounds.getRight()-juce::jmin(7,
                                            juce::jmax(2,noteBounds.getWidth()/3));
                }
            }
            refreshMidiNoteControls();
            repaint();
        }
        else if(velocity.contains(point))
        {
            auto* clip=selectedMidiData();
            if(clip==nullptr)return;
            int nearest=-1;
            int bestDistance=14;
            for(int i=0;i<(int)clip->notes.size();++i)
            {
                const int startX=roll.getX()+(int)std::llround(
                    (double)clip->notes[(size_t)i].startTick*roll.getWidth()/
                    (double)juce::jmax<std::int64_t>(1,clip->lengthTicks));
                const int distance=std::abs(startX-point.x);
                if(distance<bestDistance){nearest=i;bestDistance=distance;}
            }
            selectedMidiNote=nearest;
            if(nearest>=0)
            {
                gestureNote=nearest;
                gestureStart=point;
                gestureVelocity=true;
                gestureOriginal=clip->notes;
                const int v=juce::jlimit(1,127,
                    (velocity.getBottom()-point.y)*127/
                    juce::jmax(1,velocity.getHeight()));
                if(v!=(int)clip->notes[(size_t)nearest].velocity)
                {
                    clip->history.checkpoint(clip->notes);
                    gestureHistorySaved=true;
                    clip->notes[(size_t)nearest].velocity=(std::uint8_t)v;
                    publishMidiNotes();
                }
            }
            refreshMidiNoteControls();
            repaint();
        }
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if(!midiEditorVisible() || gestureNote<0 ||
           gestureNote>=(int)gestureOriginal.size())return;
        auto* clip=selectedMidiData();
        if(clip==nullptr || clip->lengthTicks<=0)return;
        const auto roll=midiPianoBounds();
        if(roll.isEmpty())return;
        const auto source=gestureOriginal[(size_t)gestureNote];
        auto start=source.startTick;
        auto length=source.lengthTicks;
        int pitch=(int)source.pitch;
        int velocity=(int)source.velocity;
        if(gestureVelocity)
        {
            const auto region=midiVelocityBounds();
            velocity=juce::jlimit(1,127,
                (region.getBottom()-event.y)*127/juce::jmax(1,region.getHeight()));
        }
        else
        {
            const auto delta=(std::int64_t)std::llround(
                (double)(event.x-gestureStart.x)*clip->lengthTicks/
                (double)roll.getWidth());
            const auto deltaTicks=LibertyPerformMidiEdit::snap(
                std::abs(delta))* (delta<0?-1:1);
            if(gestureResizing)
                length=juce::jlimit<std::int64_t>(1,
                    clip->lengthTicks-start,source.lengthTicks+deltaTicks);
            else
            {
                start=juce::jlimit<std::int64_t>(0,
                    juce::jmax<std::int64_t>(0,clip->lengthTicks-length),
                    source.startTick+deltaTicks);
                pitch=juce::jlimit(0,127,(int)source.pitch+
                    midiPitchAt(event.y)-midiPitchAt(gestureStart.y));
            }
        }
        if(start==source.startTick && length==source.lengthTicks &&
           pitch==(int)source.pitch && velocity==(int)source.velocity)
        {
            if(gestureHistorySaved)
            {
                clip->notes=gestureOriginal;
                publishMidiNotes();
            }
            return;
        }
        if(!gestureHistorySaved)
        {
            clip->history.checkpoint(gestureOriginal);
            gestureHistorySaved=true;
        }
        clip->notes=gestureOriginal;
        LibertyPerformMidiEdit::edit(clip->notes,gestureNote,start,
            length,pitch,velocity,clip->lengthTicks);
        publishMidiNotes();
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        gestureNote=-1;
        gestureOriginal.clear();
        gestureHistorySaved=false;
        gestureVelocity=false;
        gestureResizing=false;
    }
    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        if(!midiEditorVisible() || event.mods.isPopupMenu())return;
        const auto point=event.getPosition();
        if(!midiPianoBounds().contains(point))return;
        const int hit=midiNoteAt(point);
        if(hit>=0)
        {
            selectedMidiNote=hit;
            refreshMidiNoteControls();
            repaint();
            return;
        }
        const auto* clip=selectedMidiData();
        if(clip==nullptr || clip->notes.size()>=8192)return;
        midiLastClickTick=midiTickAt(point.x);
        const int pitch=midiPitchAt(point.y);
        insertMidiNote();
        if(auto* updated=selectedMidiData())
        {
            if(selectedMidiNote>=0 &&
               selectedMidiNote<(int)updated->notes.size())
            {
                updated->notes[(size_t)selectedMidiNote].pitch=(std::uint8_t)pitch;
                publishMidiNotes();
            }
        }
    }
    bool keyPressed(const juce::KeyPress& key) override
    {
        if(!midiEditorVisible())return false;
        if(key.getModifiers().isCommandDown() &&
           (key.getKeyCode()=='z' || key.getKeyCode()=='Z'))
        {
            if(key.getModifiers().isShiftDown())redoMidiNotes();
            else undoMidiNotes();
            return true;
        }
        if(key.getKeyCode()==juce::KeyPress::deleteKey ||
           key.getKeyCode()==juce::KeyPress::backspaceKey)
        {
            deleteMidiNote();
            return true;
        }
        if(key.getKeyCode()==juce::KeyPress::escapeKey)
        {
            inspectorTab=0;
            refreshInspector();
            return true;
        }
        return false;
    }
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override
    {
        if(midiWorkspaceVisible() && midiPianoBounds().contains(event.getPosition()))
        {
            pianoBasePitch=juce::jlimit(0,92,pianoBasePitch+
                (wheel.deltaY>0.0f?3:-3));
            repaint();
            return;
        }
        juce::Component::mouseWheelMove(event,wheel);
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        if(!midiWorkspaceVisible())
        {
            // A dedicated non-interactive status light stays inside each real
            // clip button; nothing overlays the centered clip name.
            const int count=juce::jmin(trackCount(),(int)clipButtons.size());
            for(int track=0;track<count;++track)
                for(int scene=0;scene<sceneCount;++scene)
                {
                    const auto& button=clipButtons[(size_t)track][(size_t)scene];
                    const auto cell=button.getBounds();
                    if(!button.isVisible()||cell.getWidth()<80||cell.getHeight()<24)
                        continue;
                    const bool queued=player.isTrackQueued(track,scene);
                    const bool active=player.isTrackPlaying(track,scene);
                    if(!queued&&!active)continue;
                    const auto status=queued?LibertyUI::Colour::accentAmber:
                                               LibertyUI::Colour::accentLight;
                    const auto light=juce::Rectangle<float>(
                        (float)cell.getX()+5.f,(float)cell.getY()+5.f,3.f,
                        (float)cell.getHeight()-10.f);
                    g.setColour(status);
                    g.fillRoundedRectangle(light,1.5f);
                }
        }
        if(!midiWorkspaceVisible() && hasSelectedClip() &&
           selectedClipTrack<(int)clipButtons.size())
        {
            g.setColour(juce::Colour(0xffe7c879));
            g.drawRoundedRectangle(clipButtons[(size_t)selectedClipTrack]
                [(size_t)selectedClipScene].getBounds().expanded(2).toFloat(),5.0f,2.0f);
        }
    }

    int inspectorHeight() const noexcept
    {
        if(midiWorkspaceVisible())return 0;
        return hasSelectedClip()?juce::jmin(184,juce::jmax(125,getHeight()/4)):0;
    }
    juce::Rectangle<int> inspectorArea() const noexcept
    {
        if(midiWorkspaceVisible())
            return {18,72,juce::jmax(0,getWidth()-36),
                    juce::jmax(1,getHeight()-134)};
        const int h=inspectorHeight();
        return {18,getHeight()-54-h-6,juce::jmax(0,getWidth()-36),h};
    }

    void resized() override
    {
        syncTrackContainers();
        const int visibleTracks = juce::jmin(trackCount(), (int)trackHeaders.size());
        const int controlsX=juce::jmin(500,juce::jmax(180,getWidth()-330));
        quantizeSelector.setBounds(controlsX,13,172,28);
        loopButton.setBounds(controlsX+180,13,135,28);
        const int leftMargin = 18;
        const int sceneLaunchW = 92;
        const int gridLeft = leftMargin;
        const int gridRight = getWidth() - sceneLaunchW - 28;
        const int gap = 8;
        // Fluid track columns fill the available canvas. The previous 220 px
        // hard limit left half the PERFORMANCE page empty with four tracks.
        // Keep columns disjoint even if a session has many tracks.
        const int available = juce::jmax(1,gridRight-gridLeft);
        const int totalGaps = juce::jmax(0,visibleTracks-1)*gap;
        const int columnW = visibleTracks>0?
            juce::jmin(440,juce::jmax(1,
                (juce::jmax(1,available-totalGaps))/visibleTracks)):110;
        const int headerTop = 72;
        const int headerH = 62;
        const int rowsTop = 142;
        const int footerH = 54;
        const int rowsAvailable=juce::jmax(0,
            getHeight()-rowsTop-footerH-inspectorHeight()-livePanelHeight()-22);
        const int rowH=juce::jlimit(9,86,rowsAvailable/sceneCount);
        const bool showGrid=!midiWorkspaceVisible();

        for (int track = 0; track < visibleTracks; ++track)
        {
            const int x = gridLeft + track * (columnW + gap);
            trackHeaders[(size_t)track] = { x, headerTop, columnW, headerH };
            stopTrackButtons[(size_t)track].setBounds(x + 8, headerTop + 40, columnW - 16, 18);
            stopTrackButtons[(size_t)track].setVisible(showGrid);
            for (int scene = 0; scene < sceneCount; ++scene)
            {
                const int y = rowsTop + scene * rowH;
                clipButtons[(size_t)track][(size_t)scene].setBounds(
                    x+2,y+2,columnW-4,juce::jmax(1,rowH-4));
                clipButtons[(size_t)track][(size_t)scene].setVisible(showGrid);
            }
        }

        for (int scene = 0; scene < sceneCount; ++scene)
        {
            const int y = rowsTop + scene * rowH;
            sceneRows[(size_t)scene] = { 0, y, getWidth(), rowH };
            sceneButtons[(size_t)scene].setBounds(
                getWidth()-sceneLaunchW-18,y+2,sceneLaunchW,juce::jmax(1,rowH-4));
            sceneButtons[(size_t)scene].setVisible(showGrid);
        }
        recordArrangeButton.setBounds(18, getHeight() - 44, 200, 30);
        stopAllButton.setBounds(getWidth() - 140, getHeight() - 44, 122, 30);

        const bool showLive=livePanelHeight()>0;
        const int liveY=getHeight()-54-inspectorHeight()-livePanelHeight()-7;
        const int liveW=juce::jmax(1,getWidth()-60);
        const int gapLive=5;
        const int availableLive=juce::jmax(6,liveW-5*gapLive);
        const int widths[]={availableLive*12/100,availableLive*15/100,
            availableLive*14/100,availableLive*15/100,
            availableLive*30/100,0};
        const int total=widths[0]+widths[1]+widths[2]+widths[3]+widths[4];
        const int lastWidth=juce::jmax(1,availableLive-total);
        int liveX=30;
        for(auto* combo:{&followSceneSelector,&followActionSelector,&followBarsSelector,
                         &midiLearnTargetSelector})
        {
            const int index=combo==&followSceneSelector?0:
                combo==&followActionSelector?1:combo==&followBarsSelector?2:3;
            combo->setBounds(liveX,liveY+16,juce::jmax(1,widths[index]),27);
            combo->setVisible(showLive);
            liveX+=widths[index]+gapLive;
        }
        midiLearnButton.setBounds(liveX,liveY+16,juce::jmax(1,widths[4]),27);
        midiLearnButton.setVisible(showLive);
        liveX+=widths[4]+gapLive;
        midiForgetButton.setBounds(liveX,liveY+16,lastWidth,27);
        midiForgetButton.setVisible(showLive);
        const int macroWidth=juce::jmax(1,(liveW-3*gapLive)/4);
        for(int i=0;i<4;++i)
        {
            liveMacroSliders[(size_t)i].setBounds(
                30+i*(macroWidth+gapLive),liveY+67,macroWidth,28);
            liveMacroSliders[(size_t)i].setVisible(showLive);
        }

        const auto panel=inspectorArea();
        const int x=panel.getX()+16;
        const int width=juce::jmax(1,panel.getWidth()-32);
        const int tabY=panel.getY()+8;
        const int tabGap=5;
        const int tabWidth=juce::jmax(1,(panel.getWidth()-130-4*tabGap)/5);
        int tabX=x;
        for(auto* tab:{&inspectorClipTab,&inspectorLaunchTab,&inspectorMidiTab,
                       &inspectorTransformTab,&inspectorGenerateTab})
        {
            tab->setBounds(tabX,tabY,tabWidth,26);
            tabX+=tabWidth+tabGap;
        }
        inspectorClose.setBounds(panel.getRight()-94,tabY,76,26);
        const int firstY=panel.getY()+56;
        const int secondY=panel.getY()+123;
        const int nameWidth=width*45/100;
        const int colourWidth=width*24/100;
        inspectorName.setBounds(x,firstY,juce::jmax(1,nameWidth-8),27);
        inspectorColour.setBounds(x+width*46/100,firstY,
                                  juce::jmax(1,colourWidth-8),27);
        inspectorLoop.setBounds(x+width*72/100,firstY,
                                juce::jmax(1,width*28/100),27);
        const int third=juce::jmax(1,width/3);
        inspectorGain.setBounds(x,secondY,juce::jmax(1,third-6),26);
        inspectorIn.setBounds(x+third+5,secondY,juce::jmax(1,third-10),26);
        inspectorOut.setBounds(x+2*third+5,secondY,
                               juce::jmax(1,width-2*third-7),26);
        inspectorTranspose.setBounds(x,secondY,juce::jmax(1,third-6),26);
        inspectorMidiBars.setBounds(x+third+5,secondY,
                                    juce::jmax(1,third-10),26);
        inspectorQuantize.setBounds(x,panel.getY()+74,
                                    juce::jmax(1,third-6),30);
        inspectorMode.setBounds(x+third+12,panel.getY()+74,
                                juce::jmax(1,third-6),30);
        const int actionY=panel.getY()+43;
        const int gapButton=6;
        const int usable=juce::jmax(6,width-5*gapButton);
        const int wAdd=usable*12/100,wDelete=usable*15/100;
        const int wHuman=usable*17/100,wAmount=usable*22/100;
        const int wUndo=usable*15/100;
        const int wRedo=juce::jmax(1,usable-wAdd-wDelete-wHuman-wAmount-wUndo);
        int actionX=x;
        midiAddButton.setBounds(actionX,actionY,juce::jmax(1,wAdd),27);
        actionX+=wAdd+gapButton;
        midiDeleteButton.setBounds(actionX,actionY,juce::jmax(1,wDelete),27);
        actionX+=wDelete+gapButton;
        midiHumanizeButton.setBounds(actionX,actionY,juce::jmax(1,wHuman),27);
        actionX+=wHuman+gapButton;
        midiHumanizeAmount.setBounds(actionX,actionY,juce::jmax(1,wAmount),27);
        actionX+=wAmount+gapButton;
        midiUndoButton.setBounds(actionX,actionY,juce::jmax(1,wUndo),27);
        actionX+=wUndo+gapButton;
        midiRedoButton.setBounds(actionX,actionY,wRedo,27);
        const int editY=panel.getY()+92;
        midiPitchSlider.setBounds(x,editY,juce::jmax(1,third-8),27);
        midiVelocitySlider.setBounds(x+third+5,editY,juce::jmax(1,third-10),27);
        midiLengthSlider.setBounds(x+2*third+5,editY,
                                  juce::jmax(1,width-2*third-7),27);

        const int processorGap=8;
        const int five=juce::jmax(1,(width-4*processorGap)/5);
        const int quarter=juce::jmax(1,(width-3*processorGap)/4);
        const int comboY=panel.getY()+69;
        generatorKind.setBounds(x,comboY,five,29);
        generatorStyle.setBounds(x,comboY,five,29);
        generatorDivision.setBounds(x+five+processorGap,comboY,five,29);
        generatorSteps.setBounds(x+2*(five+processorGap),comboY,five,29);
        generatorDirection.setBounds(x+3*(five+processorGap),comboY,five,29);
        generatorOctaves.setBounds(x+4*(five+processorGap),comboY,five,29);
        const int slidersY=panel.getY()+137;
        for(auto* slider:{&generatorDensity,&generatorSwing,
                          &generatorVariation,&generatorVelocity})
        {
            const auto i=slider==&generatorDensity?0:
                         slider==&generatorSwing?1:
                         slider==&generatorVariation?2:3;
            slider->setBounds(x+i*(quarter+processorGap),slidersY,quarter,29);
        }
        const int buttonsY=panel.getY()+182;
        const int actionW=juce::jmax(1,(width-3*processorGap)/4);
        int buttonsX=x;
        for(auto* button:{&generatorPreviewButton,&generatorApplyButton,
                          &generatorOriginalButton,&generatorNewVariantButton})
        {
            button->setBounds(buttonsX,buttonsY,actionW,30);
            buttonsX+=actionW+processorGap;
        }
    }

private:
    void syncTrackContainers()
    {
        const auto wantedTracks = (size_t)juce::jmax(0, trackCount());
        const auto wantedAudio = (size_t)juce::jmax(0, audioTrackCount());
        const auto oldTracks = clipButtons.size();
        if (clipButtons.size() != wantedTracks) clipButtons.resize(wantedTracks);
        if (stopTrackButtons.size() != wantedTracks) stopTrackButtons.resize(wantedTracks);
        if (trackHeaders.size() != wantedTracks) trackHeaders.resize(wantedTracks);
        if (activeTrackScene.size() != wantedTracks) activeTrackScene.resize(wantedTracks, -1);
        if (performAudioFiles.size() != wantedAudio) performAudioFiles.resize(wantedAudio);
        if (performMidiClips.size() != wantedTracks) performMidiClips.resize(wantedTracks);
        if (clipMetadata.size() != wantedTracks) clipMetadata.resize(wantedTracks);
        if (selectedClipTrack >= (int)wantedTracks) selectedClipTrack=selectedClipScene=-1;
        player.syncTrackCount();
        for (size_t t = oldTracks; t < wantedTracks; ++t)
        {
            auto& stop = stopTrackButtons[t];
            stop.setButtonText("STOP");
            stop.setMouseClickGrabsKeyboardFocus(false);
            stop.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252b32));
            stop.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            stop.onClick = [this, t]
            {
                const int track = (int)t;
                player.stopTrack(track);
                if (track < (int)activeTrackScene.size()) activeTrackScene[t] = -1;
                refreshClipLabels();
                repaint();
            };
            addAndMakeVisible(stop);
            for (int scene = 0; scene < sceneCount; ++scene)
            {
                auto& cell = clipButtons[t][(size_t)scene];
                cell.setMouseClickGrabsKeyboardFocus(false);
                cell.onClick = [this, t, scene] { launchClip((int)t, scene); };
                addAndMakeVisible(cell);
            }
        }
        if (dragTrack < -1 || dragTrack >= (int)wantedTracks || dragScene < -1 || dragScene >= sceneCount) dragTrack = dragScene = -1;
    }

    int audioTrackCount() const noexcept { return owner.getAudioTrackCount(); }
    int trackCount() const noexcept { return owner.getTotalArrangeTrackCount(); }
    int firstMidiTrack() const noexcept { return audioTrackCount(); }
    int firstInstrumentTrack() const noexcept { return audioTrackCount() + owner.getMidiTrackCount(); }

    juce::String trackName(int track) const
    {
        const auto name = getLibertyTrackName(track);
        if (name.isNotEmpty()) return name;
        if (track < firstMidiTrack()) return "Audio " + juce::String(track + 1);
        if (track < firstInstrumentTrack()) return "MIDI " + juce::String(track - firstMidiTrack() + 1);
        return "Instrument " + juce::String(track - firstInstrumentTrack() + 1);
    }

    juce::String trackType(int track) const
    {
        if (track < firstMidiTrack()) return "AUDIO";
        if (track < firstInstrumentTrack()) return "MIDI";
        return "INSTRUMENT";
    }

    juce::Colour colourForTrack(int track) const
    {
        return trackColour(getLibertyTrackColourId(track));
    }

    struct ClipMetadata
    {
        juce::String name;
        int colourId=-1;
        LibertyPerformClip::Settings settings;
    };

    struct SessionMidiData {
        bool valid=false;
        std::int64_t lengthTicks=3840;
        std::vector<MidiEngine::NoteEvent> notes;
        LibertyPerformMidiEdit::History<MidiEngine::NoteEvent> history;
        LibertyPerformGenerator::Parameters generator;
        std::vector<MidiEngine::NoteEvent> generatedNotes;
        int generatorState=0; // 0: original, 1: temporary preview, 2: active and saved
        juce::File source;
    };
    static bool isSupportedMidiFile(const juce::File& file) {
        const auto ext=file.getFileExtension().toLowerCase();
        return ext==".mid" || ext==".midi";
    }
    static bool parseMidiFile(const juce::File& file,SessionMidiData& data,juce::String& error) {
        auto input=file.createInputStream();
        juce::MidiFile midi;
        if(input==nullptr || !midi.readFrom(*input) || midi.getTimeFormat()<=0) {
            error=juce::String::fromUTF8(
                "Le fichier MIDI doit utiliser une résolution PPQ valide.");
            return false;
        }
        const double ppq=(double)midi.getTimeFormat();
        data={};
        std::int64_t longest=0;
        for(int track=0;track<midi.getNumTracks();++track) {
            const auto* source=midi.getTrack(track);
            if(source==nullptr)continue;
            juce::MidiMessageSequence sequence(*source);
            sequence.updateMatchedPairs();
            for(int i=0;i<sequence.getNumEvents();++i) {
                const auto* event=sequence.getEventPointer(i);
                if(event==nullptr || !event->message.isNoteOn()
                    || event->noteOffObject==nullptr || data.notes.size()>=8192)continue;
                const double first=event->message.getTimeStamp()*960.0/ppq;
                const double last=event->noteOffObject->message.getTimeStamp()*960.0/ppq;
                if(!std::isfinite(first) || !std::isfinite(last) || first<0.0 || last<=first)continue;
                MidiEngine::NoteEvent note;
                note.startTick=(std::int64_t)std::llround(first);
                note.lengthTicks=juce::jmax<std::int64_t>(1,(std::int64_t)std::llround(last)-note.startTick);
                note.pitch=(std::uint8_t)event->message.getNoteNumber();
                note.velocity=(std::uint8_t)juce::jlimit(1,127,(int)std::lround(event->message.getVelocity()*127.f));
                note.channel=(std::uint8_t)event->message.getChannel();
                longest=juce::jmax(longest,note.startTick+note.lengthTicks);
                data.notes.push_back(note);
            }
        }
        data.lengthTicks=juce::jmax<std::int64_t>(3840,((longest+3839)/3840)*3840);
        data.source=file;data.valid=true;return true;
    }
    bool assignMidiClip(int track,int scene,const SessionMidiData& data) {
        if(track<audioTrackCount() || track>=(int)performMidiClips.size()
            || scene<0 || scene>=sceneCount || !data.valid)return false;
        if(!player.prepareMidiClip(track,scene,data.notes,data.lengthTicks))return false;
        performMidiClips[(size_t)track][(size_t)scene]=data;
        clipMetadata[(size_t)track][(size_t)scene]={};
        return true;
    }
    bool createMidiFromTrack(int track,int scene) {
        SessionMidiData data;data.valid=true;
        if(track<firstInstrumentTrack()) {
            if(const auto* engine=owner.getMidiEngine(track-firstMidiTrack()))
                data.notes=engine->getNotesCopy();
        } else if(auto* pattern=owner.getInstrumentStepSequencer(track-firstInstrumentTrack())) {
            data.notes=LibertyStepSequencer::render(*pattern);
            data.lengthTicks=juce::jmax<std::int64_t>(3840,LibertyStepSequencer::getCycleLengthTicks(*pattern));
        }
        std::int64_t last=0;
        for(const auto& note:data.notes)last=juce::jmax(last,note.startTick+note.lengthTicks);
        data.lengthTicks=juce::jmax(data.lengthTicks,((last+3839)/3840)*3840);
        if(!assignMidiClip(track,scene,data))return false;
        ++sessionRevision;return true;
    }
    int launchQuantization() const noexcept {
        return juce::jlimit(0,2,quantizeSelector.getSelectedId()-1);
    }

    bool hasSelectedClip() const
    {
        return selectedClipTrack>=0 && selectedClipScene>=0 &&
            selectedClipTrack<(int)clipMetadata.size() &&
            slotHasClip(selectedClipTrack,selectedClipScene);
    }

    void initialiseInspector()
    {
        inspectorClipTab.setButtonText("CLIP");
        inspectorLaunchTab.setButtonText("LANCEMENT");
        inspectorMidiTab.setButtonText("PIANO ROLL");
        inspectorTransformTab.setButtonText("TRANSFORMER");
        inspectorGenerateTab.setButtonText(juce::String::fromUTF8("GÉNÉRER"));
        inspectorClose.setButtonText("FERMER");
        inspectorClipTab.onClick=[this]{inspectorTab=0;refreshInspector();};
        inspectorLaunchTab.onClick=[this]{inspectorTab=1;refreshInspector();};
        inspectorMidiTab.onClick=[this]{
            inspectorTab=2;
            focusPianoOnSource();
            refreshInspector();
        };
        inspectorTransformTab.onClick=[this]{
            inspectorTab=3;
            focusPianoOnSource();
            refreshInspector();
        };
        inspectorGenerateTab.onClick=[this]{
            inspectorTab=4;
            pianoBasePitch=24;
            refreshInspector();
        };
        inspectorClose.onClick=[this]{
            selectedClipTrack=selectedClipScene=-1;
            refreshInspector();
        };
        for(auto* button:{&inspectorClipTab,&inspectorLaunchTab,&inspectorMidiTab,
                          &inspectorTransformTab,&inspectorGenerateTab,&inspectorClose})
        {
            button->setMouseClickGrabsKeyboardFocus(false);
            addAndMakeVisible(*button);
        }
        inspectorName.setTextToShowWhenEmpty("Nom du clip",juce::Colour(0xff8896a5));
        inspectorName.onTextChange=[this]{commitInspector();};
        addAndMakeVisible(inspectorName);
        inspectorColour.addItem("COULEUR PISTE",1);
        const juce::StringArray colours{
            "BLEU NUIT","BLEU","VERT","JAUNE","ORANGE","ROUGE",
            "VIOLET","ROSE","TURQUOISE"
        };
        for(int i=0;i<colours.size();++i)
            inspectorColour.addItem(colours[i],i+2);
        inspectorColour.onChange=[this]{commitInspector();};
        addAndMakeVisible(inspectorColour);
        inspectorLoop.setButtonText("BOUCLE DU CLIP");
        inspectorLoop.onClick=[this]{commitInspector();};
        addAndMakeVisible(inspectorLoop);

        const auto sliderSetup=[this](juce::Slider& slider,double lo,double hi,
                                       double step,const juce::String& suffix)
        {
            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(juce::Slider::TextBoxRight,false,62,24);
            slider.setRange(lo,hi,step);
            slider.setTextValueSuffix(suffix);
            slider.onValueChange=[this]{commitInspector();};
            addAndMakeVisible(slider);
        };
        sliderSetup(inspectorGain,-24.0,12.0,0.1," dB");
        sliderSetup(inspectorTranspose,-24.0,24.0,1.0," st");
        sliderSetup(inspectorIn,0.0,99.0,1.0," %");
        sliderSetup(inspectorOut,1.0,100.0,1.0," %");

        for(const auto& label:{"AUTO","1 MESURE","2 MESURES","4 MESURES",
                               "8 MESURES","16 MESURES"})
            inspectorMidiBars.addItem(label,inspectorMidiBars.getNumItems()+1);
        inspectorMidiBars.onChange=[this]{commitInspector();};
        addAndMakeVisible(inspectorMidiBars);
        inspectorQuantize.addItem("GLOBAL",1);
        inspectorQuantize.addItem(juce::String::fromUTF8("IMMÉDIAT"),2);
        inspectorQuantize.addItem("1 TEMPS",3);
        inspectorQuantize.addItem("1 MESURE",4);
        inspectorQuantize.onChange=[this]{commitInspector();};
        addAndMakeVisible(inspectorQuantize);
        inspectorMode.addItem("RELANCER",1);
        inspectorMode.addItem("BASCULE",2);
        inspectorMode.onChange=[this]{commitInspector();};
        addAndMakeVisible(inspectorMode);
        midiAddButton.setButtonText(juce::String::fromUTF8("INSÉRER"));
        midiDeleteButton.setButtonText("SUPPRIMER");
        midiHumanizeButton.setButtonText("HUMANISER");
        midiUndoButton.setButtonText("ANNULER");
        midiRedoButton.setButtonText(juce::String::fromUTF8("RÉTABLIR"));
        midiAddButton.onClick=[this]{insertMidiNote();};
        midiDeleteButton.onClick=[this]{deleteMidiNote();};
        midiHumanizeButton.onClick=[this]{humanizeMidiNotes();};
        midiUndoButton.onClick=[this]{undoMidiNotes();};
        midiRedoButton.onClick=[this]{redoMidiNotes();};
        for(auto* button:{&midiAddButton,&midiDeleteButton,&midiHumanizeButton,
                           &midiUndoButton,&midiRedoButton})
        {
            button->setMouseClickGrabsKeyboardFocus(false);
            addAndMakeVisible(*button);
        }
        const auto setupMidi=[this](juce::Slider& slider,double min,double max,
                                    double step,const juce::String& suffix)
        {
            slider.setRange(min,max,step);
            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(juce::Slider::TextBoxRight,false,56,24);
            slider.setTextValueSuffix(suffix);
            slider.onDragStart=[this]{midiSliderGesture=false;};
            slider.onDragEnd=[this]{midiSliderGesture=false;};
            slider.onValueChange=[this,&slider]
            {
                if(&slider!=&midiHumanizeAmount)commitMidiSlider();
            };
            addAndMakeVisible(slider);
        };
        setupMidi(midiPitchSlider,0,127,1,"");
        setupMidi(midiVelocitySlider,1,127,1,"");
        setupMidi(midiLengthSlider,1,15360,1," tk");
        setupMidi(midiHumanizeAmount,0,100,1," %");
        midiPitchSlider.setValue(60,juce::dontSendNotification);
        midiVelocitySlider.setValue(100,juce::dontSendNotification);
        midiLengthSlider.setValue(240,juce::dontSendNotification);
        midiHumanizeAmount.setValue(25,juce::dontSendNotification);
        initialiseGenerator();
        refreshInspector();
    }

    void selectClip(int track,int scene)
    {
        if(track<0 || track>=(int)clipMetadata.size() ||
            scene<0 || scene>=sceneCount || !slotHasClip(track,scene))return;
        if(selectedClipTrack!=track || selectedClipScene!=scene)
        {
            cancelTemporaryGeneratorPreview();
            selectedMidiNote=-1;
            midiSliderGesture=false;
            gestureNote=-1;
            pianoBasePitch=48;
        }
        selectedClipTrack=track;
        selectedClipScene=scene;
        if(track<audioTrackCount() && inspectorTab>=2)inspectorTab=0;
        refreshInspector();
    }

    void refreshInspector()
    {
        const bool visible=hasSelectedClip();
        updatingInspector=true;
        const bool clipPage=visible && inspectorTab==0;
        const bool audio=visible && selectedClipTrack<audioTrackCount();
        const bool midi=visible && !audio;
        inspectorClipTab.setVisible(visible);
        inspectorLaunchTab.setVisible(visible);
        inspectorMidiTab.setVisible(midi);
        inspectorTransformTab.setVisible(midi);
        inspectorGenerateTab.setVisible(midi);
        inspectorClose.setVisible(visible);
        inspectorName.setVisible(clipPage);
        inspectorColour.setVisible(clipPage);
        inspectorLoop.setVisible(clipPage);
        inspectorGain.setVisible(clipPage&&audio);
        inspectorIn.setVisible(clipPage&&audio);
        inspectorOut.setVisible(clipPage&&audio);
        inspectorTranspose.setVisible(clipPage&&midi);
        inspectorMidiBars.setVisible(clipPage&&midi);
        inspectorQuantize.setVisible(visible&&inspectorTab==1);
        inspectorMode.setVisible(visible&&inspectorTab==1);
        const bool piano=midi&&inspectorTab==2;
        const bool generate=midi && (inspectorTab==3 || inspectorTab==4);
        generatorKind.setVisible(generate && inspectorTab==3);
        generatorStyle.setVisible(generate && inspectorTab==4);
        const bool arpeggiator=generate && inspectorTab==3 &&
            generatorKind.getSelectedId()!=2;
        generatorDirection.setVisible(arpeggiator);
        generatorOctaves.setVisible(arpeggiator);
        generatorDivision.setVisible(generate);
        generatorSteps.setVisible(arpeggiator || (generate && inspectorTab==4));
        for(auto* slider:{&generatorDensity,&generatorSwing,
                          &generatorVariation,&generatorVelocity})
            slider->setVisible(generate);
        for(auto* button:{&generatorPreviewButton,&generatorApplyButton,
                           &generatorOriginalButton,&generatorNewVariantButton})
            button->setVisible(generate);
        for(auto* button:{&midiAddButton,&midiDeleteButton,&midiHumanizeButton,
                           &midiUndoButton,&midiRedoButton})
            button->setVisible(piano);
        for(auto* slider:{&midiPitchSlider,&midiVelocitySlider,
                          &midiLengthSlider,&midiHumanizeAmount})
            slider->setVisible(piano);
        if(visible)
        {
            const auto& data=clipMetadata[(size_t)selectedClipTrack][(size_t)selectedClipScene];
            const auto& p=data.settings;
            inspectorName.setText(data.name,false);
            inspectorColour.setSelectedId(data.colourId+2,juce::dontSendNotification);
            inspectorLoop.setToggleState(p.loop,juce::dontSendNotification);
            inspectorGain.setValue(p.gainDb,juce::dontSendNotification);
            inspectorTranspose.setValue(p.transpose,juce::dontSendNotification);
            inspectorIn.setValue(p.inFraction*100.f,juce::dontSendNotification);
            inspectorOut.setValue(p.outFraction*100.f,juce::dontSendNotification);
            const int lengths[]={0,1,2,4,8,16};
            int selectedLength=1;
            for(int i=0;i<6;++i)if(lengths[i]==p.midiBars)selectedLength=i+1;
            inspectorMidiBars.setSelectedId(selectedLength,juce::dontSendNotification);
            inspectorQuantize.setSelectedId(p.quantization+2,juce::dontSendNotification);
            inspectorMode.setSelectedId(p.launchMode+1,juce::dontSendNotification);
        }
        updatingInspector=false;
        refreshMidiNoteControls();
        refreshGeneratorControls();
        resized();
        repaint();
    }

    void commitInspector()
    {
        if(updatingInspector || !hasSelectedClip())return;
        auto& data=clipMetadata[(size_t)selectedClipTrack][(size_t)selectedClipScene];
        auto p=data.settings;
        if(inspectorTab==0)
        {
            data.name=inspectorName.getText().trim().substring(0,80);
            data.colourId=juce::jlimit(-1,8,inspectorColour.getSelectedId()-2);
            p.loop=inspectorLoop.getToggleState();
            if(selectedClipTrack<audioTrackCount())
            {
                p.gainDb=(float)inspectorGain.getValue();
                p.inFraction=(float)(inspectorIn.getValue()/100.0);
                p.outFraction=(float)(inspectorOut.getValue()/100.0);
                if(p.outFraction-p.inFraction<0.01f)
                {
                    if(p.outFraction>=1.0f)p.inFraction=0.99f;
                    else p.outFraction=p.inFraction+0.01f;
                }
            }
            else
            {
                p.transpose=(int)inspectorTranspose.getValue();
                const int lengths[]={0,1,2,4,8,16};
                p.midiBars=lengths[juce::jlimit(0,5,inspectorMidiBars.getSelectedId()-1)];
            }
        }
        else
        {
            p.quantization=juce::jlimit(-1,2,inspectorQuantize.getSelectedId()-2);
            p.launchMode=juce::jlimit(0,1,inspectorMode.getSelectedId()-1);
        }
        data.settings=LibertyPerformClip::validated(p);
        player.updateClipSettings(selectedClipTrack,selectedClipScene,data.settings,
            owner.getTimeSignatureNumerator(),owner.getTimeSignatureDenominator());
        ++sessionRevision;
        refreshClipLabels();
        repaint();
    }

    void initialiseGenerator()
    {
        generatorKind.addItem(juce::String::fromUTF8("ARPÉGIATEUR"),1);
        generatorKind.addItem("VARIATIONS",2);
        generatorStyle.addItem("HOUSE",1);
        generatorStyle.addItem("TECHNO",2);
        generatorStyle.addItem("HIP-HOP",3);
        generatorStyle.addItem("BREAKBEAT",4);
        generatorDivision.addItem("1/8",1);
        generatorDivision.addItem("1/16",2);
        generatorDivision.addItem("1/32",3);
        generatorDirection.addItem("MONTANT",1);
        generatorDirection.addItem("DESCENDANT",2);
        generatorDirection.addItem("ALLER-RETOUR",3);
        generatorDirection.addItem(juce::String::fromUTF8("ALÉATOIRE"),4);
        for(int i=1;i<=4;++i)
            generatorOctaves.addItem(juce::String(i)+" OCT.",i);
        for(const int steps:{8,16,32,64})
            generatorSteps.addItem(juce::String(steps)+" PAS",
                                   generatorSteps.getNumItems()+1);
        for(auto* combo:{&generatorKind,&generatorStyle,&generatorDivision,
                         &generatorDirection,&generatorOctaves,&generatorSteps})
        {
            combo->onChange=[this]{commitGenerator();};
            addAndMakeVisible(*combo);
        }
        const auto setup=[this](juce::Slider& slider,int min,int max,
                                 int initial,const juce::String& suffix)
        {
            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(juce::Slider::TextBoxRight,false,58,24);
            slider.setRange(min,max,1);
            slider.setTextValueSuffix(suffix);
            slider.setValue(initial,juce::dontSendNotification);
            slider.onValueChange=[this]{commitGenerator();};
            addAndMakeVisible(slider);
        };
        setup(generatorDensity,0,100,75," %");
        setup(generatorSwing,0,75,0," %");
        setup(generatorVariation,0,100,30," %");
        setup(generatorVelocity,1,127,100,"");
        generatorPreviewButton.setButtonText(juce::String::fromUTF8("PRÉÉCOUTER"));
        generatorApplyButton.setButtonText("ACTIVER");
        generatorOriginalButton.setButtonText("ORIGINAL");
        generatorNewVariantButton.setButtonText("AUTRE VARIATION");
        generatorPreviewButton.onClick=[this]{
            auto* clip=selectedMidiData();
            if(clip!=nullptr)setGeneratorState(clip->generatorState==1?0:1);
        };
        generatorApplyButton.onClick=[this]{setGeneratorState(2);};
        generatorOriginalButton.onClick=[this]{setGeneratorState(0);};
        generatorNewVariantButton.onClick=[this]{nextGeneratorVariation();};
        for(auto* button:{&generatorPreviewButton,&generatorApplyButton,
                          &generatorOriginalButton,&generatorNewVariantButton})
        {
            button->setMouseClickGrabsKeyboardFocus(false);
            addAndMakeVisible(*button);
        }
    }
    LibertyPerformGenerator::Parameters readGeneratorParams() const
    {
        auto p=selectedMidiData()!=nullptr?selectedMidiData()->generator:
            LibertyPerformGenerator::Parameters{};
        if(inspectorTab==4)p.kind=2;
        else p.kind=generatorKind.getSelectedId()==2?1:0;
        p.rhythmStyle=juce::jlimit(0,3,generatorStyle.getSelectedId()-1);
        p.division=juce::jlimit(0,2,generatorDivision.getSelectedId()-1);
        p.direction=juce::jlimit(0,3,generatorDirection.getSelectedId()-1);
        p.octaves=juce::jlimit(1,4,generatorOctaves.getSelectedId());
        const int counts[]={8,16,32,64};
        p.steps=counts[juce::jlimit(0,3,generatorSteps.getSelectedId()-1)];
        p.density=(int)generatorDensity.getValue();
        p.swing=(int)generatorSwing.getValue();
        p.variation=(int)generatorVariation.getValue();
        p.velocity=(int)generatorVelocity.getValue();
        return LibertyPerformGenerator::validated(p);
    }
    void refreshGeneratorControls()
    {
        auto* clip=selectedMidiData();
        updatingGenerator=true;
        if(clip!=nullptr)
        {
            const auto& p=clip->generator;
            generatorKind.setSelectedId(p.kind==1?2:1,juce::dontSendNotification);
            generatorStyle.setSelectedId(p.rhythmStyle+1,juce::dontSendNotification);
            generatorDivision.setSelectedId(p.division+1,juce::dontSendNotification);
            generatorDirection.setSelectedId(p.direction+1,juce::dontSendNotification);
            generatorOctaves.setSelectedId(p.octaves,juce::dontSendNotification);
            const int counts[]={8,16,32,64};
            for(int i=0;i<4;++i)
                if(counts[i]==p.steps)
                    generatorSteps.setSelectedId(i+1,juce::dontSendNotification);
            generatorDensity.setValue(p.density,juce::dontSendNotification);
            generatorSwing.setValue(p.swing,juce::dontSendNotification);
            generatorVariation.setValue(p.variation,juce::dontSendNotification);
            generatorVelocity.setValue(p.velocity,juce::dontSendNotification);
            generatorPreviewButton.setButtonText(
                clip->generatorState==1?juce::String::fromUTF8("FIN PRÉÉCOUTE"):
                juce::String::fromUTF8("PRÉÉCOUTER"));
            generatorApplyButton.setButtonText(
                clip->generatorState==2?"ACTIF":"ACTIVER");
            const bool hasSource=!clip->notes.empty();
            const bool canGenerate=inspectorTab==4 || hasSource;
            generatorPreviewButton.setEnabled(canGenerate);
            generatorApplyButton.setEnabled(canGenerate);
            generatorNewVariantButton.setEnabled(canGenerate);
            generatorOriginalButton.setEnabled(clip->generatorState!=0);
        }
        updatingGenerator=false;
        if(midiGeneratorVisible())
        {
            const bool arp=inspectorTab==3 && generatorKind.getSelectedId()==1;
            generatorDirection.setVisible(arp);
            generatorOctaves.setVisible(arp);
            generatorSteps.setVisible(arp || inspectorTab==4);
            if(clip!=nullptr && clip->generatorState==0)
                clip->generatedNotes=LibertyPerformGenerator::render(
                    clip->notes,clip->lengthTicks,readGeneratorParams());
        }
    }
    bool publishGeneratorLayer()
    {
        auto* clip=selectedMidiData();
        if(clip==nullptr)return false;
        clip->generatedNotes=LibertyPerformGenerator::render(
            clip->notes,clip->lengthTicks,clip->generator);
        const auto& audible=clip->generatorState==0?
            clip->notes:clip->generatedNotes;
        const bool ok=player.replaceMidiNotes(selectedClipTrack,selectedClipScene,
            audible,clip->lengthTicks,owner.getTimeSignatureNumerator(),
            owner.getTimeSignatureDenominator());
        if(ok)
        {
            ++sessionRevision;
            refreshGeneratorControls();
            repaint();
        }
        return ok;
    }
    void commitGenerator()
    {
        if(updatingGenerator || !midiGeneratorVisible())return;
        auto* clip=selectedMidiData();
        if(clip==nullptr)return;
        clip->generator=readGeneratorParams();
        publishGeneratorLayer();
    }
    void setGeneratorState(int state)
    {
        auto* clip=selectedMidiData();
        if(clip==nullptr)return;
        if(state!=0)
        {
            clip->generator=readGeneratorParams();
            if(clip->generator.kind!=2 && clip->notes.empty())return;
        }
        clip->generatorState=juce::jlimit(0,2,state);
        publishGeneratorLayer();
    }
    void nextGeneratorVariation()
    {
        auto* clip=selectedMidiData();
        if(clip==nullptr)return;
        clip->generator=readGeneratorParams();
        if(clip->generator.kind!=2 && clip->notes.empty())return;
        clip->generator.seed=LibertyPerformGenerator::next(clip->generator.seed);
        if(clip->generatorState==0)clip->generatorState=1;
        publishGeneratorLayer();
    }
    void cancelTemporaryGeneratorPreview()
    {
        auto* clip=selectedMidiData();
        if(clip==nullptr || clip->generatorState!=1)return;
        clip->generatorState=0;
        clip->generatedNotes.clear();
        player.replaceMidiNotes(selectedClipTrack,selectedClipScene,clip->notes,
            clip->lengthTicks,owner.getTimeSignatureNumerator(),
            owner.getTimeSignatureDenominator());
        refreshGeneratorControls();
    }

    void focusPianoOnSource()
    {
        if(const auto* clip=selectedMidiData())
        {
            int lowest=128;
            for(const auto& note:clip->notes)
                lowest=juce::jmin(lowest,(int)note.pitch);
            pianoBasePitch=lowest<128?juce::jlimit(0,92,lowest-5):48;
        }
    }

    bool midiEditorVisible() const
    {
        return hasSelectedClip() && selectedClipTrack>=audioTrackCount() &&
               inspectorTab==2;
    }
    bool midiGeneratorVisible() const
    {
        return hasSelectedClip() && selectedClipTrack>=audioTrackCount() &&
               (inspectorTab==3 || inspectorTab==4);
    }
    bool midiWorkspaceVisible() const
    {
        return midiEditorVisible() || midiGeneratorVisible();
    }
    SessionMidiData* selectedMidiData()
    {
        if(!hasSelectedClip() || selectedClipTrack<audioTrackCount() ||
           selectedClipTrack>=(int)performMidiClips.size())return nullptr;
        return &performMidiClips[(size_t)selectedClipTrack][(size_t)selectedClipScene];
    }
    const SessionMidiData* selectedMidiData() const
    {
        if(!hasSelectedClip() || selectedClipTrack<audioTrackCount() ||
           selectedClipTrack>=(int)performMidiClips.size())return nullptr;
        return &performMidiClips[(size_t)selectedClipTrack][(size_t)selectedClipScene];
    }
    void refreshMidiNoteControls()
    {
        const auto* clip=selectedMidiData();
        const bool noteSelected=clip!=nullptr && selectedMidiNote>=0 &&
                                selectedMidiNote<(int)clip->notes.size();
        updatingMidiSliders=true;
        midiPitchSlider.setEnabled(noteSelected);
        midiVelocitySlider.setEnabled(noteSelected);
        midiLengthSlider.setEnabled(noteSelected);
        midiDeleteButton.setEnabled(noteSelected);
        midiHumanizeButton.setEnabled(clip!=nullptr && !clip->notes.empty());
        midiUndoButton.setEnabled(clip!=nullptr && !clip->history.undo.empty());
        midiRedoButton.setEnabled(clip!=nullptr && !clip->history.redo.empty());
        midiAddButton.setEnabled(clip!=nullptr && clip->notes.size()<8192);
        if(noteSelected)
        {
            const auto& note=clip->notes[(size_t)selectedMidiNote];
            midiPitchSlider.setValue((double)note.pitch,juce::dontSendNotification);
            midiVelocitySlider.setValue((double)note.velocity,juce::dontSendNotification);
            midiLengthSlider.setValue((double)note.lengthTicks,juce::dontSendNotification);
            const int pitch=(int)note.pitch;
            if(pitch<pianoBasePitch)pianoBasePitch=juce::jlimit(0,92,pitch);
            if(pitch>pianoBasePitch+35)pianoBasePitch=juce::jlimit(0,92,pitch-35);
        }
        updatingMidiSliders=false;
    }

    bool publishMidiNotes()
    {
        auto* clip=selectedMidiData();
        if(clip==nullptr || !clip->valid)return false;
        if(clip->generatorState!=0)
            clip->generatedNotes=LibertyPerformGenerator::render(
                clip->notes,clip->lengthTicks,clip->generator);
        const auto& rendered=clip->generatorState==0?
            clip->notes:clip->generatedNotes;
        if(!player.replaceMidiNotes(selectedClipTrack,selectedClipScene,
                                    rendered,clip->lengthTicks,
                                    owner.getTimeSignatureNumerator(),
                                    owner.getTimeSignatureDenominator()))return false;
        ++sessionRevision;
        refreshMidiNoteControls();
        repaint();
        return true;
    }
    void commitMidiSlider()
    {
        if(updatingMidiSliders || !midiEditorVisible())return;
        auto* clip=selectedMidiData();
        if(clip==nullptr || selectedMidiNote<0 ||
           selectedMidiNote>=(int)clip->notes.size())return;
        const auto& note=clip->notes[(size_t)selectedMidiNote];
        const int newPitch=(int)midiPitchSlider.getValue();
        const int newVelocity=(int)midiVelocitySlider.getValue();
        const auto newLength=(std::int64_t)midiLengthSlider.getValue();
        const auto limit=juce::jmax<std::int64_t>(1,clip->lengthTicks-note.startTick);
        if((int)note.pitch==newPitch && (int)note.velocity==newVelocity &&
           note.lengthTicks==juce::jlimit<std::int64_t>(1,limit,newLength))return;
        if(!midiSliderGesture)
        {
            clip->history.checkpoint(clip->notes);
            midiSliderGesture=true;
        }
        LibertyPerformMidiEdit::edit(clip->notes,selectedMidiNote,note.startTick,
            newLength,newPitch,newVelocity,clip->lengthTicks);
        publishMidiNotes();
    }
    void insertMidiNote()
    {
        auto* clip=selectedMidiData();
        if(clip==nullptr || clip->lengthTicks<=0 || clip->notes.size()>=8192)return;
        clip->history.checkpoint(clip->notes);
        MidiEngine::NoteEvent note;
        note.startTick=juce::jlimit<std::int64_t>(0,clip->lengthTicks-1,
            LibertyPerformMidiEdit::snap(midiLastClickTick));
        note.lengthTicks=juce::jmin<std::int64_t>(
            LibertyPerformMidiEdit::defaultGrid,clip->lengthTicks-note.startTick);
        note.pitch=(std::uint8_t)juce::jlimit(0,127,(int)midiPitchSlider.getValue());
        note.velocity=(std::uint8_t)juce::jlimit(1,127,(int)midiVelocitySlider.getValue());
        clip->notes.push_back(note);
        selectedMidiNote=(int)clip->notes.size()-1;
        publishMidiNotes();
    }
    void deleteMidiNote()
    {
        auto* clip=selectedMidiData();
        if(clip==nullptr || selectedMidiNote<0 ||
           selectedMidiNote>=(int)clip->notes.size())return;
        clip->history.checkpoint(clip->notes);
        clip->notes.erase(clip->notes.begin()+selectedMidiNote);
        selectedMidiNote=-1;
        publishMidiNotes();
    }
    void humanizeMidiNotes()
    {
        auto* clip=selectedMidiData();
        if(clip==nullptr || clip->notes.empty())return;
        const int strength=(int)midiHumanizeAmount.getValue();
        if(strength<=0)return;
        clip->history.checkpoint(clip->notes);
        humanizeSeed=LibertyPerformMidiEdit::nextRandom(humanizeSeed);
        LibertyPerformMidiEdit::humanize(clip->notes,clip->lengthTicks,
                                        strength,humanizeSeed);
        publishMidiNotes();
    }
    void undoMidiNotes()
    {
        auto* clip=selectedMidiData();
        if(clip==nullptr || !clip->history.undoStep(clip->notes))return;
        if(selectedMidiNote>=(int)clip->notes.size())selectedMidiNote=-1;
        publishMidiNotes();
    }
    void redoMidiNotes()
    {
        auto* clip=selectedMidiData();
        if(clip==nullptr || !clip->history.redoStep(clip->notes))return;
        if(selectedMidiNote>=(int)clip->notes.size())selectedMidiNote=-1;
        publishMidiNotes();
    }

    static constexpr int visibleMidiPitches=36;
    juce::Rectangle<int> midiPianoBounds() const
    {
        const auto panel=inspectorArea();
        if(midiGeneratorVisible())
            return {panel.getX()+70,panel.getY()+258,
                    juce::jmax(0,panel.getWidth()-90),
                    juce::jmax(0,panel.getHeight()-320)};
        return {panel.getX()+70,panel.getY()+132,
                juce::jmax(0,panel.getWidth()-90),
                juce::jmax(0,panel.getHeight()-190)};
    }
    juce::Rectangle<int> midiVelocityBounds() const
    {
        const auto roll=midiPianoBounds();
        return {roll.getX(),roll.getBottom()+7,roll.getWidth(),42};
    }
    juce::Rectangle<int> midiNoteBounds(const MidiEngine::NoteEvent& note) const
    {
        const auto area=midiPianoBounds();
        const auto* clip=selectedMidiData();
        if(clip==nullptr || area.isEmpty() || clip->lengthTicks<=0 ||
           (int)note.pitch<pianoBasePitch ||
           (int)note.pitch>=pianoBasePitch+visibleMidiPitches)return {};
        const auto scale=(double)area.getWidth()/(double)clip->lengthTicks;
        const int x=area.getX()+(int)std::llround(note.startTick*scale);
        const int end=area.getX()+(int)std::llround(
            juce::jmin<std::int64_t>(clip->lengthTicks,
                                    note.startTick+note.lengthTicks)*scale);
        const int row=(int)note.pitch-pianoBasePitch;
        const int y=area.getY()+
            (visibleMidiPitches-1-row)*area.getHeight()/visibleMidiPitches;
        const int next=area.getY()+
            (visibleMidiPitches-row)*area.getHeight()/visibleMidiPitches;
        return {x,y,juce::jmax(2,end-x),juce::jmax(1,next-y-1)};
    }
    int midiNoteAt(juce::Point<int> point) const
    {
        const auto* clip=selectedMidiData();
        if(clip==nullptr)return -1;
        for(int i=(int)clip->notes.size()-1;i>=0;--i)
            if(midiNoteBounds(clip->notes[(size_t)i]).contains(point))return i;
        return -1;
    }
    std::int64_t midiTickAt(int x) const
    {
        const auto* clip=selectedMidiData();
        const auto area=midiPianoBounds();
        if(clip==nullptr || area.getWidth()<1)return 0;
        const double normalized=juce::jlimit(0.0,1.0,
            (double)(x-area.getX())/(double)area.getWidth());
        return juce::jlimit<std::int64_t>(0,clip->lengthTicks-1,
            LibertyPerformMidiEdit::snap(
                (std::int64_t)std::llround(normalized*clip->lengthTicks)));
    }
    int midiPitchAt(int y) const
    {
        const auto area=midiPianoBounds();
        if(area.getHeight()<=0)return 60;
        const int row=juce::jlimit(0,visibleMidiPitches-1,
            (int)((double)(y-area.getY())*visibleMidiPitches/
                 (double)area.getHeight()));
        return juce::jlimit(0,127,
            pianoBasePitch+visibleMidiPitches-1-row);
    }
    void paintMidiPiano(juce::Graphics& g)
    {
        const auto* clip=selectedMidiData();
        if(clip==nullptr)return;
        const auto area=midiPianoBounds();
        const auto velArea=midiVelocityBounds();
        g.setColour(LibertyUI::Colour::canvasDeep);
        g.fillRect(area);
        g.setColour(LibertyUI::Colour::panel);
        g.fillRect(velArea);
        for(int row=0;row<visibleMidiPitches;++row)
        {
            const int pitch=pianoBasePitch+visibleMidiPitches-row-1;
            const int y=area.getY()+row*area.getHeight()/visibleMidiPitches;
            const int bottom=area.getY()+(row+1)*area.getHeight()/visibleMidiPitches;
            if(pitch%12==0)
            {
                g.setColour(juce::Colour(0xff21384a));
                g.fillRect(area.getX(),y,area.getWidth(),juce::jmax(1,bottom-y));
            }
            g.setColour(juce::Colour(0xff263542));
            g.drawHorizontalLine(bottom,(float)area.getX(),(float)area.getRight());
            if(pitch%12==0)
            {
                g.setColour(juce::Colour(0xffe4edf5));
                g.setFont(juce::Font(9.0f));
                const auto name="C"+juce::String(pitch/12-1);
                g.drawText(name,area.getX()-39,y-2,33,
                           juce::jmax(10,bottom-y+4),
                           juce::Justification::centredRight);
            }
        }
        if(clip->lengthTicks>0)
        {
            const auto step=juce::jmax<std::int64_t>(240,
                ((clip->lengthTicks/512+239)/240)*240);
            for(std::int64_t tick=0;tick<=clip->lengthTicks;tick+=step)
            {
                const int x=area.getX()+(int)std::llround((double)tick*
                    area.getWidth()/(double)clip->lengthTicks);
                const bool bar=tick%3840==0;
                g.setColour(bar?juce::Colour(0xff516a81):
                            tick%960==0?juce::Colour(0xff344658):
                            juce::Colour(0xff24313d));
                g.drawVerticalLine(x,(float)area.getY(),(float)area.getBottom());
                if(tick>=clip->lengthTicks)break;
            }
        }
        const auto& visibleNotes=midiGeneratorVisible()?
            clip->generatedNotes:clip->notes;
        for(int i=0;i<(int)visibleNotes.size();++i)
        {
            const auto& note=visibleNotes[(size_t)i];
            const auto bounds=midiNoteBounds(note);
            if(bounds.isEmpty())continue;
            const auto colour=midiGeneratorVisible()?LibertyUI::Colour::accentBlue:
                LibertyUI::Colour::accent;
            LibertyUI::musicalBlock(g,bounds.toFloat(),colour,
                midiEditorVisible()&&i==selectedMidiNote,note.muted);
        }
        g.setColour(juce::Colour(0xff7d91a2));
        g.drawRect(area);
        g.drawRect(velArea);
        for(int i=0;i<(int)visibleNotes.size();++i)
        {
            const auto& note=visibleNotes[(size_t)i];
            const auto x=area.getX()+(int)std::llround(
                (double)note.startTick*area.getWidth()/
                (double)juce::jmax<std::int64_t>(1,clip->lengthTicks));
            const int barHeight=juce::jmax(1,(int)note.velocity*38/127);
            g.setColour(midiEditorVisible() && i==selectedMidiNote?
                         juce::Colour(0xfff4c96d):
                         midiGeneratorVisible()?juce::Colour(0xff67bce5):
                         juce::Colour(0xff45c5c1));
            g.fillRect(x,velArea.getBottom()-barHeight-2,4,barHeight);
        }
        g.setFont(juce::Font(9.5f));
        g.setColour(juce::Colour(0xffb2c3d4));
        g.drawText(juce::String::fromUTF8("VÉLOCITÉ"),area.getX()-63,
                   velArea.getY()+6,60,20,juce::Justification::centredRight);
    }
    bool hasDroppedAudio(int track, int scene) const
    {
        return track >= 0 && track < audioTrackCount() && track < (int)performAudioFiles.size()
            && scene >= 0 && scene < sceneCount
            && performAudioFiles[(size_t)track][(size_t)scene].existsAsFile();
    }

    bool slotHasClip(int track,int scene) const {
        if(scene<0 || scene>=sceneCount)return false;
        if(track<audioTrackCount())return hasDroppedAudio(track,scene);
        return track>=0 && track<(int)performMidiClips.size()
            && performMidiClips[(size_t)track][(size_t)scene].valid;
    }
    juce::String slotName(int track,int scene) const {
        if(!slotHasClip(track,scene))return track<audioTrackCount()?"VIDE":"+ MIDI";
        if(track>=0 && track<(int)clipMetadata.size() &&
           clipMetadata[(size_t)track][(size_t)scene].name.isNotEmpty())
            return clipMetadata[(size_t)track][(size_t)scene].name.substring(0,24);
        if(track>=audioTrackCount()) {
            const auto& clip=performMidiClips[(size_t)track][(size_t)scene];
            return clip.source.existsAsFile()?clip.source.getFileNameWithoutExtension():"CLIP MIDI";
        }
        auto name=performAudioFiles[(size_t)track][(size_t)scene].getFileName();
        return name.length()>22?name.substring(0,19)+"...":name;
    }

    void refreshClipLabels()
    {
        syncTrackContainers();
        const int count=juce::jmin(trackCount(),(int)clipButtons.size());
        for(int track=0;track<count;++track)
        {
            const auto colour=colourForTrack(track);
            for(int scene=0;scene<sceneCount;++scene)
            {
                auto& button=clipButtons[(size_t)track][(size_t)scene];
                const bool valid=slotHasClip(track,scene);
                const int colourId=clipMetadata[(size_t)track][(size_t)scene].colourId;
                const auto clipColour=colourId<0?colour:trackColour(colourId);
                const bool active=player.isTrackPlaying(track,scene);
                const bool queued=player.isTrackQueued(track,scene);
                const bool target=dragTrack==track && dragScene==scene;
                button.setButtonText(target?
                    juce::String::fromUTF8(track<audioTrackCount()?
                        "DÉPOSER AUDIO":"DÉPOSER MIDI"):
                    (queued?"EN ATTENTE":slotName(track,scene)));
                button.setEnabled(true);
                button.setColour(juce::TextButton::buttonColourId,
                    target?juce::Colour(0xff245b70):
                    queued?LibertyUI::Colour::accentAmber.darker(0.65f):
                    active?clipColour.interpolatedWith(LibertyUI::Colour::accent,0.28f):
                    valid?clipColour.interpolatedWith(LibertyUI::Colour::surface,0.32f):
                          LibertyUI::Colour::control.darker(0.20f));
                button.setColour(juce::TextButton::textColourOffId,
                    queued?LibertyUI::Colour::accentAmber.brighter(0.30f):
                    valid||target?LibertyUI::Colour::textPrimary:
                                  LibertyUI::Colour::textMuted);
                if(track>=firstMidiTrack())
                {
                    const int lane=LibertyPerformTiming::instrumentForLogicalTrack(
                        track,audioTrackCount(),owner.getMidiTrackCount(),owner.getInstrumentTrackCount());
                    button.setTooltip(lane>=0?
                        "MIDI vers instrument "+juce::String(lane+1)+
                        juce::String::fromUTF8(". Cliquez sur une cellule vide pour créer un clip."):
                        "Chargez une piste instrument pour entendre ce clip MIDI.");
                }
            }
        }
    }

    void updateDropTarget(juce::Point<int> point,const juce::StringArray& files)
    {
        int newTrack=-1,newScene=-1;
        for(int track=0;track<juce::jmin(trackCount(),(int)clipButtons.size());++track)
        {
            bool supportsFile=false;
            for(const auto& path:files)
                if(track<audioTrackCount()?isSupportedAudioFile(juce::File(path)):
                    isSupportedMidiFile(juce::File(path))){supportsFile=true;break;}
            if(!supportsFile)continue;
            for(int scene=0;scene<sceneCount;++scene)
                if(clipButtons[(size_t)track][(size_t)scene].getBounds().contains(point))
                {newTrack=track;newScene=scene;break;}
            if(newTrack>=0)break;
        }
        if(newTrack!=dragTrack || newScene!=dragScene)
        {dragTrack=newTrack;dragScene=newScene;refreshClipLabels();repaint();}
    }

    void launchClip(int track,int scene)
    {
        if(track<0 || track>=trackCount() || scene<0 || scene>=sceneCount)return;
        if(!slotHasClip(track,scene) && (track<audioTrackCount() ||
            !createMidiFromTrack(track,scene)))return;
        selectClip(track,scene);
        const auto& p=clipMetadata[(size_t)track][(size_t)scene].settings;
        const int action=LibertyPerformClip::launchedScene(
            player.isTrackPlaying(track,scene)?scene:-1,scene,p.launchMode);
        const int quantize=LibertyPerformClip::launchQuantization(
            launchQuantization(),p.quantization);
        player.queueScene(track,action,quantize,
            owner.getTempoBpm(),owner.getTimeSignatureNumerator(),
            owner.getTimeSignatureDenominator());
        refreshClipLabels();
    }

    void launchScene(int scene)
    {
        if(scene<0 || scene>=sceneCount)return;
        selectedFollowScene=scene;
        activeFollowScene=scene;
        updateFollowControls();
        const auto at=player.nextQuantizedSample(launchQuantization(),
            owner.getTempoBpm(),owner.getTimeSignatureNumerator(),
            owner.getTimeSignatureDenominator());
        for(int track=0;track<trackCount();++track)
            player.queueScene(track,slotHasClip(track,scene)?scene:-1,
                launchQuantization(),owner.getTempoBpm(),
                owner.getTimeSignatureNumerator(),owner.getTimeSignatureDenominator());
        const auto rule=LibertyPerformLive::validFollow(followActions[(size_t)scene]);
        followNextAt=rule.action==0?0:
            LibertyPerformLive::nextFollowSample(at,rule,owner.getTempoBpm(),
                owner.getTimeSignatureNumerator(),owner.getTimeSignatureDenominator(),
                juce::jmax(1.0,owner.audioEngine.getSampleRate()));
        refreshClipLabels();
    }

    void initialiseLiveControls()
    {
        for(int scene=0;scene<sceneCount;++scene)
            followSceneSelector.addItem(juce::String::fromUTF8("SCÈNE ")+
                juce::String(scene+1),scene+1);
        followSceneSelector.setSelectedId(1,juce::dontSendNotification);
        followSceneSelector.onChange=[this]
        {
            selectedFollowScene=juce::jlimit(0,sceneCount-1,
                                            followSceneSelector.getSelectedId()-1);
            updateFollowControls();
        };
        addAndMakeVisible(followSceneSelector);
        followActionSelector.addItem("AUCUNE",1);
        followActionSelector.addItem("SUIVANTE",2);
        followActionSelector.addItem(juce::String::fromUTF8("PRÉCÉDENTE"),3);
        followActionSelector.addItem(juce::String::fromUTF8("ALÉATOIRE"),4);
        followActionSelector.addItem("STOP",5);
        followActionSelector.onChange=[this]{commitFollowAction();};
        addAndMakeVisible(followActionSelector);
        for(int bars=1;bars<=32;++bars)
            followBarsSelector.addItem(juce::String(bars)+
                (bars==1?" MESURE":" MESURES"),bars);
        followBarsSelector.onChange=[this]{commitFollowAction();};
        addAndMakeVisible(followBarsSelector);

        for(int i=0;i<8;++i)
            midiLearnTargetSelector.addItem(juce::String::fromUTF8("SCÈNE ")+
                juce::String(i+1),i+1);
        midiLearnTargetSelector.addItem("STOP ALL",9);
        midiLearnTargetSelector.addItem("MACRO MASTER",10);
        midiLearnTargetSelector.addItem("MACRO AUDIO",11);
        midiLearnTargetSelector.addItem("MACRO INSTR.",12);
        midiLearnTargetSelector.addItem("MACRO FADE",13);
        midiLearnTargetSelector.setSelectedId(1,juce::dontSendNotification);
        midiLearnTargetSelector.onChange=[this]{
            midiLearnArmed=false;
            updateMidiLearnLabel();
        };
        addAndMakeVisible(midiLearnTargetSelector);
        midiLearnButton.onClick=[this]
        {
            midiLearnArmed=!midiLearnArmed;
            lastMidiSerial=LibertyPerformLive::unpackMidi(
                owner.audioEngine.getPerformMidiLearnEvent()).serial;
            updateMidiLearnLabel();
        };
        midiForgetButton.setButtonText("EFFACER");
        midiForgetButton.onClick=[this]
        {
            const int target=midiLearnTargetSelector.getSelectedId()-1;
            if(target>=0 && target<LibertyPerformLive::targets)
            {
                midiBindings[(size_t)target]={};
                midiCcGates[(size_t)target]=false;
                ++sessionRevision;
            }
            midiLearnArmed=false;
            updateMidiLearnLabel();
        };
        addAndMakeVisible(midiLearnButton);
        addAndMakeVisible(midiForgetButton);
        const char* labels[]={"MASTER","AUDIO","INSTR.","FADE"};
        for(int i=0;i<4;++i)
        {
            auto& slider=liveMacroSliders[(size_t)i];
            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(juce::Slider::TextBoxRight,false,48,24);
            slider.setRange(0,i==3?1000:200,i==3?10:1);
            slider.setValue(i==3?150:100,juce::dontSendNotification);
            slider.setTextValueSuffix(i==3?" ms":" %");
            slider.setTooltip(juce::String("MACRO ")+juce::String(i+1)+
                              " - "+labels[i]);
            slider.onValueChange=[this]
            {
                if(!updatingMacros)
                {
                    applyLiveMacros();
                    ++sessionRevision;
                }
            };
            addAndMakeVisible(slider);
        }
        applyLiveMacros();
        updateFollowControls();
        updateMidiLearnLabel();
    }

    void applyLiveMacros()
    {
        player.setLiveMasterGain(LibertyPerformLive::macroGain(
            (int)liveMacroSliders[0].getValue()));
        player.setLiveAudioGain(LibertyPerformLive::macroGain(
            (int)liveMacroSliders[1].getValue()));
        player.setLiveInstrumentGain(LibertyPerformLive::macroGain(
            (int)liveMacroSliders[2].getValue()));
        player.setTransitionMilliseconds((int)liveMacroSliders[3].getValue());
    }

    void updateFollowControls()
    {
        if(selectedFollowScene<0 || selectedFollowScene>=sceneCount)return;
        updatingFollow=true;
        const auto& rule=followActions[(size_t)selectedFollowScene];
        followSceneSelector.setSelectedId(selectedFollowScene+1,juce::dontSendNotification);
        followActionSelector.setSelectedId(rule.action+1,juce::dontSendNotification);
        followBarsSelector.setSelectedId(rule.afterBars,juce::dontSendNotification);
        updatingFollow=false;
    }
    void commitFollowAction()
    {
        if(updatingFollow)return;
        followActions[(size_t)selectedFollowScene]=LibertyPerformLive::validFollow(
            {followActionSelector.getSelectedId()-1,followBarsSelector.getSelectedId()});
        // Editing an inactive scene must not interrupt the running follow.
        if(selectedFollowScene==activeFollowScene)
        {
            const auto rule=followActions[(size_t)selectedFollowScene];
            followNextAt=rule.action==0?0:
                LibertyPerformLive::nextFollowSample(player.getSessionSamples(),
                    rule,owner.getTempoBpm(),owner.getTimeSignatureNumerator(),
                    owner.getTimeSignatureDenominator(),
                    juce::jmax(1.0,owner.audioEngine.getSampleRate()));
        }
        ++sessionRevision;
    }
    void pollFollowActions()
    {
        if(followNextAt<=0 || player.getSessionSamples()<followNextAt)return;
        if(activeFollowScene<0 || activeFollowScene>=sceneCount)
        {
            followNextAt=0;
            return;
        }
        const auto rule=followActions[(size_t)activeFollowScene];
        followNextAt=0;
        if(rule.action==0)return;
        const int next=LibertyPerformLive::followScene(
            activeFollowScene,rule.action,followRandomSeed);
        if(next<0)
        {
            player.stopAll();
            activeFollowScene=-1;
            refreshClipLabels();
        }
        else launchScene(next);
    }

    void updateMidiLearnLabel()
    {
        const int target=midiLearnTargetSelector.getSelectedId()-1;
        if(target<0 || target>=LibertyPerformLive::targets)return;
        const auto& binding=midiBindings[(size_t)target];
        const auto description=binding.kind==0?
            juce::String::fromUTF8("NON ASSIGNÉ"):
            juce::String(binding.kind==1?"CC ":"NOTE ")+
            juce::String(binding.number)+" / CH "+juce::String(binding.channel);
        midiLearnButton.setButtonText(midiLearnArmed?
            juce::String("EN ATTENTE MIDI..."):
            juce::String("LEARN : ")+description);
    }
    void pollLiveMidiLearn()
    {
        const auto data=LibertyPerformLive::unpackMidi(
            owner.audioEngine.getPerformMidiLearnEvent());
        if(data.serial==0 || data.serial==lastMidiSerial)return;
        lastMidiSerial=data.serial;
        if(midiLearnArmed)
        {
            const int target=midiLearnTargetSelector.getSelectedId()-1;
            if(target>=0 && target<LibertyPerformLive::targets)
            {
                midiBindings[(size_t)target]=LibertyPerformLive::validBinding(
                    {data.channel,data.kind,data.number});
                midiCcGates[(size_t)target]=false;
                ++sessionRevision;
            }
            midiLearnArmed=false;
            updateMidiLearnLabel();
            return;
        }
        for(int target=0;target<LibertyPerformLive::targets;++target)
        {
            if(!LibertyPerformLive::matches(
                midiBindings[(size_t)target],data.kind,data.channel,data.number))
                continue;
            if(target>=9)
            {
                if(data.kind==1)
                {
                    updatingMacros=true;
                    liveMacroSliders[(size_t)(target-9)].setValue(
                        target==12?(int)std::lround(data.value*1000.0/127):
                            LibertyPerformLive::macroFromMidi(data.value),
                        juce::dontSendNotification);
                    updatingMacros=false;
                    applyLiveMacros();
                    ++sessionRevision;
                }
            }
            else
            {
                const bool gate=data.kind!=1 || data.value>=64;
                const bool trigger=gate && !midiCcGates[(size_t)target];
                midiCcGates[(size_t)target]=data.kind==1?gate:false;
                if(!trigger)continue;
                if(target==8)
                {
                    player.stopAll();
                    followNextAt=0;
                    activeFollowScene=-1;
                    refreshClipLabels();
                }
                else launchScene(target);
            }
        }
    }
    int livePanelHeight() const noexcept
    {return midiWorkspaceVisible()?0:102;}

    int importCapturedMidiIntoArrangement(const std::vector<PerformAudioPlayer::CaptureEvent>& events,
                                          std::int64_t takeSamples,double rate,double bpm)
    {
        const int audioTracks=audioTrackCount();
        const int midiTracks=owner.getMidiTrackCount();
        const int instrumentTracks=owner.getInstrumentTrackCount();
        if(instrumentTracks<=0 || rate<=0 || takeSamples<=0)return 0;
        std::vector<std::vector<MidiEngine::NoteEvent>> byInstrument((size_t)instrumentTracks);
        const auto sampleToTicks=[&](std::int64_t samples)->std::int64_t
        {
            return (std::int64_t)std::llround(
                (double)juce::jmax<std::int64_t>(0,samples) *
                juce::jmax(1.0,bpm)*960.0/(60.0*rate));
        };
        for(int logical=audioTracks;logical<trackCount();++logical)
        {
            const int lane=LibertyPerformTiming::instrumentForLogicalTrack(
                logical,audioTracks,midiTracks,instrumentTracks);
            if(lane<0)continue;
            auto& output=byInstrument[(size_t)lane];
            std::vector<PerformAudioPlayer::CaptureEvent> actions;
            for(const auto& event:events)
                if(event.track==logical)actions.push_back(event);
            std::stable_sort(actions.begin(),actions.end(),
                [](const auto& a,const auto& b){return a.atSample<b.atSample;});
            for(size_t i=0;i<actions.size() && output.size()<8192;++i)
            {
                const auto scene=actions[i].scene;
                if(scene<0 || scene>=sceneCount)continue;
                const auto first=juce::jlimit<std::int64_t>(
                    0,takeSamples,actions[i].atSample);
                const auto last=juce::jlimit<std::int64_t>(
                    first,takeSamples,i+1<actions.size()?
                        actions[i+1].atSample:takeSamples);
                if(last<=first)continue;
                const auto& clip=performMidiClips[(size_t)logical][(size_t)scene];
                if(!clip.valid || clip.lengthTicks<=0)continue;
                const auto& settings=clipMetadata[(size_t)logical][(size_t)scene].settings;
                const auto lengthTicks=LibertyPerformClip::midiLengthTicks(
                    clip.lengthTicks,settings.midiBars,
                    owner.getTimeSignatureNumerator(),
                    owner.getTimeSignatureDenominator());
                const auto cycle=juce::jmax<std::int64_t>(1,
                    LibertyPerformTiming::ticksToSamples(lengthTicks,bpm,rate));
                const auto& notes=clip.generatorState==2?clip.generatedNotes:clip.notes;
                const bool repeat=loopButton.getToggleState() && settings.loop;
                const auto offset=juce::jmax<std::int64_t>(
                    0,actions[i].sourceOffsetSamples);
                const auto startOfCycle=first-(offset%cycle);
                const auto iterations=repeat?
                    juce::jmin<std::int64_t>(8192,
                        (last-startOfCycle+cycle-1)/cycle):1;
                for(std::int64_t iteration=0;
                    iteration<iterations && output.size()<8192;++iteration)
                {
                    const auto origin=startOfCycle+iteration*cycle;
                    if(origin>=last)break;
                    for(const auto& original:notes)
                    {
                        if(output.size()>=8192)break;
                        if(original.muted || original.lengthTicks<=0 ||
                           original.startTick<0)continue;
                        const auto onset=origin+LibertyPerformTiming::ticksToSamples(
                            original.startTick,bpm,rate);
                        if(onset>=last || onset>=origin+cycle)continue;
                        const auto end=juce::jmin(last,juce::jmin(origin+cycle,
                            origin+LibertyPerformTiming::ticksToSamples(
                                original.startTick+original.lengthTicks,bpm,rate)));
                        if(end<=onset || end<=first)continue;
                        const auto recordedOnset=juce::jmax(first,onset);
                        auto note=original;
                        note.pitch=(std::uint8_t)LibertyPerformClip::notePitch(
                            (int)original.pitch,settings.transpose);
                        note.startTick=sampleToTicks(recordedOnset);
                        note.lengthTicks=juce::jmax<std::int64_t>(
                            1,sampleToTicks(end)-note.startTick);
                        output.push_back(note);
                    }
                }
            }
        }
        int committed=0;
        const double lengthSeconds=(double)takeSamples/rate;
        for(int lane=0;lane<instrumentTracks;++lane)
        {
            auto& notes=byInstrument[(size_t)lane];
            if(notes.empty())continue;
            std::stable_sort(notes.begin(),notes.end(),[](const auto& a,const auto& b)
                {return a.startTick<b.startTick;});
            if(commitLibertyPerformCapturedInstrumentClip(
                owner,notes,lane,0.0,lengthSeconds))++committed;
        }
        return committed;
    }

    void toggleCaptureToArrange()
    {
        if (!player.isCapturing())
        {
            juce::String error;
            if (!player.startCapture(error))
            {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                       "Liberty - PERFORM", error, "OK");
                return;
            }
            captureTempoBpm=owner.getTempoBpm();
            recordArrangeButton.setButtonText(juce::String::fromUTF8("ARRÊTER CAPTURE"));
            recordArrangeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff9b4545));
        }
        else
        {
            finishCaptureToArrange();
        }
    }

    void finishCaptureToArrange()
    {
        const auto capture = player.stopCapture();
        const auto events=player.getCaptureEvents();
        const auto takeSamples=player.getCapturedSamples();
        const auto takeRate=juce::jmax(1.0,owner.audioEngine.getSampleRate());
        // Import MIDI while original PERFORMANCE logical track indices still
        // match, before adding an audio arrangement track.
        const int midiTracksImported=importCapturedMidiIntoArrangement(
            events,takeSamples,takeRate,captureTempoBpm);
        recordArrangeButton.setButtonText("CAPTURER VERS ARRANGEMENT");
        recordArrangeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff31404d));
        if (!capture.existsAsFile()) return;

        // A capture creates a new, dedicated arrangement lane.
        const int targetTrack=owner.addAudioTrack();
        if(targetTrack<0)
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                "Liberty - PERFORMANCE",
                juce::String::fromUTF8("Impossible de créer la piste audio de capture."),"OK");
            return;
        }
        // Audio rows precede MIDI/instrument rows. Preserve every existing
        // PERFORMANCE clip, player state and Follow target after insertion.
        if(targetTrack<=(int)performMidiClips.size())
        {
            player.insertNewAudioTrackAt(targetTrack);
            performMidiClips.insert(performMidiClips.begin()+targetTrack,
                std::array<SessionMidiData,sceneCount>{});
            clipMetadata.insert(clipMetadata.begin()+targetTrack,
                std::array<ClipMetadata,sceneCount>{});
            activeTrackScene.insert(activeTrackScene.begin()+targetTrack,-1);
            performAudioFiles.resize((size_t)owner.getAudioTrackCount());
            if(selectedClipTrack>=targetTrack)++selectedClipTrack;
        }
        syncTrackContainers();

        juce::String error;
        if (!owner.audioEngine.loadAudioFileIntoTrack(targetTrack, capture, error))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                   "Liberty - PERFORM", error, "OK");
            return;
        }
        owner.audioEngine.setTrackStartSeconds(targetTrack, 0.0);
        owner.trackSourceFiles[(size_t)targetTrack] = capture;
        owner.rebuildWaveformCache(targetTrack);
        owner.selectedTrack = targetTrack;
        if(midiTracksImported>0)
            recordArrangeButton.setTooltip(
                juce::String(midiTracksImported)+
                juce::String::fromUTF8(
                    " piste(s) MIDI enregistrée(s) avec le mix audio."));
        toFront(false);
        owner.repaint();
    }

    void timerCallback() override
    {
        if (!performVisible) return;
        const auto wanted = juce::Rectangle<int>(0, transportHeight, owner.getWidth(), juce::jmax(1, owner.getHeight() - transportHeight));
        if (getBounds() != wanted) setBounds(wanted);
        player.setTempoBpm(owner.getTempoBpm());
        pollLiveMidiLearn();
        pollFollowActions();
        // Keep z-order stable while PERFORM is running. Repeated toFront()
        // competes with the Browser's always-on-top layer and causes flicker.
        refreshClipLabels();
        repaint();
    }

    MainComponent& owner;
    PerformAudioPlayer player;
    std::deque<std::array<juce::TextButton, sceneCount>> clipButtons;
    std::array<juce::TextButton, sceneCount> sceneButtons;
    std::deque<juce::TextButton> stopTrackButtons;
    juce::TextButton stopAllButton, recordArrangeButton;
    std::array<LibertyPerformLive::Follow,LibertyPerformLive::scenes> followActions {};
    std::array<LibertyPerformLive::Binding,LibertyPerformLive::targets> midiBindings {};
    std::array<bool,LibertyPerformLive::targets> midiCcGates {};
    std::array<juce::Slider,4> liveMacroSliders;
    juce::ComboBox followSceneSelector,followActionSelector,followBarsSelector,
                   midiLearnTargetSelector;
    juce::TextButton midiLearnButton,midiForgetButton;
    bool updatingFollow=false,midiLearnArmed=false,updatingMacros=false;
    std::int64_t followNextAt=0;
    std::uint32_t followRandomSeed=0x8f31c05u,lastMidiSerial=0;
    int selectedFollowScene=0,activeFollowScene=-1;
    double captureTempoBpm=120.0;
    std::vector<juce::Rectangle<int>> trackHeaders;
    std::array<juce::Rectangle<int>, sceneCount> sceneRows;
    std::vector<int> activeTrackScene;
    std::vector<std::array<juce::File, sceneCount>> performAudioFiles;
    std::vector<std::array<SessionMidiData, sceneCount>> performMidiClips;
    std::vector<std::array<ClipMetadata, sceneCount>> clipMetadata;
    int selectedClipTrack=-1,selectedClipScene=-1,inspectorTab=0;
    bool updatingInspector=false,updatingMidiSliders=false,midiSliderGesture=false;
    bool updatingGenerator=false;
    int selectedMidiNote=-1,pianoBasePitch=48;
    int gestureNote=-1;
    bool gestureResizing=false,gestureVelocity=false,gestureHistorySaved=false;
    juce::Point<int> gestureStart;
    std::vector<MidiEngine::NoteEvent> gestureOriginal;
    std::int64_t midiLastClickTick=0;
    std::uint32_t humanizeSeed=0x2da13bc1u;
    juce::TextEditor inspectorName;
    juce::ComboBox inspectorColour,inspectorMidiBars,inspectorQuantize,inspectorMode;
    juce::ToggleButton inspectorLoop;
    juce::Slider inspectorGain,inspectorTranspose,inspectorIn,inspectorOut;
    juce::TextButton inspectorClipTab,inspectorLaunchTab,inspectorMidiTab,inspectorClose;
    juce::TextButton inspectorTransformTab,inspectorGenerateTab;
    juce::TextButton generatorPreviewButton,generatorApplyButton,
                     generatorOriginalButton,generatorNewVariantButton;
    juce::ComboBox generatorKind,generatorStyle,generatorDivision,
                   generatorDirection,generatorOctaves,generatorSteps;
    juce::Slider generatorDensity,generatorSwing,generatorVariation,generatorVelocity;
    juce::TextButton midiAddButton,midiDeleteButton,midiHumanizeButton,midiUndoButton,midiRedoButton;
    juce::Slider midiPitchSlider,midiVelocitySlider,midiLengthSlider,midiHumanizeAmount;
    juce::ComboBox quantizeSelector;
    juce::TextButton loopButton;
    std::uint64_t sessionRevision=0;
    int dragTrack = -1;
    int dragScene = -1;
    bool performVisible = false;
};

class PerformController final : private juce::Timer
{
public:
    explicit PerformController(MainComponent& ownerIn) : owner(ownerIn), view(ownerIn)
    {
        performButton.setButtonText("PERFORM");
        performButton.setMouseClickGrabsKeyboardFocus(false);
        performButton.setClickingTogglesState(false);
        performButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        performButton.onClick = [this]
        {
            if (!performVisible) setLibertyMixConsoleVisible(&owner, false);
            setPerformVisible(true);
        };
        owner.addAndMakeVisible(performButton);

        arrangeOverlay.setButtonText("ARRANGE");
        mixOverlay.setButtonText("MIXCONSOLE");
        for (auto* b : { &arrangeOverlay, &mixOverlay })
        {
            b->setMouseClickGrabsKeyboardFocus(false);
            b->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252a31));
            b->setVisible(false);
            owner.addAndMakeVisible(*b);
        }
        arrangeOverlay.onClick = [this]
        {
            setPerformVisible(false);
            setLibertyMixConsoleVisible(&owner, false);
        };
        mixOverlay.onClick = [this]
        {
            setPerformVisible(false);
            setLibertyMixConsoleVisible(&owner, true);
        };

        startTimerHz(30);
    }

    ~PerformController() override { shutdown(); }

    void shutdown()
    {
        if (stopped.exchange(true)) return;
        stopTimer();
        view.setPerformVisible(false);
        performButton.setVisible(false);
        arrangeOverlay.setVisible(false);
        mixOverlay.setVisible(false);
    }

    void setPerformVisible(bool shouldShow)
    {
        if (stopped.load()) return;
        performVisible = shouldShow;
        view.setPerformVisible(shouldShow);
        arrangeOverlay.setVisible(shouldShow);
        mixOverlay.setVisible(shouldShow);
        performButton.setColour(juce::TextButton::buttonColourId,
                                shouldShow ? juce::Colour(0xff315f7a) : juce::Colour(0xff252a31));
        // The Browser is an always-on-top sibling. Raising PERFORM again
        // here can reorder its header and cause a visible flash.
        // PerformView already raises itself once when becoming visible.
        arrangeOverlay.toFront(false);
        mixOverlay.toFront(false);
        performButton.toFront(false);
        owner.repaint();
    }

    void renderAudioBlock(float* const* outputs, int numOutputs, int numSamples)
    {
        if (performVisible) view.renderAudioBlock(outputs, numOutputs, numSamples);
    }
    void appendMidi(int track,juce::MidiBuffer& midi,int samples)
    {if(performVisible)view.appendMidiForInstrument(track,midi,samples);}
    float instrumentMacro() const noexcept
    {return view.instrumentMacro();}
    void finalMix(float* const* data,int channels,int samples)
    {view.finalMix(data,channels,samples);}
    void clearSession(){view.clearSession();}
    void saveSession(juce::XmlElement& xml,const juce::File& file) const
    {view.saveSession(xml,file);}
    void loadSession(const juce::XmlElement& xml,const juce::File& file)
    {view.loadSession(xml,file);}
    std::uint64_t getSessionRevision() const noexcept {return view.getSessionRevision();}

    bool isVisible() const noexcept { return performVisible; }

private:
    void timerCallback() override
    {
        if (stopped.load()) return;
        arrangeOverlay.setBounds(1055, 8, 88, 26);
        mixOverlay.setBounds(1147, 8, 112, 26);
        performButton.setBounds(1263, 8, 94, 26);
        // Page controls are raised on mode activation, not on every timer
        // tick. Constantly changing sibling z-order makes the Browser header
        // and its resize handle flash while PERFORM is visible.
    }

    MainComponent& owner;
    PerformView view;
    juce::TextButton arrangeOverlay, mixOverlay, performButton;
    std::atomic<bool> stopped { false };
    bool performVisible = false;
};

std::map<MainComponent*, std::unique_ptr<PerformController>> controllers;

class Bootstrap final : private juce::Timer
{
public:
    Bootstrap() { startTimerHz(10); }
    ~Bootstrap() override { shutdown(); }

    void shutdown()
    {
        stopTimer();
        for (auto& entry : controllers)
            if (entry.second) entry.second->shutdown();
        controllers.clear();
    }

private:
    void timerCallback() override
    {
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* window = dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i)))
                if (auto* main = dynamic_cast<MainComponent*>(window->getContentComponent()))
                    if (controllers.find(main) == controllers.end())
                    {
                        controllers.emplace(main, std::make_unique<PerformController>(*main));
                        stopTimer();
                        return;
                    }
    }
};

Bootstrap bootstrap;
}

void renderLibertyPerformAudio(AudioEngine* engine, float* const* outputs, int numOutputs, int numSamples)
{
    if (engine == nullptr) return;
    for (auto& entry : controllers)
        if (entry.first != nullptr && &entry.first->audioEngine == engine && entry.second)
        {
            entry.second->renderAudioBlock(outputs, numOutputs, numSamples);
            return;
        }
}

void appendLibertyPerformMidi(AudioEngine* engine,int instrument,juce::MidiBuffer& midi,int samples)
{
    if(engine==nullptr)return;
    for(auto& entry:controllers)
        if(entry.first!=nullptr && &entry.first->audioEngine==engine && entry.second)
        {entry.second->appendMidi(instrument,midi,samples);return;}
}

float getLibertyPerformInstrumentMacro(AudioEngine* engine) noexcept
{
    if(engine==nullptr)return 1.f;
    for(auto& entry:controllers)
        if(entry.first!=nullptr && &entry.first->audioEngine==engine && entry.second)
            return entry.second->instrumentMacro();
    return 1.f;
}
void processLibertyPerformFinalMix(AudioEngine* engine,float* const* data,int channels,int samples)
{
    if(engine==nullptr)return;
    for(auto& entry:controllers)
        if(entry.first!=nullptr && &entry.first->audioEngine==engine && entry.second)
        {
            entry.second->finalMix(data,channels,samples);
            return;
        }
}
void ensureLibertyPerformController(MainComponent* owner);
void saveLibertyPerformSession(MainComponent* owner,juce::XmlElement& xml,const juce::File& file)
{
    if(owner==nullptr)return;
    ensureLibertyPerformController(owner);
    if(auto it=controllers.find(owner);it!=controllers.end() && it->second)
        it->second->saveSession(xml,file);
}
void loadLibertyPerformSession(MainComponent* owner,const juce::XmlElement& xml,const juce::File& file)
{
    if(owner==nullptr)return;
    ensureLibertyPerformController(owner);
    if(auto it=controllers.find(owner);it!=controllers.end() && it->second)
        it->second->loadSession(xml,file);
}
void clearLibertyPerformSession(MainComponent* owner)
{
    if(auto it=controllers.find(owner);it!=controllers.end() && it->second)
        it->second->clearSession();
}
std::uint64_t getLibertyPerformSessionRevision(const MainComponent* owner)
{
    if(auto it=controllers.find(const_cast<MainComponent*>(owner));it!=controllers.end() && it->second)
        return it->second->getSessionRevision();
    return 0;
}

void ensureLibertyPerformController(MainComponent* owner)
{
    if (owner != nullptr && !controllers.count(owner))
        controllers.emplace(owner, std::make_unique<PerformController>(*owner));
}

bool isLibertyPerformVisible(MainComponent* owner)
{
    if (owner == nullptr) return false;
    const auto it = controllers.find(owner);
    return it != controllers.end() && it->second && it->second->isVisible();
}

void setLibertyPerformVisible(MainComponent* owner, bool shouldShow)
{
    if (owner == nullptr) return;
    ensureLibertyPerformController(owner);
    const auto it = controllers.find(owner);
    if (it == controllers.end() || !it->second) return;
    it->second->setPerformVisible(shouldShow);
}

void shutdownLibertyPerformController()
{
    bootstrap.shutdown();
}
